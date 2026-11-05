#!/usr/bin/env python3
import importlib.util,pathlib,socket,threading,unittest,sys
script=pathlib.Path(__file__).resolve().parents[1]/'tools/lan_throughput.py'
sys.path.insert(0,str(script.parent))
spec=importlib.util.spec_from_file_location('lan',script);lan=importlib.util.module_from_spec(spec);spec.loader.exec_module(lan)
class ProtocolTest(unittest.TestCase):
 def test_both_directions_with_partial_last_block(self):
  for direction in ['upload','download']:
   with self.subTest(direction=direction):
    client,server=socket.socketpair();results=[];errors=[]
    def receiver():
     try:results.append(lan.handle(server))
     except Exception as exc:errors.append(exc)
     finally:server.close()
    worker=threading.Thread(target=receiver);worker.start()
    with client:
     size=2*len(lan.BLOCK)+17
     lan.reply(client,{'protocol':'RealtekVTDNext-LAN-1','mode':direction,'bytes':size});self.assertTrue(lan.line(client)['ready'])
     local=lan.transfer(client,size,direction=='upload');remote=lan.line(client)
     self.assertEqual(local['sha256'],remote['sha256']);self.assertEqual(local['bytes'],size)
    worker.join(2);self.assertFalse(worker.is_alive());self.assertFalse(errors);self.assertEqual(results[0][0],direction)
 def test_truncated_stream_is_rejected(self):
  left,right=socket.socketpair()
  with left,right:
   left.sendall(b'abc');left.shutdown(socket.SHUT_WR)
   with self.assertRaises(RuntimeError):lan.transfer(right,64,False)
 def test_wrong_protocol_is_rejected(self):
  left,right=socket.socketpair()
  with left,right:
   lan.reply(left,{'protocol':'wrong','mode':'upload','bytes':4})
   with self.assertRaises(RuntimeError):lan.handle(right)
if __name__=='__main__':unittest.main()
