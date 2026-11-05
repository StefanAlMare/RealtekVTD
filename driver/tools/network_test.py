#!/usr/bin/env python3
"""Observe this driver's own interface and ping its DHCP router using its assigned source IP."""
import argparse,ipaddress,pathlib,re,subprocess,sys,time
from registry import read_registry
p=argparse.ArgumentParser();p.add_argument('--interface');p.add_argument('--target');args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
out=root/'build/local-evidence';out.mkdir(parents=True,exist_ok=True)
lines=[]
def record(text):
 print(text,flush=True);lines.append(text)
def command(cmd):
 result=subprocess.run(cmd,text=True,capture_output=True);return result.returncode,result.stdout+result.stderr

def driver(tree):
 if isinstance(tree,list):
  for item in tree:
   found=driver(item)
   if found:return found
 if isinstance(tree,dict):
  if tree.get('DriverVersion')=='0.20.5-JTX1-DIAG':return tree
  for child in tree.get('IORegistryEntryChildren',[]):
   found=driver(child)
   if found:return found
 return None

def names(tree):
 result=[]
 if isinstance(tree,dict):
  name=tree.get('BSD Name') or tree.get('IOBSDName')
  if isinstance(name,str) and re.fullmatch(r'en\d+',name):result.append(name)
  for child in tree.get('IORegistryEntryChildren',[]):result+=names(child)
 elif isinstance(tree,list):
  for child in tree:result+=names(child)
 return result
success=False
try:
 record(time.strftime('STAGE3B network test %Y-%m-%d %H:%M:%S'))
 _,snapshot=read_registry()
 tree=[snapshot]
 before=driver(tree)
 if not before or not before.get('Stage3BEnabled'):raise RuntimeError('STAGE3B driver is not enabled; send COLLECT_LOCAL evidence')
 detected=sorted(set(names(tree)))
 if len(detected)!=1:raise RuntimeError(f'Expected one interface belonging to RealtekVTDNextEthernet; found {detected}')
 iface=args.interface or detected[0]
 if iface!=detected[0]:raise RuntimeError('Requested interface is not owned by RealtekVTDNextEthernet')
 record('Driver interface: '+iface)
 code,config=command(['/sbin/ifconfig',iface]);record(config)
 _,ip=command(['/usr/sbin/ipconfig','getifaddr',iface]);ip=ip.strip()
 try:address=ipaddress.IPv4Address(ip)
 except ValueError:raise RuntimeError('No IPv4 address assigned. Select DHCP in macOS network settings, wait 60 seconds, then repeat.')
 if address.is_link_local or address.is_unspecified:raise RuntimeError('Only a self-assigned/invalid IPv4 address is available; DHCP has not been demonstrated.')
 record('Assigned IPv4: '+ip)
 code,dhcp=command(['/usr/sbin/ipconfig','getpacket',iface]);record('DHCP lease:\n'+dhcp)
 if code or not re.search(r'yiaddr\s*=\s*'+re.escape(ip)+r'\b',dhcp) or 'dhcp_message_type' not in dhcp:raise RuntimeError('No DHCP lease evidence. This test requires DHCP, not a static address.')
 _,router=command(['/usr/sbin/ipconfig','getoption',iface,'router']);router=router.strip().split()
 target=args.target or (router[0] if router else '')
 try:ipaddress.IPv4Address(target)
 except ValueError:raise RuntimeError('DHCP did not provide a router. Repeat with --target <IPv4 peer on the same wired LAN>.')
 if target==ip:raise RuntimeError('The test target must be a different computer/router.')
 # Scope ping to the driver's interface AND its assigned address, excluding Wi-Fi success.
 record(f'Ping via {iface}, source {ip}, target {target}')
 code,ping=command(['/sbin/ping','-b',iface,'-S',ip,'-c','10','-W','2000',target]);record(ping)
 success=code==0 and re.search(r'\b0(?:\.0)?% packet loss',ping) is not None
 # Wait for the driver's once-per-second property publication, not for completion polling.
 time.sleep(2)
 _,after=read_registry()
 tx=after.get('Stage3BTXCompleted',0)-before.get('Stage3BTXCompleted',0)
 rx=after.get('Stage3BRXDelivered',0)-before.get('Stage3BRXDelivered',0)
 record(f'Driver counter increase: TX={tx}, RX={rx}; IRQ actions={after.get("Stage3BInterruptActions",0)}')
 success=success and tx>=10 and rx>=10 and after.get('Stage3BEnabled') is True and not after.get('Stage3BUnsafeMappingRetained')
 record('PASS: DHCP lease, 10/10 ping replies and driver TX/RX progress' if success else 'NOT PASS: ping incomplete; send the evidence archive for diagnosis')
except Exception as exc:
 record('NOT PASS: '+str(exc))
finally:
 (out/'network-test-latest.txt').write_text('\n'.join(lines)+'\n')
sys.exit(0 if success else 1)
