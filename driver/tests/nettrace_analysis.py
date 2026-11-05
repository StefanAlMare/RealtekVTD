#!/usr/bin/env python3
import importlib.util,json,pathlib,socket,struct,tempfile,unittest
p=pathlib.Path(__file__).resolve().parents[1]/'tools/analyze_nettrace.py'
spec=importlib.util.spec_from_file_location('analysis',p);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
def frame(seq,data,flags=0x10,outgoing=False,damage=False):
 src,dst=('192.168.10.10','192.168.10.105') if outgoing else ('192.168.10.105','192.168.10.10')
 addresses=socket.inet_aton(src)+socket.inet_aton(dst)
 sport,dport=(49072,50139) if outgoing else (50139,49072)
 tcp=struct.pack('!HHIIBBHHH',sport,dport,seq,0,0x50,flags,65535,0,0)+data
 csum=mod.checksum(addresses+b'\0\x06'+struct.pack('!H',len(tcp))+tcp)
 tcp=tcp[:16]+struct.pack('!H',csum)+tcp[18:]
 if damage:tcp=tcp[:-1]+bytes([tcp[-1]^1])
 ip=struct.pack('!BBHHHBBH',0x45,0,20+len(tcp),0,0,64,6,0)+addresses
 ip=ip[:10]+struct.pack('!H',mod.checksum(ip))+ip[12:]
 return b'\0'*12+b'\x08\x00'+ip+tcp
class TestAnalysis(unittest.TestCase):
 def test_corruption_retransmission_and_offload_exclusion(self):
  request=json.dumps({'protocol':'RealtekVTDNext-LAN-1','mode':'upload','bytes':256}).encode()+b'\n'
  seq=1001+len(request);pattern=bytes(range(256))
  frames=[frame(1000,b'',2),frame(1001,request),frame(seq,pattern,damage=True),frame(seq,pattern),frame(99,b'x',outgoing=True,damage=True)]
  raw=b'\xd4\xc3\xb2\xa1'+struct.pack('<HHIIII',2,4,0,0,65535,1)
  for i,f in enumerate(frames):raw+=struct.pack('<IIII',10,i*1000,len(f),len(f))+f
  with tempfile.TemporaryDirectory() as folder:
   path=pathlib.Path(folder)/'test.pcap';path.write_bytes(raw);result=mod.analyze(path)
  f=result['flows'][0]
  self.assertEqual(result['captured_packets'],5)
  self.assertEqual(f['incoming_bad_tcp_checksums'],1)
  self.assertEqual(f['incoming_bad_ip_checksums'],0)
  self.assertEqual(f['pattern_bad_packets'],1);self.assertEqual(f['pattern_bad_bytes'],1)
  self.assertEqual(f['repeated_seq_length'],1)
  self.assertEqual(f['pattern_checked_bytes'],512)
  self.assertEqual(f['pattern_unique_bytes'],256);self.assertEqual(f['pattern_overlap_bytes'],256)
  self.assertNotIn('payload',f);self.assertNotIn('data',f)
 def test_truncation_rejected(self):
  with tempfile.TemporaryDirectory() as folder:
   path=pathlib.Path(folder)/'test.pcap';path.write_bytes(b'\xd4\xc3\xb2\xa1')
   with self.assertRaises(ValueError):mod.analyze(path)
if __name__=='__main__':unittest.main()
