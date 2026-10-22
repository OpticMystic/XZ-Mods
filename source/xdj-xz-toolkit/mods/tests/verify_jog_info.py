"""Actual Jog C INFO fixture, host sanitizers and ARM emulator. No device I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser()
p.add_argument('--out',required=True,type=Path)
p.add_argument('--expect-red',action='store_true')
p.add_argument('--red-key',action='store_true',help='Retain a mutant that omits the key packet write; it must fail')
p.add_argument('--fixtures',type=Path,default=Path('E:/Codex/xz-jog-offline-20261021/display/c-adapter'))
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
def linux(path):
    s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
results=[]
core=root/'jog_display.c'
if a.red_key:
    original=core.read_text();needle='p[0x3c]=s->key_code;'
    assert original.count(needle)==1
    core=a.out/'key-disabled.c';core.write_text(original.replace(needle,'/* Key output deliberately removed for RED. */'))
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=a.out/('info-'+arch)
    flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+linux(root),'-I'+linux(a.fixtures)]
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
    compile=[cc,*flags,linux(root/'tests/test_jog_display.c'),linux(core),linux(root/'jog_display_wave.c'),'-o',linux(binary)]
    run=(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]
    for i,cmd in enumerate([compile,run]):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        log=r.stdout+r.stderr;(a.out/f'{arch}-{i}.log').write_text(log)
        results.append(dict(command=cmd,returncode=r.returncode))
        if i and (a.expect_red or a.red_key):
            assert r.returncode and ('p[0x3c]' if a.red_key else 'p[9]&0x20u') in log,log
            print(f'{arch} RED: missing '+('key field' if a.red_key else 'native BPM-valid bit'))
        else:
            assert not r.returncode,log
            print(log,end='')
(a.out/'results.json').write_text(json.dumps(dict(results=results,fixtures_sha256=hashlib.sha256((a.fixtures/'jog_display_fixtures.h').read_bytes()).hexdigest()),indent=2)+'\n')
