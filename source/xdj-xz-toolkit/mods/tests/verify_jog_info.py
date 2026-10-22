"""Actual Jog C INFO fixture, host sanitizers and ARM emulator. No device I/O."""
import argparse
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser()
p.add_argument('--out',required=True,type=Path)
p.add_argument('--expect-red',action='store_true')
p.add_argument('--fixtures',type=Path,default=Path('E:/Codex/xz-jog-offline-20261021/display/c-adapter'))
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
def linux(path):
    s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
results=[]
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=a.out/('info-'+arch)
    flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+linux(root),'-I'+linux(a.fixtures)]
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
    compile=[cc,*flags,linux(root/'tests/test_jog_display.c'),linux(root/'jog_display.c'),'-o',linux(binary)]
    run=(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]
    for i,cmd in enumerate([compile,run]):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        log=r.stdout+r.stderr;(a.out/f'{arch}-{i}.log').write_text(log)
        results.append(dict(command=cmd,returncode=r.returncode))
        if i and a.expect_red:
            assert r.returncode and 'p[9]&0x20u' in log,log
            print(f'{arch} RED: missing native BPM-valid bit')
        else:
            assert not r.returncode,log
            print(log,end='')
(a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
