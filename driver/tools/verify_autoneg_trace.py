#!/usr/bin/env python3
"""Compare full successful probe configuration writes against immutable 0.20.1."""
import argparse,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--baseline',type=pathlib.Path,required=True);args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];work=root/'build/write-audit';work.mkdir(parents=True,exist_ok=True)
source=root/'tests/probe_write_trace.cpp';traces=[]
for n,base in enumerate([args.baseline.resolve(),root]):
 core=['RegisterAccess.cpp','FirmwareProgram.cpp','PhyInit.cpp','HardwareInit.cpp','ProbeProgram.cpp']
 if (base/'src/CopperAutoneg.cpp').exists():core+=['CopperAutoneg.cpp']
 exe=work/f'probe-{n}'
 subprocess.run(['clang++','-std=c++14','-I',str(base),str(source),*[str(base/'src'/f) for f in core],'-o',str(exe)],check=True)
 traces.append(subprocess.check_output([str(exe)],text=True).splitlines())
old,new=traces;filtered=[];extras={};chip=None
for line in new:
 if line.startswith('chip '):chip=line;extras[chip]=[]
 if line in ['phy a408 de1','phy a412 300','phy a400 1240']:
  extras[chip].append(line)
 else:filtered.append(line)
assert filtered==old,'Existing chip-specific configuration trace changed'
assert len(extras)==3 and all(v==['phy a408 de1','phy a412 300','phy a400 1240'] for v in extras.values()),extras
report='PASS: complete successful probe traces identical to 0.20.1 for RTL8411B/RTL8168H/RTL8168M except exactly ANAR, CTRL1000 and BMCR restart writes.\n'
(work/'result.txt').write_text(report);print(report,end='')
