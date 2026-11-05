#!/usr/bin/env python3
"""Read-only evidence check; never install/load or change network state."""
import argparse,json,pathlib,plistlib,subprocess,sys
VERSION='0.20.5-JTX1-DIAG'
def evaluate(d,machine):
 errors=[]
 def require(ok,message):
  if not ok:errors.append(message)
 profiles={'asus':('RTL8411B-MAC_VER_44',0x1043,0x200f,0x12,0x5c8),
           'acer':('RTL8168H-MAC_VER_46',0x1025,0x100c,0x15,0x541)}
 profile,sv,sd,rev,xid=profiles[machine]
 require(d.get('DriverVersion')==VERSION,'Exact 0.20.4 driver required')
 require(d.get('ChipProfile')==profile and d.get('PCISubsystemVendor')==sv and d.get('PCISubsystemDevice')==sd and d.get('PCIRevision')==rev and d.get('XID')==xid,'Hardware identity does not match '+machine)
 require(d.get('Stage3BEnabled') is True,'Driver must be enabled')
 require(d.get('PHYDiagValidMask')==511 and d.get('PHYDiagReadFailures')==0,'Complete valid PHYDIAG sample required')
 require(d.get('PHYAutonegConfigured') is True and d.get('PHYAutonegStatus')==0,'Autoneg configuration must have succeeded')
 require(d.get('PHYDiagLink') is True and d.get('PHYDiagAutonegEnabled') is True and d.get('PHYDiagAutonegComplete') is True,'Link and completed enabled autoneg required')
 bmsr=d.get('PHYCapabilitiesBMSR',0);ext=d.get('PHYCapabilitiesESTATUS',0)
 anar=d.get('PHYDiagANAR',0);ctrl=d.get('PHYDiagCTRL1000',0)
 supported=(bmsr&0x7800)>>6
 require(anar&0x1e0==supported,'10/100 advertising must exactly match BMSR')
 require(not bmsr&0x100 or ctrl&0x300==(ext&0x3000)>>4,'Gigabit advertising must match base-T ESTATUS')
 speed=d.get('PHYDiagSpeedMbps',0)
 require(d.get('PHYDiagFullDuplex') is True,'Full duplex required')
 if machine=='acer' or d.get('PHYDiagPartner1000Full') is True:
  require(speed==1000,'Gigabit full-duplex acceptance target required')
 else:require(speed in (100,1000),'Asus must negotiate at least 100 Mb/s; 10 Mb/s is NOT PASS')
 require(isinstance(d.get('Stage3BTXSubmitted'),int) and d.get('Stage3BTXSubmitted')==d.get('Stage3BTXCompleted'),'TX submitted/completed must match in this sample; rerun after traffic settles if busy')
 require(d.get('Stage3BTXWatchdog') is False,'TX watchdog must be false')
 require(d.get('Stage3BRXDropped')==0,'RX dropped must be zero')
 require(d.get('DMAMode')=='device-mapper-native64' and d.get('DeviceMapperPresent') is True,'Native device mapper path required for these AppleVTD machines')
 if machine=='acer':require(d.get('PHYDiagEEEAdvertisement')==0,'MAC_VER_46 EEE advertisement must remain disabled')
 return errors

def main():
 p=argparse.ArgumentParser();p.add_argument('--machine',choices=['asus','acer'],required=True);args=p.parse_args()
 root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/local-evidence';out.mkdir(parents=True,exist_ok=True)
 try:
  r=subprocess.run(['/usr/sbin/ioreg','-a','-r','-c','RealtekVTDNextEthernet'],capture_output=True,check=True)
  tree=plistlib.loads(r.stdout);nodes=[]
  def walk(v):
   if isinstance(v,dict):
    if 'DriverVersion' in v:nodes.append(v)
    for child in v.get('IORegistryEntryChildren',[]):walk(child)
   elif isinstance(v,list):
    for child in v:walk(child)
  walk(tree)
  if len(nodes)!=1:raise RuntimeError('Expected exactly one driver controller')
  d=nodes[0];errors=evaluate(d,args.machine)
  data={k:v for k,v in d.items() if k.startswith(('PHY','ChipProfile','PCI','XID','MACVersion','DMAMode','DeviceMapper','Stage3')) and isinstance(v,(str,int,bool))}
  result={'machine':args.machine,'pass':not errors,'errors':errors,'properties':data}
 except Exception as exc:result={'machine':args.machine,'pass':False,'errors':[str(exc)]}
 (out/'autoneg-acceptance-latest.json').write_text(json.dumps(result,indent=2)+'\n')
 print('PASS: '+args.machine+' autoneg hardware sample' if result['pass'] else 'NOT PASS: '+'; '.join(result['errors']))
 return 0 if result['pass'] else 1
if __name__=='__main__':sys.exit(main())
