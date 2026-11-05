#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[1] / 'runtime/decode_jtx1.py'
spec = importlib.util.spec_from_file_location('jtx_decoder', path)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
blob = Path(sys.argv[1]).read_bytes(); report = module.decode(blob)
assert report['count'] == 10 and report['delivery_on_wire_proven'] is False
for i, record in enumerate(report['records']):
    assert record['icmp_sequence'] == i and record['icmp_id'] == 0xcafe
    assert record['length'] == 4014 and record['ipv4_length'] == 4000
    assert record['destination_mac'] == '02:11:22:33:44:55'
    assert record['copy']['prefix_matches_snapshot']
    assert all(record['sync_and_kick'].values())
    assert record['publication_bits']['OWN'] and record['publication_bits']['FS'] and record['publication_bits']['LS']
    assert record['completion_raw_valid'] and not record['completion_opts1'] & (1 << 31)
    assert bool(record['completion_opts1'] & 0x00800000) == (1 <= i <= 5)
    assert record['phy_at_publication']['an_pending'] == 1
for bad in [b'', blob[:-1], blob + b'X', b'XXXX' + blob[4:]]:
    try: module.decode(bad)
    except (AssertionError, ValueError): pass
    else: raise AssertionError('Malformed export accepted')
print('PASS JTX1 decoder: C++ binary roundtrip, exact identities/raw status/cache age, malformed fail-closed; no payload and no hardware PASS inference')
