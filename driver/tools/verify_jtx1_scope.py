#!/usr/bin/env python3
"""Exact EXP1 input/scope gate. No remote access or installed-device access."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path

BASE_SHA = '710aab6bbe5778d6b72cfd7c7f3af268d130a2fc91df95eb48a7a7002ee31b01'
PREFIX = 'RealtekVTDNext-0.20.5-VLJ1-EXP1-BAZA_COMPLETA/sursa/RealtekVTDNext-0.20.5-VLJ1-EXP1/'
ALLOWED = {'src/NetworkProgram.cpp', 'src/NetworkProgram.hpp', 'src/TXPool.hpp',
           'macos/MacOSBackend.cpp', 'macos/MacOSBackend.hpp', 'macos/RealtekVTDNextEthernet.cpp',
           'tools/package_probe.py', 'tools/verify_kext.py', 'tools/autoneg_acceptance.py',
           'tools/registry.py', 'tools/network_test.py', 'tests/autoneg_acceptance_tool.py',
           'tests/network_test_tool.py', 'TEST_LOCAL.command', 'LINEAGE.json',
           'README.md', 'HANDOVER.txt', 'NEXT_STEP.txt', 'EXECUTABLE_SHA256.txt'}
EXCLUDED = {'source-manifest.json', 'docs/JTX1_REVIEWED_INPUTS.json', 'EXECUTABLE_SHA256.txt'}
def sha(data): return hashlib.sha256(data).hexdigest()
def verify(root, baseline):
    assert sha(baseline.read_bytes()) == BASE_SHA, 'Wrong exact EXP1 archive'
    pins = json.loads((root / 'docs/JTX1_REVIEWED_INPUTS.json').read_text())
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()
              and p.relative_to(root).parts[0] not in {'build', '.git'}
              and '__pycache__' not in p.parts and p.suffix != '.pyc'
              and p.relative_to(root).as_posix() not in EXCLUDED}
    assert actual == set(pins['reviewed_files']), 'Unexpected/missing inputs'
    assert all(sha((root / n).read_bytes()) == h for n, h in pins['reviewed_files'].items()), 'Reviewed bytes changed'
    with zipfile.ZipFile(baseline) as z:
        assert z.testzip() is None
        manifest = json.loads(z.read(PREFIX + 'source-manifest.json')); assert len(manifest) == 165
        changed = []
        for rel, expected in manifest.items():
            before = z.read(PREFIX + rel); assert sha(before) == expected
            if before != (root / rel).read_bytes():
                assert rel in ALLOWED, 'Protected EXP1 input changed: ' + rel
                changed.append(rel)
        backend = (root / 'macos/MacOSBackend.cpp').read_text()
        assert backend.split('// Snapshot outside the output gate.')[0] == z.read(PREFIX + 'macos/MacOSBackend.cpp').decode().split('// Snapshot outside the output gate.')[0], 'Native DMA algorithm changed'
        native = (root / 'macos/RealtekVTDNextEthernet.cpp').read_text()
        old = z.read(PREFIX + 'macos/RealtekVTDNextEthernet.cpp').decode()
        for first, last in [('IOReturn RealtekVTDNextEthernet::setMaxPacketSize', 'IOReturn RealtekVTDNextEthernet::registerWithPolicyMaker'),
                            ('bool RealtekVTDNextEthernet::receive', 'bool RealtekVTDNextEthernet::filter')]:
            assert native.split(first)[1].split(last)[0] == old.split(first)[1].split(last)[0], 'MTU/RX behavior changed'
    trace = (root / 'src/JTXTrace.hpp').read_text()
    for forbidden in ['new ', 'malloc(', 'write8(', 'write16(', 'write32(', 'phyRead(', 'syncForDevice(', 'delayUs(', 'IOLog(']:
        assert forbidden not in trace, 'Intrusive trace operation: ' + forbidden
    assert 'Capacity=64' in trace and 'RecordBytes=320' in trace
    assert 'JTX1FixImplemented",false' in native and 'RXSTAT1Status","DEFERRED' in native
    return {'status': 'PASS', 'baseline_sha256': BASE_SHA, 'baseline_files': 165,
            'changed_existing_files': changed, 'reviewed_files': len(actual),
            'native_DMA_algorithm': 'UNCHANGED', 'MTU_PAUSE_RX_behavior': 'UNCHANGED',
            'diagnostic_only': True, 'fix_implemented': False, 'hardware_validated': False}
if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--baseline-zip', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True); a = p.parse_args()
    result = verify(Path(__file__).resolve().parents[1], a.baseline_zip)
    a.report.parent.mkdir(parents=True, exist_ok=True); a.report.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS JTX1 exact EXP1 inputs/scope; native DMA/MTU/PAUSE/RX unchanged; diagnostic only')
