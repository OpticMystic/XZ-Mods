from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess,sys
sys.dont_write_bytecode=True
R=Path(__file__).resolve().parent;M=R.parent
p=argparse.ArgumentParser();p.add_argument('--arch',choices=['host','arm'],default='host');p.add_argument('--output',type=Path,required=True);p.add_argument('--modes',default='new-roundtrips,fractional-export,adjacent,loop-explicit,loop-timeout,loop-readonly,call-colors,short-fractional-loop,export-loop-first-save,export-loop-edited');a=p.parse_args()
O=a.output.resolve();O.mkdir(parents=True,exist_ok=False)
def linux(p):
 s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
names=['extra_player.c','deck_select.c','anlz_extra.c','shell_analysis.c','ui/layered_wave_source.c','ui/layered_wave.c','audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c','extra_cue_store.c']
files=[M/n for n in names]+[R/'test_cue_endpoint_regressions.c']
flags=['-x','c','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-pthread','-I'+linux(M),'-DXZ_XP_TEST_RENDER_HOOK']
if a.arch=='host':cc='gcc';flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie'];runner=[]
else:cc='arm-linux-gnueabi-gcc';flags+=['-mcpu=cortex-a9','-mfloat-abi=softfp','-mfpu=vfpv3'];runner=['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi']
receipt={'arch':a.arch,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'runs':[],'hardware':False}
def run(name,cmd):
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=180)
 (O/(name+'.stdout')).write_bytes(r.stdout);(O/(name+'.stderr')).write_bytes(r.stderr)
 receipt['runs'].append({'name':name,'command':['wsl','-d','Ubuntu','--exec',*cmd],'exit':r.returncode})
 (O/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(name,'exit='+str(r.returncode),r.stdout.decode(errors='replace').splitlines()[-1:] or r.stderr.decode(errors='replace')[-500:],flush=True)
 return r.returncode
if run('compile',[cc,*flags,*map(linux,files),'-lm','-o',linux(O/'test')]):raise SystemExit(2)
spec=importlib.util.spec_from_file_location('fixture',M/'extra_player_fixtures.py');f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
failed=0
for mode in a.modes.split(','):
 for deck in [0,1]:
  for hz in [44100,48000]:
   name=f'{mode}-d{deck+3}-{hz}';data=O/name;data.mkdir();f.fixtures(data)
   failed+=bool(run(name,[*runner,linux(O/'test'),linux(data),mode,str(deck),str(hz)]))
raise SystemExit(bool(failed))
