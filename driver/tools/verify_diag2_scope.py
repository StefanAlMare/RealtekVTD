#!/usr/bin/env python3
"""Verify the exact DIAG1 parent and an observation-only diagnostic patch."""
import argparse, hashlib, json, zipfile
from pathlib import Path

ARCHIVE_SHA='bec10fe6910a566aba7078fb9b40aa861ad8f15701642fb1ca5ef8b9d5accec6'
BINARY_SHA='7cadf13fc08dca64dfe38c82315d398b3dd486b1391d4955f49f79e27b25ca9b'
CODE_PINS = {'macos/RealtekVTDNextEthernet.cpp': '031ce52ba91a7dfdcd6cc6f17858141081814f8dfc529c3b4c5c72faecf0a07d', 'src/RXHandoffDiagnostics.hpp': 'a353a9a1d19ffa1ee01e5e2f376b5ed57b92b6c786e255e60f7e77c75052ecd0', 'tests/rx_handoff_diagnostics.cpp': 'e102fce72e61ee35cb9875bc81dd5363eeea7b8b5d2fa7999f27f26246a61009'}
METADATA={'README.md','HANDOVER.txt','NEXT_STEP.txt','LINEAGE.json','EXECUTABLE_SHA256.txt'}
ADDITIONS={'src/RXHandoffDiagnostics.hpp','tests/rx_handoff_diagnostics.cpp',
 'tools/verify_diag2_scope.py','BUILD_DIAG2_LOCAL.command'}
def sha(data):return hashlib.sha256(data).hexdigest()
def verify(root,archive):
 assert sha(archive.read_bytes())==ARCHIVE_SHA,'Wrong DIAG1 baseline archive'
 with zipfile.ZipFile(archive) as z:
  parent='RealtekVTDNext-0.20.4-RXMC1-DIAG1-BAZA/'
  prefix=parent+'sursa/RealtekVTDNext-0.20.4-RXMC1-DIAG1/'
  manifest=json.loads(z.read(prefix+'source-manifest.json'));assert len(manifest)==151
  before={p:z.read(prefix+p) for p in manifest}
  assert all(sha(before[p])==h for p,h in manifest.items()),'Corrupt DIAG1 source'
  assert sha(z.read(parent+'kext/RealtekVTDNextEthernet.kext/Contents/MacOS/RealtekVTDNextEthernet'))==BINARY_SHA
 assert CODE_PINS,'Missing reviewed code hashes'
 for p,h in CODE_PINS.items():assert sha((root/p).read_bytes())==h,'Different reviewed diagnostic code: '+p
 changes=[];unchanged=0
 for p,b in before.items():
  a=(root/p).read_bytes()
  if a==b:
   unchanged+=p.startswith(('src/','macos/','firmware/'));continue
  if p in {'tools/verify_kext.py', 'tools/registry.py', 'tools/autoneg_acceptance.py', 'tools/network_test.py', 'tools/package_probe.py', 'tests/network_test_tool.py', 'tests/autoneg_acceptance_tool.py'}:
   assert a==b.replace(b'0.20.4-RXMC1-DIAG1',b'0.20.4-RXMC1-DIAG2'),'Changed packaging beyond build identity'
  elif p=='TEST_LOCAL.command':
   extra='\nxcrun clang++ -std=c++14 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/rx_handoff_diagnostics.cpp -o build/tests/rx_handoff_diagnostics\nbuild/tests/rx_handoff_diagnostics\n'
   assert a==b+extra.encode(),'Inherited tests changed'
  elif p=='BUILD_DRIVER.command':
   old=b.decode();start=old.index('if [ "${1:-}" = --fetch-sdk ]; then');end=old.index('\n[ -f',start)
   expected=old[:start]+'''[ "$#" -eq 0 ] || { echo 'Network fetch is disabled; use only a verified local SDK.'; exit 2; }'''+old[end:]
   expected=expected.replace(', or run with --fetch-sdk.',' (local verified dependency required).')
   assert a.decode()==expected,'Compiler or linker flags changed'
  elif p=='BUILD_DIAG1_LOCAL.command':
   assert a.decode()=='#!/bin/bash\necho "Use BUILD_DIAG2_LOCAL.command with the exact DIAG1 archive." >&2\nexit 2\n'
  else:assert p=='macos/RealtekVTDNextEthernet.cpp' or p in METADATA,'Changed protected source: '+p
  changes.append({'path':p,'before_sha256':sha(b),'after_sha256':sha(a)})
 actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and not p.relative_to(root).parts[0]=='build' and p.name!='source-manifest.json'}
 assert actual-set(manifest)==ADDITIONS,'Unexpected source additions'
 native=(root/'macos/RealtekVTDNextEthernet.cpp').read_text()
 for token in ['ifnet_stat_increment','ifnet_set_stat','enqueueInputPacket(']:assert token not in native,'Unapproved RX accounting or API replacement'
 assert native.count('->inputPacket(')==1 and native.count('->flushInputQueue(')==1
 assert '"RXMC1RXFixImplemented",false' in native
 lineage=json.loads((root/'LINEAGE.json').read_text())
 assert lineage['baseline_archive_sha256']==ARCHIVE_SHA and lineage['rx_fix_implemented'] is False
 return {'result':'PASS','candidate':'0.20.4-RXMC1-DIAG2','baseline_archive_sha256':ARCHIVE_SHA,
  'baseline_executable_sha256':BINARY_SHA,'baseline_source_files_verified':151,
  'unchanged_hardware_files':unchanged,'changes':changes,'rx_fix_implemented':False,
  'vlan_implemented':False,'jumbo_implemented':False,'diagnostic_only':True,
  'compiler_flags_unchanged':True,'dma_irq_hardware_pm_flow_multicast_source_unchanged':True}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--baseline-zip',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
 a=p.parse_args();r=verify(Path(__file__).resolve().parents[1],a.baseline_zip)
 a.report.write_text(json.dumps(r,indent=2)+'\n');print('PASS exact DIAG1 baseline; bounded RX handoff observations only; RX fix/VLAN/Jumbo=false')
