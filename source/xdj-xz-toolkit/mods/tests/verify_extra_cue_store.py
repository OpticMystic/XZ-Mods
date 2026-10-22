from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess
M=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--arch',choices=['host','arm'],default='host');ap.add_argument('--sanitizer',choices=['address','thread'],default='address');ap.add_argument('--player',action='store_true');a=ap.parse_args()
O=a.output.resolve();O.mkdir(parents=True,exist_ok=False);(O/'data').mkdir()
def linux(p):
 s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
files=[M/'extra_cue_store.c',M/'tests/test_extra_cue_store.c']
if a.player:
 files=[M/n for n in ['extra_player.c','deck_select.c','anlz_extra.c','shell_analysis.c','ui/layered_wave_source.c','ui/layered_wave.c','audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c','extra_cue_store.c','tests/test_stock_cues_review.c']]
 spec=importlib.util.spec_from_file_location('cue_fixtures',M/'extra_player_fixtures.py');fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture);fixture.fixtures(O/'data')
flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread','-I'+linux(M)]
if a.player:flags+=['-DXZ_XP_TEST_RENDER_HOOK','-Wno-unused-function']
if a.arch=='host':
 cc='gcc';flags+=['-fsanitize=thread','-fno-pie','-no-pie'] if a.sanitizer=='thread' else ['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie']
 runner=['setarch','x86_64','-R'] if a.sanitizer=='thread' else []
else:cc='arm-linux-gnueabi-gcc';flags+=['-mcpu=cortex-a9','-mfloat-abi=softfp','-mfpu=vfpv3'];runner=['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi']
receipt={'arch':a.arch,'player':a.player,'sanitizer':a.sanitizer if a.arch=='host' else None,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files+list(M.glob('*.h'))},'runs':[],'hardware':False}
for name,cmd in [('compile',[cc,*flags,*map(linux,files),'-lm','-o',linux(O/'test')]),('run',[*runner,linux(O/'test'),linux(O/'data')]+(['cues'] if a.player else []))]:
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=120)
 (O/(name+'.stdout')).write_bytes(r.stdout);(O/(name+'.stderr')).write_bytes(r.stderr)
 receipt['runs'].append({'name':name,'command':cmd,'exit':r.returncode});(O/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(name,r.returncode,r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace'),flush=True)
 if r.returncode:raise SystemExit(r.returncode)
