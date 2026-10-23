"""Offline own-C phase/generation regressions. Existing GCC/ARM/QEMU only."""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
M=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--arch',choices=['host','arm'],required=True);ap.add_argument('--data',type=Path,required=True);a=ap.parse_args()
O=a.out.resolve();assert not O.exists();O.mkdir(parents=True)
def linux(p):
 s=str(p.resolve()).replace('\\','/');return '/mnt/'+s[0].lower()+s[2:]
cc='gcc' if a.arch=='host' else 'arm-linux-gnueabi-gcc'
flags=['-std=c11','-O1','-g','-UNDEBUG','-ffunction-sections','-fdata-sections','-DXZ_XP_JOG_TICKS_PER_REV=6600','-I'+linux(M)]
flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if a.arch=='host' else ['-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=softfp']
commands=[]
def run(cmd):
 start=time.monotonic();p=subprocess.run(cmd,capture_output=True,text=True)
 commands.append(dict(command=cmd,exit=p.returncode,seconds=time.monotonic()-start,stdout=p.stdout,stderr=p.stderr))
 (O/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');print((p.stdout+p.stderr)[-5000:],flush=True);return p.returncode
import shutil
shutil.copytree(a.data.resolve(),O/'data')
all_sources=[]
for suite in ['phase','generation']:
 sources=[M/f'tests/test_completed_{suite}.c']
 if suite=='phase':sources += [M/'sync_link_plan.c',M/'sync_link_runtime.c']
 else:sources += [M/n for n in ['extra_player.c','deck_select.c','anlz_extra.c','shell_analysis.c','ui/layered_wave_source.c','ui/layered_wave.c','audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c']]
 all_sources+=sources
 rc=run(['wsl','-d','Ubuntu','--exec',cc,*flags,*map(linux,sources),'-Wl,--gc-sections','-lm','-pthread','-o',linux(O/suite)])
 if not rc:rc=run(['wsl','-d','Ubuntu','--cd',linux(O),'--exec',*(['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi'] if a.arch=='arm' else []),linux(O/suite),*([linux(O/'data')] if suite=='generation' else [])])
 if rc:break
(O/'receipt.json').write_text(json.dumps(dict(exit=rc,arch=a.arch,sources={str(p.relative_to(M)):hashlib.sha256(p.read_bytes()).hexdigest() for p in all_sources},binaries={n:hashlib.sha256((O/n).read_bytes()).hexdigest() for n in ['phase','generation'] if (O/n).exists()}),indent=2)+'\n')
raise SystemExit(rc)
