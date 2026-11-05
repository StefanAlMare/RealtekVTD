#!/usr/bin/env python3
"""Bound-interface TCP diagnostics, with end-to-end SHA256 validation. No external services."""
import argparse,hashlib,ipaddress,json,pathlib,socket,subprocess,sys,time
from registry import read_registry
ROOT=pathlib.Path(__file__).resolve().parents[1]
BLOCK=bytes(range(256))*256
LIMIT=32*1024*1024

def line(sock):
 data=bytearray()
 while len(data)<2048:
  byte=sock.recv(1)
  if not byte:raise RuntimeError('Connection ended before protocol reply')
  if byte==b'\n':return json.loads(data)
  data.extend(byte)
 raise RuntimeError('Protocol line too long')

def reply(sock,obj):sock.sendall(json.dumps(obj).encode()+b'\n')

def transfer(sock,size,send):
 digest=hashlib.sha256();total=0;start=time.monotonic()
 while total<size:
  count=min(len(BLOCK),size-total)
  if send:
   data=BLOCK[:count];sock.sendall(data)
  else:
   data=sock.recv(count)
   if not data:raise RuntimeError(f'Connection ended at {total}/{size} bytes')
  digest.update(data);total+=len(data)
 return {'bytes':total,'seconds':time.monotonic()-start,'sha256':digest.hexdigest()}

def handle(sock):
 request=line(sock);size=request.get('bytes',0);mode=request.get('mode')
 if request.get('protocol')!='RealtekVTDNext-LAN-1' or mode not in ['upload','download'] or not isinstance(size,int) or not 1<=size<=LIMIT:
  raise RuntimeError('Invalid diagnostic request')
 reply(sock,{'ready':True})
 result=transfer(sock,size,mode=='download');reply(sock,result);return mode,result

def detect():
 iface,driver=read_registry()
 source=subprocess.check_output(['/usr/sbin/ipconfig','getifaddr',iface],text=True).strip()
 ipaddress.IPv4Address(source);return iface,source,driver

def client_round(peer,port,size,source=None,iface=None,mss=None):
 results=[]
 for mode in ['upload','download']:
  with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as sock:
   sock.settimeout(45)
   if iface:
    if sys.platform!='darwin':raise RuntimeError('Bound-interface client requires macOS')
    # IP_BOUND_IF = 25, as defined by the pinned macOS kernel SDK netinet/in.h.
    sock.setsockopt(socket.IPPROTO_IP,25,socket.if_nametoindex(iface))
   if source:sock.bind((source,0))
   if mss:sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_MAXSEG,mss)
   sock.connect((peer,port));effective=sock.getsockopt(socket.IPPROTO_TCP,socket.TCP_MAXSEG)
   reply(sock,{'protocol':'RealtekVTDNext-LAN-1','mode':mode,'bytes':size})
   if line(sock).get('ready') is not True:raise RuntimeError('Receiver did not acknowledge test')
   start=time.monotonic();local=transfer(sock,size,mode=='upload');remote=line(sock)
   elapsed=time.monotonic()-start
   if local['bytes']!=remote.get('bytes') or local['sha256']!=remote.get('sha256'):raise RuntimeError('End-to-end data integrity mismatch')
   results.append({'direction':mode,'mss_requested':mss or 'default','mss_effective':effective,'mbps':round(size*8/elapsed/1e6,2),
    'seconds':round(elapsed,3),'bytes':size,'integrity':'SHA256 PASS','local':sock.getsockname()[0]})
 return results

def main():
 p=argparse.ArgumentParser();sub=p.add_subparsers(dest='mode',required=True)
 server=sub.add_parser('server');server.add_argument('--listen',required=True);server.add_argument('--seconds',type=int,default=600);server.add_argument('--port',type=int,default=49072)
 client=sub.add_parser('client');client.add_argument('--peer',required=True);client.add_argument('--port',type=int,default=49072);client.add_argument('--mib',type=int,default=8)
 args=p.parse_args();out=ROOT/'build/local-evidence';out.mkdir(parents=True,exist_ok=True)
 messages=[]
 def log(message):print(message,flush=True);messages.append(message)
 name='lan-server-latest.txt' if args.mode=='server' else 'lan-test-latest.txt';status=0
 try:
  log(time.strftime('RealtekVTDNext LAN test %Y-%m-%d %H:%M:%S'))
  if args.mode=='server':
   address=ipaddress.IPv4Address(args.listen)
   if address.is_unspecified or address.is_multicast:raise RuntimeError('Provide this computer’s wired IPv4 address')
   if not 1<=args.seconds<=600:raise RuntimeError('--seconds must be 1..600')
   with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as listener:
    listener.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);listener.bind((args.listen,args.port));listener.listen(2);listener.settimeout(1)
    log(f'Receiver ready at {args.listen}:{args.port} for {args.seconds} seconds; Ctrl+C stops it')
    deadline=time.monotonic()+args.seconds
    while time.monotonic()<deadline:
     try:connection,peer=listener.accept()
     except socket.timeout:continue
     with connection:
      connection.settimeout(45)
      try:direction,result=handle(connection);log(f'{peer[0]} {direction}: {json.dumps(result)}')
      except Exception as exc:log(f'{peer[0]} failed: {exc}')
  else:
   peer=ipaddress.IPv4Address(args.peer)
   if peer.is_unspecified or peer.is_multicast or peer.is_loopback:raise RuntimeError('Use another computer’s wired IPv4 address')
   if not 1<=args.mib<=32:raise RuntimeError('--mib must be 1..32')
   iface,source,before=detect()
   if source==args.peer:raise RuntimeError('Peer must be another computer')
   log(f'Bound driver interface: {iface}; source {source}; receiver {peer}')
   before_tcp=subprocess.run(['/usr/sbin/netstat','-s','-p','tcp'],capture_output=True,text=True).stdout
   (out/'tcp-before.txt').write_text(before_tcp)
   for mss in [None]:
    log('Testing normal MSS' if mss is None else 'Testing small MSS 536')
    for result in client_round(args.peer,args.port,args.mib*1024*1024,source,iface,mss):log(json.dumps(result))
   time.sleep(2);_,_,after=detect()
   log('Driver counter change: '+json.dumps({key:after.get(key,0)-before.get(key,0) for key in ['Stage3BTXSubmitted','Stage3BTXCompleted','Stage3BRXDelivered','Stage3BRXDropped','Stage3BTXErrors','Stage3BInterruptActions','Stage3CTXDropped','Stage3CTXDropMapping','Stage3CTXDropSize','Stage3CTXDropSubmit','Stage3CTXRingFull','Stage3CTXMapCalls','Stage3CTXMapOver1MS','Stage3CTXPendingChecksum']}))
   after_tcp=subprocess.run(['/usr/sbin/netstat','-s','-p','tcp'],capture_output=True,text=True).stdout
   (out/'tcp-after.txt').write_text(after_tcp)
   log('TRANSFER COMPLETE; SHA256 verified. Throughput is diagnostic, not a production speed benchmark.')
 except KeyboardInterrupt:log('Receiver stopped')
 except Exception as exc:log('TEST INCOMPLETE: '+str(exc));status=1
 finally:(out/name).write_text('\n'.join(messages)+'\n')
 return status
if __name__=='__main__':sys.exit(main())
