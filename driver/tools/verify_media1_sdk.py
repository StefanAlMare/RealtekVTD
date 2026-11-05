#!/usr/bin/env python3
"""Read-only validation of the exact local SDK; no fetch or checkout."""
import argparse,hashlib,json,os,pathlib,subprocess
def verify(sdk,manifest):
    expected=json.loads(manifest.read_text())
    def git(*args):return subprocess.check_output(['git','--no-optional-locks','-C',str(sdk),*args],text=True).strip()
    assert git('rev-parse','HEAD')==expected['commit']=='3f750085caa17ec3a7880f11c11bf4f48cd6a164','SDK commit mismatch'
    assert not git('status','--porcelain=v1','--untracked-files=all'),'SDK has local changes'
    tracked=set(subprocess.check_output(['git','--no-optional-locks','-C',str(sdk),'ls-files','-z']).decode().split('\0'))-{''}
    assert tracked==set(expected['tracked_files']),'SDK input set mismatch'
    for rel,entry in expected['tracked_files'].items():
        p=sdk/rel;assert p.resolve().is_relative_to(sdk.resolve()),'SDK link escapes root'
        assert ('symlink' if p.is_symlink() else 'file')==entry['kind']
        data=os.readlink(p).encode() if p.is_symlink() else p.read_bytes()
        assert hashlib.sha256(data).hexdigest()==entry['sha256'],'SDK bytes mismatch: '+rel
    print('PASS: pinned local SDK, clean tree, all '+str(len(tracked))+' input hashes; no remote access')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=pathlib.Path,required=True);a=p.parse_args()
    verify(a.sdk,pathlib.Path(__file__).resolve().parents[1]/'docs/SDK_INPUTS.json')
