"""Actual-C accepted-output history tests. Existing GCC/ARM/QEMU only, no device access."""
from pathlib import Path
import argparse,json,subprocess,time
MODS=Path(__file__).resolve().parent.parent
def linux(p):
 s=str(p.resolve()).replace('\\','/');return '/mnt/'+s[0].lower()+s[2:]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',type=Path,required=True);ap.add_argument('--arch',choices=['host','arm'],required=True);a=ap.parse_args()
out=a.out.resolve();assert not out.exists(),'Use a new evidence directory';out.mkdir(parents=True)
cc='gcc' if a.arch=='host' else 'arm-linux-gnueabi-gcc'
flags=['-std=c11','-O2','-g','-UNDEBUG','-ffunction-sections','-fdata-sections','-DXZ_XP_SERVICE_PROOF=1','-DXZ_XP_JOG_TICKS_PER_REV=6600','-I'+linux(MODS)]
flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if a.arch=='host' else ['-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=softfp']
commands=[]
def run(cmd):
 start=time.monotonic();p=subprocess.run(cmd,capture_output=True,text=True)
 commands.append(dict(command=cmd,exit=p.returncode,seconds=time.monotonic()-start,stdout=p.stdout,stderr=p.stderr))
 (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(p.stdout+p.stderr);return p.returncode
rc=run(['wsl','-d','Ubuntu','--exec',cc,*flags,linux(MODS/'tests/test_completed_output.c'),linux(MODS/'sync_link_plan.c'),'-Wl,--gc-sections','-lm','-pthread','-o',linux(out/'completed-output')])
if not rc:rc=run(['wsl','-d','Ubuntu','--exec',*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if a.arch=='arm' else []),linux(out/'completed-output')])
raise SystemExit(rc)
