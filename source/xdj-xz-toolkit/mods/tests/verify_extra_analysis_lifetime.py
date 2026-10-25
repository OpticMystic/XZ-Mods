import argparse,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--expect-red',action='store_true');a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False);root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from extra_player_fixtures import fixtures
fixtures(a.out)
def linux(p):
 s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
units=['tests/test_extra_analysis_lifetime.c','anlz_extra.c','shell_analysis.c','shell_runtime_core.c','sync_link_runtime.c',
 'ui/layered_wave_source.c','ui/layered_wave.c','ui/extra_lane.c','ui/wave_viewport.c','ui/ui_draw.c',
 'audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c']
rows=[]
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 if a.expect_red and arch=='arm':continue
 binary=a.out/arch;flags=['-O1','-g','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+linux(root)]
 flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
 cmd=[cc,*flags,*[linux(root/x) for x in units],'-lm','-o',linux(binary)]
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=120)
 (a.out/(arch+'-build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 for mode in ['shell','classic','anchor']+([] if a.expect_red else ['destroy']):
  cmd=(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary),linux(a.out),mode]
  r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
  (a.out/(arch+'-'+mode+'.log')).write_text(r.stdout+r.stderr);rows.append(dict(command=cmd,returncode=r.returncode))
  if a.expect_red:assert r.returncode and 'heap-use-after-free' in r.stderr,r.stdout+r.stderr
  else:assert not r.returncode,r.stdout+r.stderr
  print(arch,mode,'RED UAF' if a.expect_red else 'PASS',flush=True)
(a.out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
