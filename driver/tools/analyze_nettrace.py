#!/usr/bin/env python3
"""Summarize incoming TCP checksums/test-pattern integrity; never emit payload bytes."""
import argparse, collections, ipaddress, json, struct
from pathlib import Path

def checksum(data):
 if len(data)%2:data+=b'\0'
 total=sum(struct.unpack('!%dH'%(len(data)//2),data))
 while total>>16:total=(total&65535)+(total>>16)
 return (~total)&65535

def analyze(path,acer='192.168.10.105',imac='192.168.10.10',port=49072):
 raw=Path(path).read_bytes()
 if len(raw)<24:raise ValueError('Missing pcap header')
 formats={b'\xd4\xc3\xb2\xa1':('<',1e6),b'\xa1\xb2\xc3\xd4':('>',1e6),b'\x4d\x3c\xb2\xa1':('<',1e9),b'\xa1\xb2\x3c\x4d':('>',1e9)}
 if raw[:4] not in formats:raise ValueError('Expected classic pcap')
 endian,scale=formats[raw[:4]]
 if struct.unpack_from(endian+'I',raw,20)[0]!=1:raise ValueError('Expected Ethernet link type')
 flows={};cursor=24;captured=0;skipped=0
 while cursor<len(raw):
  if cursor+16>len(raw):raise ValueError('Truncated record header')
  sec,frac,size,wire=struct.unpack_from(endian+'IIII',raw,cursor);cursor+=16
  if cursor+size>len(raw):raise ValueError('Truncated record body')
  frame=raw[cursor:cursor+size];cursor+=size;captured+=1
  if len(frame)<34:skipped+=1;continue
  offset=14;eth=struct.unpack_from('!H',frame,12)[0]
  while eth in (0x8100,0x88a8) and offset+4<=len(frame):
   eth=struct.unpack_from('!H',frame,offset+2)[0];offset+=4
  if eth!=0x0800:skipped+=1;continue
  ip=frame[offset:]
  if len(ip)<20 or ip[0]>>4!=4:skipped+=1;continue
  ihl=(ip[0]&15)*4
  if ihl<20 or len(ip)<ihl or ip[9]!=6:skipped+=1;continue
  length=struct.unpack_from('!H',ip,2)[0]
  if length<ihl+20 or length>len(ip) or struct.unpack_from('!H',ip,6)[0]&0x3fff:skipped+=1;continue
  src=str(ipaddress.IPv4Address(ip[12:16]));dst=str(ipaddress.IPv4Address(ip[16:20]))
  tcp=ip[ihl:length];sport,dport,seq=struct.unpack_from('!HHI',tcp);thl=(tcp[12]>>4)*4
  if thl<20 or thl>len(tcp):skipped+=1;continue
  incoming=src==acer and dst==imac and dport==port
  outgoing=src==imac and dst==acer and sport==port
  if not (incoming or outgoing):skipped+=1;continue
  client=sport if incoming else dport;t=sec+frac/scale
  flow=flows.setdefault(client,{'client_port':client,'first':t,'last':t,'incoming_packets':0,'incoming_data_packets':0,'incoming_payload_bytes':0,'incoming_bad_tcp_checksums':0,'incoming_bad_ip_checksums':0,'repeated_seq_length':0,'zero_windows':0,'seen':set(),'data':[]})
  flow['last']=t
  if not incoming:continue # Sender-side checksum/TSO offload is not a corruption verdict.
  flow['incoming_packets']+=1
  if checksum(ip[:ihl]):flow['incoming_bad_ip_checksums']+=1
  if tcp[13]&2:flow['isn']=seq
  if struct.unpack_from('!H',tcp,14)[0]==0:flow['zero_windows']+=1
  payload=tcp[thl:]
  if not payload:continue
  flow['incoming_data_packets']+=1;flow['incoming_payload_bytes']+=len(payload)
  pseudo=ip[12:20]+b'\0\x06'+struct.pack('!H',len(tcp))
  if checksum(pseudo+tcp):flow['incoming_bad_tcp_checksums']+=1
  key=(seq,len(payload))
  if key in flow['seen']:flow['repeated_seq_length']+=1
  flow['seen'].add(key);flow['data'].append((seq,payload))
 result=[]
 for flow in flows.values():
  data=flow.pop('data');flow.pop('seen');isn=flow.pop('isn',None)
  flow['duration_seconds']=round(flow.pop('last')-flow.pop('first'),6)
  flow['mode']='unknown'
  # Only test the known generated upload pattern after a valid protocol request.
  request=next((p for s,p in data if isn is not None and s==(isn+1)&0xffffffff and b'\n' in p),None)
  if request:
   header,_,_=request.partition(b'\n')
   try:protocol=json.loads(header)
   except (ValueError,UnicodeError):protocol={}
   if protocol.get('protocol')=='RealtekVTDNext-LAN-1':
    flow['mode']=protocol.get('mode','unknown')
    count=protocol.get('bytes',0)
    if flow['mode']=='upload' and isinstance(count,int) and 0<count<=32*1024*1024:
     start=(isn+1+len(header)+1)&0xffffffff;bad_packets=bad_bytes=checked=0;intervals=[]
     for seq,payload in data:
      position=(seq-start)&0xffffffff
      if position>=count:continue
      payload=payload[:count-position];checked+=len(payload);intervals.append((position,position+len(payload)))
      differences=sum(value!=((position+i)&255) for i,value in enumerate(payload))
      bad_bytes+=differences;bad_packets+=bool(differences)
     unique=overlap_packets=0;end=0
     for begin,stop in sorted(intervals):
      if begin<end:overlap_packets+=1
      unique+=max(0,stop-max(begin,end));end=max(end,stop)
     flow.update(pattern_checked_bytes=checked,pattern_unique_bytes=unique,pattern_overlap_bytes=checked-unique,pattern_overlap_packets=overlap_packets,pattern_bad_packets=bad_packets,pattern_bad_bytes=bad_bytes)
  result.append(flow)
 return {'captured_packets':captured,'skipped_packets':skipped,'flows':result,'note':'Only Acer-to-iMac incoming packets assessed. Outgoing capture checksums excluded because offload may defer them. No payload content exported.'}
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('pcap');args=parser.parse_args()
 print(json.dumps(analyze(args.pcap),indent=2))
