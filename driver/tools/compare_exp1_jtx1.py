#!/usr/bin/env python3
"""Compile a host model only; compare descriptor/MMIO/DMA transcripts to exact EXP1."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(); p.add_argument('--exp1-source', type=Path, required=True); a = p.parse_args()
root = Path(__file__).resolve().parents[1]
baseline = a.exp1_source.resolve()
expected = 'd6d0294a814246ec629f87afced77d47225d5f2a7b552b608b590fcf3961446d'
assert hashlib.sha256((baseline/'source-manifest.json').read_bytes()).hexdigest() == expected
for name, digest in json.loads((baseline/'source-manifest.json').read_text()).items():
    assert hashlib.sha256((baseline/name).read_bytes()).hexdigest() == digest
out = root/'build/equivalence'; out.mkdir(parents=True, exist_ok=True)
results = []
for name, tree, diagnostic in [('EXP1', baseline, False), ('JTX1', root, True)]:
    exe = out/name
    cmd = ['xcrun', 'clang++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
           '-DEXP_FIXTURE="'+str(tree/'tests/jumbo.cpp')+'"']
    if diagnostic: cmd += ['-DJTX_OBSERVE=1']
    cmd += [str(root/'tests/jtx_equivalence_harness.cpp'), *map(str, sorted((tree/'src').glob('*.cpp'))), '-o', str(exe)]
    subprocess.run(cmd, check=True)
    data = subprocess.check_output([str(exe)])
    (out/(name+'.transcript.txt')).write_bytes(data)
    results.append({'candidate': name, 'sha256': hashlib.sha256(data).hexdigest(), 'lines': len(data.splitlines())})
assert results[0]['sha256'] == results[1]['sha256'], 'EXP1/JTX1 operations differ'
receipt = {'status': 'PASS', 'exact_EXP1_source': True, 'transcripts': results,
           'scope': 'descriptors, MMIO, DMA sync/barriers/BME/release, wrap, 1500/4000/9000, VLAN, two simulated DMA modes',
           'hardware_equivalence_proven': False}
(out/'VERIFICATION.json').write_text(json.dumps(receipt, indent=2)+'\n')
print('PASS exact EXP1/JTX1 transcript equality: '+str(results[0]['lines'])+' lines; host model only')
