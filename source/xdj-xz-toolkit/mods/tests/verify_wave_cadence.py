"""Offline actual-runtime cadence regression. Compiles own C; no native/device/network operations."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arch',choices=['host','arm'],required=True)
parser.add_argument('--out',type=Path,required=True,help='New output directory; never overwritten')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
def platform_path(path):
    value=str(path.resolve()).replace('\\','/')
    return '/mnt/'+value[0].lower()+value[2:] if os.name=='nt' else value
prefix=['wsl','-d','Ubuntu','--cd',platform_path(out),'--exec'] if os.name=='nt' else []
entries=[]
def invoke(command):
    result=subprocess.run(prefix+command,cwd=out,capture_output=True,text=True)
    entries.append({'command':prefix+command,'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
    (out/'commands.json').write_text(json.dumps(entries,indent=2)+'\n')
    if result.returncode:
        print(result.stdout+result.stderr)
        raise SystemExit(result.returncode)
    return result
cc='gcc' if args.arch=='host' else 'arm-linux-gnueabi-gcc'
flags=['-std=c11','-O1','-g','-UNDEBUG','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror',
       '-Wno-misleading-indentation','-Wno-format-truncation','-I'+platform_path(root),'-I'+platform_path(root/'shell')]
if args.arch=='host':
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie']
else:
    flags+=['-march=armv7-a','-mfpu=vfpv3-d16','-mfloat-abi=softfp']
units=['shell/shell.c','shell/shell_draw.c','shell/shell_play.c','shell/shell_wave.c','shell/shell_browse.c',
       'shell/shell_settings.c','shell/shell_info.c','shell/shell_source.c','shell/shell_menu.c','ui/ui_draw.c','ui/pictures.c',
       'shell_runtime_core.c','shell_library.c','shell_playlists.c','shell_mysettings.c','my_settings.c',
       'shell_utility.c','pdb/pdb.c','ui/ui.c','key/native_browse.c','extra_player.c']
fixture=root/'tests/test_wave_cadence.c'
deps=invoke([cc,*flags,'-MM',platform_path(fixture),*[platform_path(root/u) for u in units]])
inputs={}
for line in deps.stdout.replace('\\\n',' ').splitlines():
    for name in shlex.split(line.split(':',1)[1]):
        path=Path(name[5].upper()+':'+name[6:]) if os.name=='nt' and name.startswith('/mnt/') else Path(name)
        inputs[str(path.resolve())]=hashlib.sha256(path.read_bytes()).hexdigest()
invoke([cc,*flags,'-c',platform_path(fixture),'-o',platform_path(out/'fixture.o')])
binary=out/'test-wave-cadence'
invoke([cc,*flags,'-Wl,--gc-sections',platform_path(out/'fixture.o'),*[platform_path(root/u) for u in units],
        '-pthread','-ldl','-lm','-o',platform_path(binary)])
runner=['env','ASAN_OPTIONS=detect_leaks=0:abort_on_error=1','UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1'] if args.arch=='host' else ['qemu-arm','-L','/usr/arm-linux-gnueabi']
result=subprocess.run(prefix+runner+[platform_path(binary),platform_path(out)],cwd=out,capture_output=True,text=True)
entries.append({'command':prefix+runner+[platform_path(binary),platform_path(out)],'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
unchanged=all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in inputs.items())
(out/'commands.json').write_text(json.dumps(entries,indent=2)+'\n')
(out/'receipt.json').write_text(json.dumps({'arch':args.arch,'test_exit':result.returncode,'source_unchanged':unchanged,
    'inputs':inputs,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'hardware_contacted':False},indent=2)+'\n')
print('\n'.join(result.stdout.splitlines()[-2:]));print(result.stderr)
assert unchanged,'Compiler inputs changed during run'
sys.exit(result.returncode)
