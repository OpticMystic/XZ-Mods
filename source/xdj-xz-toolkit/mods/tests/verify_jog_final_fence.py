"""Replay the unchanged independent clock-gap probe against this source. Offline."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path)
p.add_argument('--review',type=Path,default=Path('E:/Codex/xz-jog-presentation-review-20261022'))
p.add_argument('--expect-red',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1];probe=a.review/'final_fence_race.c'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def linux(p):
 s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
files=[probe,root/'jog_display.c',root/'jog_display.h',root/'jog_display_runtime.c',root/'jog_display_wave.c',root/'sync_master.h']
before={str(p):sha(p) for p in files};rows=[]
assert sha(probe)=='ccbc261f86d663db993f13dcab4721f8f0bdd8fec229099be3e28a6ac4ff4b49'
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 binary=a.out/('fence-'+arch)
 flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-pthread','-I'+linux(root)]
 flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
 commands=[('build',[cc,*flags,linux(probe),linux(root/'jog_display.c'),linux(root/'jog_display_wave.c'),'-o',linux(binary)],0)]
 run=(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]
 commands += [('control',run,0),('flip',run+['--flip'],1 if a.expect_red else 0)]
 for name,cmd,expected in commands:
  r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
  (a.out/f'{arch}-{name}.log').write_text(r.stdout+r.stderr)
  rows.append(dict(arch=arch,case=name,command=cmd,returncode=r.returncode,expected=expected))
  assert r.returncode==expected,r.stdout+r.stderr
  if name!='build':print(arch,name,r.returncode,r.stdout.strip())
assert before=={str(p):sha(p) for p in files}
(a.out/'results.json').write_text(json.dumps(dict(source_hashes=before,rows=rows),indent=2)+'\n')
