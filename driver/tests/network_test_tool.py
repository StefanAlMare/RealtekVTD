#!/usr/bin/env python3
"""Exercise the user-facing hardware test without issuing any real network commands."""
import contextlib,io,pathlib,plistlib,runpy,subprocess,sys,unittest
from unittest.mock import patch
script=pathlib.Path(__file__).resolve().parents[1]/'tools/network_test.py'
sys.path.insert(0,str(script.parent))
class TestNetworkTool(unittest.TestCase):
 def run_case(self,mode):
  counter=[0]
  def fake_run(cmd,**kwargs):
   if cmd[0].endswith('ioreg'):
    counter[0]+=1
    node={'DriverVersion':'0.20.5-JTX1-DIAG','Stage3BEnabled':True,'Stage3BTXCompleted':10 if counter[0]>1 else 0,
     'Stage3BRXDelivered':10 if counter[0]>1 else 0,'Stage3BInterruptActions':20,'Stage3BUnsafeMappingRetained':False,
     'IORegistryEntryChildren':[{'BSD Name':'en7'}]}
    if mode=='wrong_driver':node['DriverVersion']='old'
    if mode=='legacy_key':node['IORegistryEntryChildren']=[{'IOBSDName':'en7'}]
    if mode=='no_rx':node['Stage3BRXDelivered']=0
    if mode=='stopped':node['Stage3BEnabled']=False
    text='+-o RealtekVTDNextEthernet <class RealtekVTDNextEthernet, id 0x1>\n'
    for key,value in node.items():
     if key=='IORegistryEntryChildren':
      for child in value:
       for name,entry in child.items():text+=f'  "{name}" = "{entry}"\n'
     elif isinstance(value,bool):text+=f'  "{key}" = {"Yes" if value else "No"}\n'
     elif isinstance(value,str):text+=f'  "{key}" = "{value}"\n'
     else:text+=f'  "{key}" = {value}\n'
    return subprocess.CompletedProcess(cmd,0,text,'')
   if cmd[0].endswith('ifconfig'):return subprocess.CompletedProcess(cmd,0,'status: active','')
   if cmd[0].endswith('ipconfig'):
    result={'getifaddr':'192.168.10.22','getpacket':'yiaddr = 192.168.10.22\ndhcp_message_type (uint8): ACK','getoption':'192.168.10.1'}[cmd[1]]
    if mode=='static' and cmd[1]=='getpacket':return subprocess.CompletedProcess(cmd,1,'','no DHCP lease')
    if mode=='linklocal' and cmd[1]=='getifaddr':result='169.254.1.2'
    return subprocess.CompletedProcess(cmd,0,result,'')
   if cmd[0].endswith('ping'):
    assert '-b' in cmd and cmd[cmd.index('-b')+1]=='en7'
    assert '-S' in cmd and cmd[cmd.index('-S')+1]=='192.168.10.22'
    return subprocess.CompletedProcess(cmd,0,'10 packets transmitted, 10 packets received, 0.0% packet loss','')
   raise AssertionError('Unexpected command: '+str(cmd))
  with patch.object(sys,'argv',[str(script)]),patch('subprocess.run',side_effect=fake_run),patch('time.sleep'),patch('pathlib.Path.write_text'),contextlib.redirect_stdout(io.StringIO()):
   try:runpy.run_path(str(script),run_name='__main__')
   except SystemExit as exc:return exc.code
  raise AssertionError('Test tool did not exit')
 def test_pass(self):self.assertEqual(self.run_case('pass'),0)
 def test_legacy_key(self):self.assertEqual(self.run_case('legacy_key'),0)
 def test_reject_unproven(self):
  for mode in ['wrong_driver','no_rx','stopped','static','linklocal']:
   with self.subTest(mode=mode):self.assertEqual(self.run_case(mode),1)
if __name__=='__main__':unittest.main()
