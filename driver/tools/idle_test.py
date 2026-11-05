#!/usr/bin/env python3
"""Read IRQ/traffic counters over a quiet 10-second interval; send no traffic."""
import json,pathlib,time
from registry import read_registry
root=pathlib.Path(__file__).resolve().parents[1]
out=root/'build/local-evidence';out.mkdir(parents=True,exist_ok=True)
messages=[]
def log(value):print(value,flush=True);messages.append(value)
try:
 interface,before=read_registry()
 required=['Stage3FIRQFilters','Stage3FIRQActions','Stage3FIRQPasses']
 if any(key not in before for key in required):raise RuntimeError('IRQ audit counters missing')
 log('Keep Ethernet enabled; do not start transfers during this 10-second observation.')
 start=time.monotonic();time.sleep(10);interface2,after=read_registry();elapsed=time.monotonic()-start
 if interface2!=interface:raise RuntimeError('Interface changed during observation')
 keys=[key for key in before if key.startswith('Stage3FIRQ') or key in ['Stage3BTXSubmitted','Stage3BTXCompleted','Stage3BRXDelivered','Stage3BRXDropped','Stage3CTXDropped']]
 delta={key:after.get(key,0)-before[key] for key in keys}
 if any(value<0 for value in delta.values()):raise RuntimeError('Counters restarted during observation')
 log(json.dumps({'interface':interface,'elapsed_seconds':round(elapsed,3),'delta':delta,'irq_actions_per_second':round(delta['Stage3FIRQActions']/elapsed,2)},sort_keys=True))
 log('OBSERVATION COMPLETE. Background traffic is reported; this is not a zero-traffic guarantee.')
except Exception as exc:
 log('OBSERVATION FAILED: '+str(exc));raise SystemExit(1)
finally:(out/'idle-test-latest.txt').write_text('\n'.join(messages)+'\n')
