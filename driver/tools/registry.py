#!/usr/bin/env python3
"""Read the controller subtree using the text format proven by the Acer evidence."""
import re,subprocess

def parse_registry(text):
 controllers=re.findall(r'<class RealtekVTDNextEthernet,',text)
 if len(controllers)!=1:raise RuntimeError(f'Expected one RealtekVTDNextEthernet controller; found {len(controllers)}')
 versions=re.findall(r'"DriverVersion"\s*=\s*"([^"\n]+)"',text)
 if versions!=['0.20.5-JTX1-DIAG']:raise RuntimeError('Expected running driver 0.20.5-JTX1-DIAG')
 names=set(re.findall(r'"(?:BSD Name|IOBSDName)"\s*=\s*"(en\d+)"',text))
 if len(names)!=1:raise RuntimeError('Cannot identify the interface in this controller subtree: '+repr(names))
 name=names.pop();driver={'DriverVersion':versions[0],'IORegistryEntryChildren':[{'BSD Name':name}]}
 for key,value in re.findall(r'"((?:Stage3[BCDEFG]|Stage4[ABD])[^" ]+)"\s*=\s*(Yes|No|[0-9]+)',text):
  driver[key]=(value=='Yes') if value in ['Yes','No'] else int(value)
 if not driver.get('Stage3BEnabled'):raise RuntimeError('Driver is inactive; collect evidence with COLLECT_LOCAL.command before changing Ethernet state')
 return name,driver

def read_registry():
 result=subprocess.run(['/usr/sbin/ioreg','-lw0','-r','-c','RealtekVTDNextEthernet'],capture_output=True,text=True)
 if result.returncode:raise RuntimeError('Cannot read driver registry: '+result.stderr.strip())
 return parse_registry(result.stdout)
