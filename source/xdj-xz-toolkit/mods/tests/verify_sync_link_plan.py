"""Pure future native-Link plan fixtures; no native activation."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[1]
def linux(p):
 p=str(p.resolve()).replace('\\','/');return '/mnt/'+p[0].lower()+p[2:]
def run(*args):subprocess.run(['wsl','-d','Ubuntu','--cd',linux(root),'--exec',*map(str,args)],check=True)
for arch,compiler in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 binary=a.out/f'test-sync-link-plan-{arch}';flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if arch=='host' else []
 run(compiler,'-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'tests/test_sync_link_plan.c','sync_link_plan.c','-lm','-o',linux(binary))
 run(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(binary))
print('PASS Link plan host/ARM provenance/packet/clock/lease fixtures; unactivated')
