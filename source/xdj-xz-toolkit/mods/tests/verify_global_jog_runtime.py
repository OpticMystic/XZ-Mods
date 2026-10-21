"""Build offline global-jog ownership and production-player input tests (Ubuntu WSL)."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
def linux(p):
 p=str(p.resolve()).replace('\\','/');return '/mnt/'+p[0].lower()+p[2:]
def run(*args):subprocess.run(['wsl','-d','Ubuntu','--cd',linux(root),'--exec',*map(str,args)],check=True)
for arch,cc,objcopy in [('host','gcc','objcopy'),('arm','arm-linux-gnueabi-gcc','arm-linux-gnueabi-objcopy')]:
 selector=a.out/f'test-global-selector-{arch}';run(cc,'-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','tests/test_global_jog.c','deck_select.c','-o',linux(selector))
 run(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(selector))
 kart=a.out/f'test-kart-jog-pending-{arch}';run(cc,'-std=c11','-O1','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wall','-Wextra','-Werror','tests/test_kart_jog_pending.c','kart/xz_kart_input.c','-lpthread','-o',linux(kart))
 run(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(kart))
 sync=a.out/f'test-sync-edges-{arch}';run(cc,'-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','tests/test_sync_edges.c','deck_select.c','-o',linux(sync))
 run(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(sync))
 objects=[]
 for name in ['runtime','player']:
  obj=a.out/f'global-jog-{arch}-{name}.o';objects.append(linux(obj));run(cc,'-std=c11','-O1','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-c',f'tests/test_global_jog_{name}.c','-o',linux(obj))
  if name=='runtime':run(objcopy,'--remove-section=.init_array','--remove-section=.fini_array',linux(obj))
 # Removing fixture constructor/destructor sections prevents production install hooks from running.
 binary=a.out/f'test-global-runtime-{arch}';run(cc,'-Wl,--gc-sections',*objects,'deck_select.c','-lm','-lpthread','-o',linux(binary))
 for side in [0,1]:
  for selected in [0,1]:run(*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else []),linux(binary),side,selected,-9)
print('PASS host and ARM global runtime, actual player queue, ownership, games and route loss')
