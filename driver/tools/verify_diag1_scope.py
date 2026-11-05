#!/usr/bin/env python3
"""Fail closed on a different baseline, hardware source, or diagnostic patch."""
import argparse, hashlib, importlib.util, json
from pathlib import Path
import zipfile

ARCHIVE_SHA='3ee246d1a6625cb31fed639568d10ab81e2aa9d0bd6d3599f0f00533f6405076'
BINARY_SHA='232823ed3a1c8169b08e350ee3f66373c6199d92b435a76d35176690cf930959'
NATIVE='macos/RealtekVTDNextEthernet.cpp'
CODE_PINS={NATIVE:'27bc4e372dc3996ac34822b2664453820ffd3da4d72bc1d038d287b489481adf',
 'src/MulticastFilter.hpp':'91cb9170f82e8abb8069efea9311a7356552b5974b262cc29915b0538c3e2228',
 'tests/multicast_filter.cpp':'b999c9c52ed246d01441fa4687a4d4861655c87226368a7701e05f76aaef460f'}
VERSION_ONLY={'macos/KextEntry.cpp','tools/verify_kext.py','tools/network_test.py','tools/autoneg_acceptance.py','tools/registry.py','tests/autoneg_acceptance_tool.py','tests/network_test_tool.py','TEST_BOOT.command','TEST_ASUS.command'}
METADATA={'README.md','HANDOVER.txt','NEXT_STEP.txt','LINEAGE.json','EXECUTABLE_SHA256.txt'}
def sha(b): return hashlib.sha256(b).hexdigest()
def version(t): return t.replace('0.20.3-MEDIA1','0.20.4-RXMC1-DIAG1').replace('0.20.3','0.20.4')
def verify(root,archive):
 assert sha(archive.read_bytes())==ARCHIVE_SHA, 'Wrong baseline ZIP'
 with zipfile.ZipFile(archive) as z:
  prefix='RealtekVTDNext-0.20.3-MEDIA1/'
  manifest=json.loads(z.read(prefix+'source-manifest.json')); assert len(manifest)==147
  old={p:z.read(prefix+p) for p in manifest}
  assert all(sha(old[p])==h for p,h in manifest.items()), 'Corrupt baseline source manifest'
  assert sha(z.read(prefix+'build/Release/RealtekVTDNextEthernet.kext/Contents/MacOS/RealtekVTDNextEthernet'))==BINARY_SHA
 for p,h in CODE_PINS.items(): assert sha((root/p).read_bytes())==h, 'Different reviewed code: '+p
 changed=[]
 for p,b in old.items():
  a=(root/p).read_bytes()
  if a==b: continue
  if p in VERSION_ONLY:
   expected=version(b.decode())
   if p=='tools/verify_kext.py':
    expected=expected.replace("assert info['CFBundleVersion']=='0.20.4'", "assert info['CFBundleVersion']=='0.20.4'\nassert info['RealtekVTDNextBuild']=='0.20.4-RXMC1-DIAG1'\nassert info['RealtekVTDNextRXFixImplemented'] is False")
   assert a.decode()==expected, 'Beyond version-only or diagnostic identity assertions: '+p
  elif p=='tools/package_probe.py':
   expected=version(b.decode()).replace("'CFBundleShortVersionString':'0.20.4'","'CFBundleShortVersionString':'0.20.4','RealtekVTDNextBuild':'0.20.4-RXMC1-DIAG1','RealtekVTDNextRXFixImplemented':False")
   assert a.decode()==expected, 'Changed PCI/firmware/library packaging'
  elif p=='TEST_LOCAL.command':
   extra='\nxcrun clang++ -std=c++14 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/multicast_filter.cpp -o build/tests/multicast_filter\nbuild/tests/multicast_filter\n'
   assert a.decode()==b.decode()+extra, 'Changed inherited tests'
  else: assert p==NATIVE or p in METADATA, 'Changed protected source: '+p
  changed.append({'path':p,'before_sha256':sha(b),'after_sha256':sha(a)})
 for directory in ['src','macos','firmware','tests']:
  actual={p.relative_to(root).as_posix() for p in (root/directory).rglob('*') if p.is_file()}
  assert actual-set(manifest)<=set(CODE_PINS)-{NATIVE}, 'Unexpected source addition'
 spec=importlib.util.spec_from_file_location('scope03',root/'tools/verify_media1_scope.py')
 scope=importlib.util.module_from_spec(spec); spec.loader.exec_module(scope)
 native=(root/NATIVE).read_text(); original=old[NATIVE].decode()
 for name in ['receive','interrupt','tick','outputPacket','configureInterface','updateLink','createMedia','selectMedium','applyPower','stop','free']:
  assert scope.method(native,name)==scope.method(original,name), 'Changed protected method: '+name
 observer=scope.method(native,'publishRXStatistics')
 for forbidden in ['ifnet_stat_increment','ifnet_set_stat','inputPacket(','enqueueInputPacket','flushInputQueue','->write','phyWrite','mdioWrite','startNetwork','stopNetwork','serviceNetwork','freePacket','allocatePacket']:
  assert forbidden not in observer, 'Mutation in statistics observer'
 assert '"RXMC1RXFixImplemented",false' in observer
 lineage=json.loads((root/'LINEAGE.json').read_text())
 assert lineage['rx_fix_implemented'] is False and lineage['baseline_archive_sha256']==ARCHIVE_SHA
 return {'result':'PASS','candidate':'0.20.4-RXMC1-DIAG1','baseline_source_files_verified':147,
  'baseline_archive_sha256':ARCHIVE_SHA,'baseline_executable_sha256':BINARY_SHA,
  'unchanged_hardware_files':sum(1 for path,before in old.items() if path.startswith(('src/','macos/','firmware/')) and (root/path).read_bytes()==before),'rx_delivery_dma_tx_irq_pm_watchdog_flow_autoneg_unchanged':True,
  'rx_statistics_observer_read_only':True,'rx_fix_implemented':False,'final_rxmc1_acceptance':'BLOCKED pending RX cause and target hardware results',
  'diagnostic_build_allowed':True,'changes':changed,'new_hardware_source':['src/MulticastFilter.hpp']}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--baseline-zip',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
 a=p.parse_args();r=verify(Path(__file__).resolve().parents[1],a.baseline_zip);a.report.write_text(json.dumps(r,indent=2)+'\n')
 print('PASS DIAG1: exact MEDIA1 baseline; reviewed multicast and read-only counters; RX/TX/DMA/IRQ/PM/Flow Control unchanged. RX fix=false.')
