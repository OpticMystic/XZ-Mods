"""Focused offline proof build, own-source callback tests and mocked PC reader."""
import argparse
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]

def linux(path):
    s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]

def wsl(*args):
    result=subprocess.run(['wsl','-d','Ubuntu','--exec',*map(str,args)],capture_output=True,text=True,timeout=60)
    if result.returncode:print(result.stdout+result.stderr)
    result.check_returncode()
    return result

for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=a.out/f'latch-{arch}'
    flags=['-std=c11','-O1','-g','-UNDEBUG','-DXZ_JOG_DISPLAY_PROOF','-Wall','-Wextra','-Werror','-pthread','-rdynamic']
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
    print(wsl(cc,*flags,linux(root/'tests/test_jog_display_latch.c'),linux(root/'jog_display.c'),linux(root/'jog_display_wave.c'),'-o',linux(binary)).stdout,end='')
    prefix=[] if arch=='host' else ['qemu-arm','-L','/usr/arm-linux-gnueabi']
    print(wsl(*prefix,linux(binary)).stdout,end='')
    nm='nm' if arch=='host' else 'arm-linux-gnueabi-nm'
    symbols=wsl(nm,'-D','-S','--defined-only',linux(binary)).stdout.splitlines()
    for name,size in [('xz_mods_jog_proof_v1',196),('xz_mods_jog_latch_v2',4184)]:
        rows=[row.split() for row in symbols if row.endswith(' '+name)]
        assert len(rows)==1 and int(rows[0][1],16)==size and rows[0][2] in ('D','B')
    print('PASS',arch,'exported own-code metadata: v1=196 latch_v2=4184')
    retained=a.out/f'retention-{arch}'
    print(wsl(cc,*flags,linux(root/'tests/test_jog_display_retention.c'),linux(root/'jog_display.c'),linux(root/'jog_display_wave.c'),'-o',linux(retained)).stdout,end='')
    result=wsl(*prefix,linux(retained))
    (a.out/f'retention-{arch}-raw.log').write_text(result.stdout+result.stderr)
    print('\n'.join(line for line in result.stdout.splitlines() if not line.startswith('LATCH_HEX ')))
subprocess.run([sys.executable,'-B','-m','unittest','discover','-s',str(root/'tests'),
                '-p','test_read_jog_display_latch.py','-v'],check=True,timeout=60)
