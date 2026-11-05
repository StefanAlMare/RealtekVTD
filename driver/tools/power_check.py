#!/usr/bin/env python3
"""Record and compare one manual sleep/wake cycle; no power or network writes."""
import json,pathlib,sys,time
from registry import read_registry
root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/local-evidence';out.mkdir(parents=True,exist_ok=True)
phase=sys.argv[1] if len(sys.argv)==2 else '';result={'phase':phase,'timestamp':time.strftime('%Y-%m-%d %H:%M:%S'),'status':'NOT PASS'}
try:
 if phase not in ['before','after']:raise RuntimeError('Use before or after')
 iface,driver=read_registry();result.update(interface=iface,properties={key:value for key,value in driver.items() if key.startswith('Stage4A') or key in ['Stage3BEnabled','Stage3BTXWatchdog','Stage3BUnsafeMappingRetained','Stage3ETXPoolLeased','Stage3GMessageInterruptSelected','Stage3GInterruptIndex']})
 if not driver.get('Stage4APowerRegistered'):raise RuntimeError('Power management not registered')
 if driver.get('Stage4ASuspended') or not driver.get('Stage4ARequestedEnabled'):raise RuntimeError('Expected awake, administratively enabled Ethernet')
 if phase=='after':
  before=json.loads((out/'power-before.json').read_text())
  if before.get('status')!='PASS' or before.get('interface')!=iface:raise RuntimeError('Missing valid before-sleep baseline')
  for key in ['Stage4ASleepCount','Stage4AWakeCount']:
   delta=driver.get(key,0)-before['properties'].get(key,0);result[key+'Delta']=delta
   if delta<1:raise RuntimeError('No completed PM cycle: '+key)
  if driver.get('Stage4ALastStatus') or driver.get('Stage4AFailures',0)>before['properties'].get('Stage4AFailures',0):raise RuntimeError('Power transition failure reported')
  if driver.get('Stage3BTXWatchdog') or driver.get('Stage3BUnsafeMappingRetained'):raise RuntimeError('Watchdog or unsafe DMA retention')
 result['status']='PASS'
except Exception as exc:result['error']=str(exc)
(out/('power-'+(phase if phase in ['before','after'] else 'invalid')+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
sys.exit(0 if result['status']=='PASS' else 1)
