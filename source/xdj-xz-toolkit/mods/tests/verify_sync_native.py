"""Offline native sync reader fixtures: host sanitizers and extracted ARM-compatible implementation."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[1]
def linux(p):
 p=str(p.resolve()).replace('\\','/');return '/mnt/'+p[0].lower()+p[2:]
def run(*args):subprocess.run(['wsl','-d','Ubuntu','--cd',linux(root),'--exec',*map(str,args)],check=True)
for arch,compiler in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 binary=a.out/f'test-sync-native-{arch}';flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if arch=='host' else []
 run(compiler,'-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'tests/test_sync_native.c','sync_native.c','arcade/native_clock.c','-lm','-o',linux(binary))
 run(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(binary))
print('PASS read-only native sync snapshot host/ARM fixtures')
