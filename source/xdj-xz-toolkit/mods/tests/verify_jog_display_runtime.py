"""Jog bridge: source startup order, real C runtime/publication gates, host/ARM.

Pass --zig /path/to/zig for the optional Windows UBSan gate. Uses existing tools.
No native constructor, device, network, USB or live runtime is executed.
"""
import argparse, subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--out',required=True,type=Path)
p.add_argument('--zig',type=Path)
a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]

# Guard constructor ordering without executing any native/runtime boundary.
startup=(root/'runtime.c').read_text().split('__attribute__((constructor)) static void start(void) {',1)[1].split('__attribute__((destructor))',1)[0]
assert startup.index('xz_jog_display_runtime_start()')<startup.index('xz_extra_decks_start()')
assert startup.index('xz_extra_decks_start()')<startup.index('xz_sync_link_runtime_start()')
print('PASS source constructor installs Jog before players/connector; native constructor not executed',flush=True)

def linux(path):
 s=path.resolve().as_posix()
 return '/mnt/'+s[0].lower()+s[2:]

def wsl(*args):
 subprocess.run(['wsl','-d','Ubuntu','--exec',*map(str,args)],check=True,timeout=60)

for arch,compiler in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 for proof in [False,True]:
  for test in ['runtime','publication']:
   binary=a.out/f'test-jog-{test}-{arch}{"-proof" if proof else ""}'
   sources=[root/f'tests/test_jog_display_{test}.c',root/'jog_display.c',root/'jog_display_wave.c']
   if test=='runtime':sources.append(root/'jog_display_runtime.c')
   flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-pthread']
   if proof:flags.append('-DXZ_JOG_DISPLAY_PROOF')
   flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
   wsl(compiler,*flags,*map(linux,sources),'-o',linux(binary))
   wsl(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(binary))
if a.zig:
 for proof in [False,True]:
  binary=a.out/f'test-jog-publication-zig{"-proof" if proof else ""}.exe'
  subprocess.run([str(a.zig),'cc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-pthread',
   '-fsanitize=undefined','-fno-sanitize-recover=all',*(['-DXZ_JOG_DISPLAY_PROOF'] if proof else []),
   str(root/'tests/test_jog_display_publication.c'),str(root/'jog_display.c'),str(root/'jog_display_wave.c'),'-o',str(binary)],check=True,timeout=60)
  subprocess.run([str(binary)],check=True,timeout=60)
print('PASS Jog runtime/publication gates; all native, clock and route boundaries remain synthetic')
