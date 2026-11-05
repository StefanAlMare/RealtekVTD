#!/usr/bin/env python3
"""Read-only verification of the accepted beta image, source and SDK."""
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import uuid
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify_tree(root, manifest, self_name=None):
    files = manifest['files']
    actual = {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}
    expected = set(files) | ({self_name} if self_name else set())
    if actual != expected:
        raise SystemExit('File set mismatch: ' + str(root))
    for name, value in files.items():
        target = (root / name).resolve()
        wanted = value if isinstance(value, str) else value['sha256']
        if not target.is_relative_to(root.resolve()) or digest(target.read_bytes()) != wanted:
            raise SystemExit('Hash/path mismatch: ' + name)
    return len(files)


def main():
    root = Path(__file__).resolve().parents[1]
    identity = json.loads((root / 'beta/IDENTITY.json').read_text())
    source = root / 'driver'
    manifest_name = 'source-manifest-MAC46-N64-TXPRECHECK-B.json'
    manifest_bytes = (source / manifest_name).read_bytes()
    if digest(manifest_bytes) != identity['source_manifest_sha256']:
        raise SystemExit('Wrong source manifest')
    manifest = json.loads(manifest_bytes)
    count = verify_tree(source, manifest, manifest_name)
    if count != 281:
        raise SystemExit('Unexpected source entry count')
    sdk_manifest = json.loads((root / 'dependencies/SDK-MANIFEST.json').read_text())
    if sdk_manifest['commit'] != '3f750085caa17ec3a7880f11c11bf4f48cd6a164':
        raise SystemExit('Wrong SDK commit')
    sdk_count = verify_tree(root / 'dependencies/MacKernelSDK', sdk_manifest)
    if sdk_count != 1232:
        raise SystemExit('Unexpected SDK file count')
    archive = root / 'beta/KEXT-MAC46-N64-TXPRECHECK-B.zip'
    if digest(archive.read_bytes()) != identity['kext_archive_sha256']:
        raise SystemExit('Wrong kext archive')
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise SystemExit('ZIP CRC failure')
        prefix = 'RealtekVTDNextEthernet.kext/Contents/'
        if set(z.namelist()) != {prefix + 'Info.plist', prefix + 'MacOS/RealtekVTDNextEthernet'}:
            raise SystemExit('Unexpected kext ZIP members')
        binary = z.read(prefix + 'MacOS/RealtekVTDNextEthernet')
        info = plistlib.loads(z.read(prefix + 'Info.plist'))
    if len(binary) != identity['executable_bytes'] or digest(binary) != identity['executable_sha256']:
        raise SystemExit('Wrong executable')
    magic, cpu, _, kind, ncmds, sizecmds, _, _ = struct.unpack_from('<8I', binary)
    if (magic, cpu, kind) != (0xfeedfacf, 0x01000007, 11) or 32 + sizecmds > len(binary):
        raise SystemExit('Expected x86_64 Mach-O kext bundle')
    offset = 32
    uuids = []
    for _ in range(ncmds):
        if offset + 8 > 32 + sizecmds:
            raise SystemExit('Invalid Mach-O command header')
        cmd, size = struct.unpack_from('<II', binary, offset)
        if size < 8 or offset + size > 32 + sizecmds:
            raise SystemExit('Invalid Mach-O command size')
        if cmd == 0x1b:
            if size != 24:
                raise SystemExit('Invalid UUID command')
            uuids.append(str(uuid.UUID(bytes=binary[offset + 8:offset + 24])).upper())
        offset += size
    if offset != 32 + sizecmds or uuids != [identity['uuid']]:
        raise SystemExit('Wrong Mach-O UUID')
    if info['CFBundleIdentifier'] != 'local.realtekvtdnext.ethernet':
        raise SystemExit('Wrong bundle identifier')
    # The packaging template stores the full original identity in the personality.
    def contains_build(obj):
        if isinstance(obj, dict):
            return any(contains_build(v) for v in obj.values())
        if isinstance(obj, list):
            return any(contains_build(v) for v in obj)
        return obj == manifest['build']
    if not contains_build(info):
        raise SystemExit('Wrong full build identity')
    print(json.dumps({'status': 'PASS', 'uuid': uuids[0],
                      'executable_sha256': digest(binary), 'source_entries': count,
                      'sdk_files': sdk_count, 'archive_crc': 'PASS',
                      'original_binary_preserved': True, 'driver_rebuilt': False,
                      'physical_tests_executed': False}, indent=2))


if __name__ == '__main__':
    main()
