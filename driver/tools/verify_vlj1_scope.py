#!/usr/bin/env python3
"""Fail-closed local input/scope gate; no network or installed-device access."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path

BASE_SHA = '7efd512a9b6f5c0895427f4e14bd8f7dcd43548cafe6ed806da98d35d8187834'
BASE_BINARY = 'cc213cff0003605649acda06f418c324fc89bdb395377e46f58f6232e991bfa2'
PREFIX = 'RealtekVTDNext-0.20.5-PREP-DIAG2-BAZA/sursa/RealtekVTDNext-0.20.4-RXMC1-DIAG2/'
EXCLUDED = {'source-manifest.json', 'docs/VLJ1_REVIEWED_INPUTS.json', 'EXECUTABLE_SHA256.txt'}
ALLOWED_CHANGES = {'EXECUTABLE_SHA256.txt', 'tools/verify_kext.py', 'tests/network.cpp', 'README.md', 'tests/txsnapshot.cpp', 'macos/RealtekVTDNextEthernet.cpp', 'macos/MacOSBackend.cpp', 'src/TXPool.hpp', 'src/TXSnapshot.cpp', 'src/TXPool.cpp', 'src/NetworkProgram.cpp', 'tests/network_test_tool.py', 'tools/autoneg_acceptance.py', 'tools/registry.py', 'HANDOVER.txt', 'TEST_LOCAL.command', 'src/NetworkProgram.hpp', 'NEXT_STEP.txt', 'tools/package_probe.py', 'tests/autoneg_acceptance_tool.py', 'tools/network_test.py', 'macos/MacOSBackend.hpp', 'LINEAGE.json'}


def sha(data):
    return hashlib.sha256(data).hexdigest()

def verify(root, baseline):
    assert sha(baseline.read_bytes()) == BASE_SHA, 'Wrong exact DIAG2 archive'
    pins = json.loads((root / 'docs/VLJ1_REVIEWED_INPUTS.json').read_text())
    assert set(pins['allowed_changed_files']) == ALLOWED_CHANGES, 'Protected scope allowlist changed'
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()
              and p.relative_to(root).parts[0] not in {'build', '.git'}
              and '__pycache__' not in p.parts and p.suffix != '.pyc'
              and p.relative_to(root).as_posix() not in EXCLUDED}
    assert actual == set(pins['reviewed_files']), 'Unexpected/missing source inputs'
    for rel, expected in pins['reviewed_files'].items():
        assert sha((root / rel).read_bytes()) == expected, 'Unreviewed bytes: ' + rel
    with zipfile.ZipFile(baseline) as z:
        assert z.testzip() is None, 'Baseline CRC failure'
        manifest = json.loads(z.read(PREFIX + 'source-manifest.json'))
        assert len(manifest) == 155
        assert sha(z.read(PREFIX + 'build/Release/RealtekVTDNextEthernet.kext/Contents/MacOS/RealtekVTDNextEthernet')) == BASE_BINARY
        changed = []
        for rel, expected in manifest.items():
            before = z.read(PREFIX + rel)
            assert sha(before) == expected, 'Invalid baseline manifest'
            after = (root / rel).read_bytes()
            if before != after:
                changed.append(rel)
                assert rel in pins['allowed_changed_files'], 'Protected input changed: ' + rel
        assert sorted(changed) == sorted(pins['allowed_changed_files']), 'Scope list differs'
        # Preserve the native DMA implementation verbatim except its allocation ceiling.
        original = z.read(PREFIX + 'macos/MacOSBackend.cpp').decode()
        expected = original.replace('uint64_t(realtek_next::kTXPoolCount)*realtek_next::kTXPoolStride',
                                    'uint64_t(realtek_next::kTXPoolCount)*realtek_next::kJumboTXStride')
        assert (root / 'macos/MacOSBackend.cpp').read_text() == expected, 'DMA algorithm changed'
    native = (root / 'macos/RealtekVTDNextEthernet.cpp').read_text()
    assert 'getFeatures() const override{return kIONetworkFeatureSoftwareVlan;}' in native
    for forbidden in ['kIONetworkFeatureHardwareVlan', 'ifnet_stat_increment', 'ifnet_set_stat', 'getVlanTagDemand(', 'setVlanTag(']:
        assert forbidden not in native, 'Unapproved offload/accounting: ' + forbidden
    assert native.count('->inputPacket(') == 1 and native.count('->flushInputQueue(') == 1
    setter = native.split('IOReturn RealtekVTDNextEthernet::setMaxPacketSize(UInt32 size)')[1].split('IOReturn RealtekVTDNextEthernet::registerWithPolicyMaker')[0]
    before_join = setter.split('queue->stop()')[0]
    assert before_join.rfind('IOLockUnlock(lock)') > before_join.rfind('IOLockLock(lock)'), 'Queue join under controller lock'
    output = native.split('UInt32 RealtekVTDNextEthernet::outputPacket')[1].split('bool RealtekVTDNextEthernet::receive')[0]
    assert 'queue->stop' not in output and 'runAction' not in output and 'closeGate' not in output
    assert 'generation==epoch' in output and output.index('IOLockUnlock(lock)') < output.index('mapPacket')
    assert '"RXMC1RXFixImplemented",false' in native and '"RXSTAT1Status","DEFERRED"' in native
    core = (root / 'src/NetworkProgram.cpp').read_text()
    assert 'cplus&~0x60U' in core and 'n.tx[4*i+1]=0' in core and 'wireLength-4' in core
    assert core.index('d.setBusMaster(false)') < core.index('cleanup(d,n.txPackets[i])', core.index('bool stopNetwork'))
    assert 'SAFE_STOP' in (root / 'src/MTUTransaction.hpp').read_text()
    return {'status': 'PASS', 'build': '0.20.5-VLJ1-EXP1', 'baseline_sha256': BASE_SHA,
            'baseline_files_verified': 155, 'changed_baseline_files': changed,
            'reviewed_files': len(actual), 'dma_algorithm_unchanged': True,
            'ring_geometry_unchanged': True, 'irq_mapper_firmware_eee_multicast_unchanged': True,
            'queue_stop_outside_controller_lock': True, 'rxstat1': 'DEFERRED', 'hardware_tests': 'NOT TESTED'}

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--baseline-zip', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    result = verify(Path(__file__).resolve().parents[1], a.baseline_zip)
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS VLJ1 scope: exact DIAG2, reviewed inputs, unchanged native DMA/IRQ/firmware, queue-lock order, RX deferred')
