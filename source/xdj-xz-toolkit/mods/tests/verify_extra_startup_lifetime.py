import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);p.add_argument('--expect-red',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
def linux(p):
 s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
rows=[]
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 binary=a.out/arch
 cmd=[cc,'-O1','-g','-std=c11','-UNDEBUG','-DXZ_SYNC_OBSERVATION=0','-DXZ_PLACEMENT_CONNECTOR_TEST',
 '-DXZ_XP_TEST_RENDER_HOOK','-DUSB_MAJOR=1','-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
 '-I'+linux(root),linux(root/'tests/test_extra_startup_lifetime.c'),'-o',linux(binary)]
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
 (a.out/(arch+'-build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 cmd=(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
 (a.out/(arch+'-run.log')).write_text(r.stdout+r.stderr);rows.append(dict(command=cmd,returncode=r.returncode))
 if a.expect_red:assert r.returncode and 'PARTIAL_STARTUP_VISIBLE' in r.stderr,r.stdout+r.stderr
 else:assert not r.returncode,r.stdout+r.stderr
 print(arch,'RED private player exposed' if a.expect_red else 'PASS',flush=True)
(a.out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
