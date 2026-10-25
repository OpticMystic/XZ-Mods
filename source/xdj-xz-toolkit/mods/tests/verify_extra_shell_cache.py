"""Compile the actual shell cache block; simulate a new track at reused addresses."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
source=(subprocess.check_output(['git','show','30cd9f7a14daf0327dd92f2d55cf593e884dfd56:source/xdj-xz-toolkit/mods/shell_runtime.c'],cwd=root,text=True) if a.baseline else (root/'shell_runtime.c').read_text())
section=source[source.index('static struct xz_extra_decks extra;'):source.index('static void extra_refresh_locked')]
defines='\n'.join(re.findall(r'^#define (?:GRID_CAP|WAVE_SCROLL_CAP|WAVE_OVERVIEW)\b.*$',source,re.M))
test=a.out/'cache.c';test.write_text('#include "shell_runtime.h"\n#include <assert.h>\n#include <string.h>\n#include <stdio.h>\n'+defines+'\n'+section+'''
int main(void){
 extra_reset();extra_notice_seen=0;
 struct xz_extra_column columns[4]={{80,0,0},{80,0,0},{80,0,0},{80,0,0}};
 struct xz_extra_beat grid[2]={{.ms=0,.beat_in_bar=1},{.ms=500,.beat_in_bar=2}};
 struct xz_extra_deck d={.loaded=1,.source=1,.jog_content_epoch=11,
  .known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID),
  .wave={columns,4,2000,765},.grid=grid,.grid_count=2,
  .timeline={.route_epoch=1,.load_revision=1,.media_generation=7}};
 extra_parts_of(0,&d);assert(extra_scroll[0][0].low==255&&extra_scroll[0][0].mid==0&&extra_grid[0][0].ms==0);
 for(unsigned i=0;i<4;i++)columns[i]=(struct xz_extra_column){0,80,0};
 grid[0].ms=50;d.jog_content_epoch++;d.timeline.load_revision++;
 extra_parts_of(0,&d);
 assert(extra_scroll[0][0].low==0&&extra_scroll[0][0].mid==255&&extra_grid[0][0].ms==50&&"CACHE_REUSED_ALLOCATION");
 puts("PASS new track at identical array addresses refreshes both waveform and grid");return 0;
}
''')
def linux(p):
 s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
rows=[]
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
 binary=a.out/arch
 cmd=[cc,'-O1','-g','-std=c11','-UNDEBUG','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+linux(root),linux(test),linux(root/'shell_runtime_core.c'),'-o',linux(binary)]
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
 (a.out/(arch+'-build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 cmd=(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
 (a.out/(arch+'-run.log')).write_text(r.stdout+r.stderr);rows.append(dict(command=cmd,returncode=r.returncode))
 if a.baseline:assert r.returncode and 'CACHE_REUSED_ALLOCATION' in r.stderr,r.stdout+r.stderr
 else:assert not r.returncode,r.stdout+r.stderr
 print(arch,'RED stale cache' if a.baseline else 'PASS',flush=True)
(a.out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
