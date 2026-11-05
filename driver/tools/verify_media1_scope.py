#!/usr/bin/env python3
"""Fail closed on a different baseline or changes outside MEDIA1 reporting."""
import argparse, hashlib, json, pathlib, re, zipfile

ARCHIVE_SHA = '9973badc7d3fd36bd32538e6fc7d750bc10af5f87b75a4d9d9202471be419393'
BINARY_SHA = 'ffed1ffff65c5012ee13e37d4e9c9025fa948923a72ab4ece581535915eed847'

def sha(data): return hashlib.sha256(data).hexdigest()

def method(source, name):
    match = re.search(r'^.*RealtekVTDNextEthernet::' + name + r'\([^\n]*', source, re.M)
    assert match, name
    start = match.start(); first = source.index('{', match.start()); depth = 0
    for end in range(first, len(source)):
        if source[end] == '{': depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0: return source[start:end+1]
    raise AssertionError('Unclosed method: ' + name)

def verify(root, archive):
    assert sha(archive.read_bytes()) == ARCHIVE_SHA, 'Wrong baseline archive'
    with zipfile.ZipFile(archive) as z:
        prefix = 'RealtekVTDNext-0.20.2-AUTONEG/'
        manifest = json.loads(z.read(prefix+'source-manifest.json'))
        assert len(manifest) == 137
        old = {p:z.read(prefix+p) for p in manifest}
        assert all(sha(old[p]) == h for p,h in manifest.items()), 'Baseline manifest mismatch'
        assert sha(z.read(prefix+'build/Release/RealtekVTDNextEthernet.kext/Contents/MacOS/RealtekVTDNextEthernet')) == BINARY_SHA
    metadata = {'README.md','HANDOVER.txt','NEXT_STEP.txt','LINEAGE.json','EXECUTABLE_SHA256.txt'}
    version_only = {'macos/KextEntry.cpp','tools/package_probe.py','tools/verify_kext.py',
        'tools/network_test.py','tools/autoneg_acceptance.py','tools/registry.py',
        'tests/autoneg_acceptance_tool.py','tests/network_test_tool.py','TEST_BOOT.command','TEST_ASUS.command'}
    special = {'macos/RealtekVTDNextEthernet.cpp','COLLECT_LOCAL.command','TEST_LOCAL.command'}
    changed = []; protected = 0
    for p,before in old.items():
        target = root/p
        assert target.is_file(), 'Missing baseline source file: '+p
        after = target.read_bytes()
        if before == after:
            if p.startswith(('src/','firmware/','macos/')): protected += 1
            continue
        assert p in metadata | version_only | special, 'Out-of-scope change: '+p
        if p in version_only:
            expected = before.decode().replace('0.20.2-AUTONEG','0.20.3-MEDIA1').replace('0.20.2','0.20.3')
            assert after.decode() == expected, 'Beyond version change: '+p
        changed.append({'path':p,'before_sha256':sha(before),'after_sha256':sha(after)})
    for directory in ['src','macos','firmware']:
        additions={p.relative_to(root).as_posix() for p in (root/directory).rglob('*') if p.is_file()}-set(old)
        assert additions<=({'src/PauseObservation.hpp'} if directory=='src' else set()), 'Unexpected hardware source additions: '+repr(additions)
    native = (root/'macos/RealtekVTDNextEthernet.cpp').read_text()
    original = old['macos/RealtekVTDNextEthernet.cpp'].decode()
    normalized = native
    for name in ['publishPause','reportLinkInactive']:
        normalized = normalized.replace(method(normalized,name)+'\n','')
    for name in ['createMedia','updateLink']:
        normalized = normalized.replace(method(normalized,name),method(original,name))
    normalized = normalized.replace('#include "../src/PauseObservation.hpp"\n','')
    normalized = normalized.replace('IONetworkMedium *media[10]{};IOEthernetAddress address{};uint64_t pauseSamples=0;', 'IONetworkMedium *media[7]{};IOEthernetAddress address{};')
    normalized = normalized.replace('\n void publishPause(const PHYDiagnostic &,const PauseResolution &);void reportLinkInactive(const char *);','')
    normalized = normalized.replace('"0.20.3-MEDIA1"','"0.20.2-AUTONEG"').replace('reportLinkInactive("inactive")','setLinkStatus(kIONetworkLinkValid)')
    assert normalized == original, 'Native changes outside media/reporting and version'
    for name in ['createMedia','updateLink','publishPause','reportLinkInactive']:
        body = method(native,name)
        for forbidden in ['->write','phyWrite','mdioWrite','macWrite','Modify(', 'startNetwork','stopNetwork','syncForCPU','serviceNetwork']:
            assert forbidden not in body, 'Configuration/DMA call in reporting: '+name
    header = (root/'src/PauseObservation.hpp').read_text()
    for forbidden in ['.write8(','.write16(','.write32(','phyWrite(','mdioWrite(','macWrite(','setFirmwareOCPBase(','restorePHYBase(']:
        assert forbidden not in header, 'Configuration write in observer'
    expected_test = old['TEST_LOCAL.command'].decode() + '\n' + 'xcrun clang++ -std=c++14 -Wall -Wextra -Werror -g tests/pause_observation.cpp -o build/tests/pause_observation\n' + 'build/tests/pause_observation\n' + 'python3 tests/media1_audit_tool.py\n'
    assert (root/'TEST_LOCAL.command').read_text() == expected_test, 'Existing test suite was changed'
    collect = old['COLLECT_LOCAL.command'].decode().replace('0.20.2-AUTONEG','0.20.3-MEDIA1').replace('autoneg-$stamp','media1-$stamp').replace('(PHYDiag[^" ]*|','(Pause[^" ]*|Media1[^" ]*|PHYDiag[^" ]*|').replace('for result in autoneg-acceptance-latest.json','for result in media1-reporting-latest.json autoneg-acceptance-latest.json')
    assert (root/'COLLECT_LOCAL.command').read_text() == collect
    report = {'result':'PASS','baseline_archive_sha256':ARCHIVE_SHA,'baseline_executable_sha256':BINARY_SHA,
        'baseline_manifest_verified_files':137,'unchanged_hardware_backend_files':protected,
        'all_existing_src_files_unchanged':True,'native_outside_media_reporting_unchanged':True,
        'select_medium_irq_dma_tx_rx_iommu_pm_watchdog_unchanged':True,'changed_manifest_files':changed,
        'hardware_acceptance':'PENDING'}
    return report

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--baseline-zip',type=pathlib.Path,required=True);parser.add_argument('--report',type=pathlib.Path)
    args=parser.parse_args();result=verify(pathlib.Path(__file__).resolve().parents[1],args.baseline_zip)
    if args.report: args.report.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: exact 0.20.2 archive/manifest/binary; protected sources and native DMA/TX/RX/IRQ/IOMMU/negotiation/PM/watchdog unchanged')
