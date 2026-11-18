from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess
R=Path(__file__).resolve().parent;M=R.parent
p=argparse.ArgumentParser();p.add_argument('--arch',choices=['host','arm'],default='host');p.add_argument('--output',type=Path,required=True);p.add_argument('--mods',type=Path,default=M);p.add_argument('--modes',default='delete-roundtrip,loop-out-edit,readonly,lifecycle,memory-loop');a=p.parse_args();M=a.mods.resolve();O=a.output.resolve();O.mkdir(exist_ok=False)
def linux(x):
 s=x.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
names=['extra_player.c','key/keyshift.c','key/music.c','key/control.c','deck_select.c','anlz_extra.c','shell_analysis.c','ui/layered_wave_source.c','ui/layered_wave.c','audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c','extra_cue_store.c'];files=[M/n for n in names]+[R/'test_cue_persistence_regressions.c']
flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread','-I'+linux(M),'-DXZ_XP_TEST_RENDER_HOOK','-Wno-unused-function']
if a.arch=='host':cc='gcc';flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie'];runner=[]
else:cc='arm-linux-gnueabi-gcc';flags+=['-mcpu=cortex-a9','-mfloat-abi=softfp','-mfpu=vfpv3'];runner=['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi']
receipt={'arch':a.arch,'inputs':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in files},'runs':[],'hardware':False}
def run(name,cmd):
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=120);(O/(name+'.stdout')).write_bytes(r.stdout);(O/(name+'.stderr')).write_bytes(r.stderr);receipt['runs'].append({'name':name,'command':cmd,'exit':r.returncode});(O/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(name,r.returncode,r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace'),flush=True);return r.returncode
if run('compile',[cc,*flags,*map(linux,files),'-lm','-o',linux(O/'test')]):raise SystemExit(2)
spec=importlib.util.spec_from_file_location('r14fixtures',M/'extra_player_fixtures.py');f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
failed=0
for mode in a.modes.split(','):
 data=O/mode;data.mkdir();f.fixtures(data);failed+=bool(run(mode,[*runner,linux(O/'test'),linux(data),mode]))
raise SystemExit(bool(failed))
