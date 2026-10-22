"""Actual player LOAD/publication epoch on host and ARM. Synthetic media/sink only."""
import argparse,json,sys,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path)
p.add_argument('--red-old-edge',action='store_true');a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False);root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root));from extra_player_fixtures import fixtures
fixtures(a.out)
def linux(p):
    s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
sources=['tests/test_jog_content_epoch.c','anlz_extra.c','shell_analysis.c','ui/layered_wave_source.c',
         'ui/layered_wave.c','audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c']
test=root/sources[0]
if a.red_old_edge:
    mutant=a.out/'mutant';(mutant/'tests').mkdir(parents=True)
    source=(root/'extra_player.c').read_text();needle='s==XZ_XP_LOADING&&d->jog_load_pending?'
    assert source.count(needle)==1
    (mutant/'extra_player.c').write_text(source.replace(needle,'s==XZ_XP_LOADING&&atomic_load(&d->published_state)!=XZ_XP_LOADING?'))
    test=mutant/sources[0];test.write_text((root/sources[0]).read_text())
results=[]
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=a.out/('content-'+arch)
    flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
           '-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+linux(root)]
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
    commands=[[cc,*flags,linux(test),*[linux(root/s) for s in sources[1:]],'-lm','-o',linux(binary)],
              (['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary),linux(a.out)]]
    for i,cmd in enumerate(commands):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        (a.out/f'{arch}-{i}.log').write_text(r.stdout+r.stderr)
        results.append(dict(command=cmd,returncode=r.returncode))
        if i and a.red_old_edge:
            assert r.returncode and 'loaded+2' in r.stderr,r.stdout+r.stderr
            print(arch+' RED: published-state edge loses second accepted LOAD')
        else:
            assert not r.returncode,r.stdout+r.stderr
            print(r.stdout,end='')
(a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
