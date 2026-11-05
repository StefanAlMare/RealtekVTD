#!/usr/bin/env python3
"""Read an already installed JTX1 diagnostic. Never installs/configures/tests traffic."""
import argparse
import datetime
import hashlib
import json
import plistlib
import re
import subprocess
from pathlib import Path
from decode_jtx1 import decode

def read(args):
    return subprocess.check_output(args, timeout=30)

def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values(): yield from nodes(child)
    elif isinstance(value, list):
        for child in value: yield from nodes(child)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    p.add_argument('--interface', default='en0')
    a = p.parse_args()
    assert re.fullmatch(r'en[0-9]+', a.interface), 'Invalid interface'
    assert not a.output.exists(), 'Output exists; refusing overwrite'
    raw = read(['/usr/sbin/ioreg', '-a', '-r', '-c', 'RealtekVTDNextEthernet'])
    matches = [v for v in nodes(plistlib.loads(raw)) if v.get('DriverVersion') == '0.20.5-JTX1-DIAG' and 'JTX1Trace' in v]
    assert len(matches) == 1, 'Exactly one running JTX1-DIAG with export required'
    blob = matches[0]['JTX1Trace']; assert isinstance(blob, bytes)
    decoded = decode(blob)
    info = {'collected_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'driver': '0.20.5-JTX1-DIAG', 'rxstat1': 'DEFERRED',
            'test_result': 'NOT EVALUATED', 'installation_performed': False,
            'trace_sha256': hashlib.sha256(blob).hexdigest(),
            'sw_vers': read(['/usr/bin/sw_vers']).decode(),
            'hostname': read(['/bin/hostname']).decode().strip()}
    config = read(['/sbin/ifconfig', '-v', a.interface])
    a.output.mkdir(parents=True, exist_ok=False)
    (a.output / 'ioreg.plist').write_bytes(raw)
    (a.output / 'JTX1Trace.bin').write_bytes(blob)
    (a.output / 'JTX1Trace.json').write_text(json.dumps(decoded, indent=2) + '\n')
    (a.output / 'PROVENANCE.json').write_text(json.dumps(info, indent=2) + '\n')
    (a.output / 'ifconfig.txt').write_bytes(config)
    sums = ''.join(hashlib.sha256(f.read_bytes()).hexdigest() + '  ' + f.name + '\n'
                   for f in sorted(a.output.iterdir()) if f.is_file())
    (a.output / 'SHA256SUMS.txt').write_text(sums)
    print('CAPTURED JTX1 RAM export; hardware delivery has NOT been declared PASS: ' + str(a.output))

if __name__ == '__main__': main()
