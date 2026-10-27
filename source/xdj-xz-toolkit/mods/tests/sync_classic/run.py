"""Offline production SYNC scheduling regression. No hardware, network or native process access.
Run on Linux, or Windows with an existing Ubuntu WSL toolchain. No dependency installation.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,shutil
HERE=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,default=HERE.parents[1]);ap.add_argument('--build-dir',type=Path,required=True);ap.add_argument('--arch',choices=['host','arm'],required=True);ap.add_argument('--expect-red',action='store_true');ap.add_argument('--mode',choices=['positive','negative','coherence','uncertainty','refusal','fence','master','prefence','drawing','observation','completion','boundary','guards','all'],default='all');a=ap.parse_args()
src=a.source.resolve();build=a.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
if src==build or src in build.parents:raise SystemExit('Build directory must be outside source tree')
def linux(p):
 s=str(p).replace('\\','/');return '/mnt/'+s[0].lower()+s[2:] if len(s)>1 and s[1]==':' else s
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
before={str(p.relative_to(src)):digest(p) for p in src.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.py','.inc')}
subprocess.run([sys.executable,'-B',str(HERE/'generate.py'),'--source',str(src),'--out',str(build)],check=True)
for name in ['native_adapter.c','integration.c','load_cases.inc']:shutil.copyfile(HERE/name,build/name)
sys.path.insert(0,str(src));from extra_player_fixtures import fixtures
data=build/'data';data.mkdir(exist_ok=True);fixtures(data)
entries=[]
def run(args,expected=0):
 command=['wsl','-d','Ubuntu','--cd',linux(build),'--exec',*map(str,args)] if sys.platform=='win32' else list(map(str,args))
 p=subprocess.run(command,capture_output=True,text=True)
 entries.append({'args':command,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr});(build/'checks.json').write_text(json.dumps(entries,indent=2)+'\n')
 print(p.stdout,end='');print(p.stderr,end='',file=sys.stderr)
 if p.returncode!=expected:raise RuntimeError(f'expected {expected}, got {p.returncode}')
 return p
cc='gcc' if a.arch=='host' else 'arm-linux-gnueabi-gcc';oc='objcopy' if a.arch=='host' else 'arm-linux-gnueabi-objcopy'
flags=['-std=c11','-O1','-g','-UNDEBUG','-ffunction-sections','-fdata-sections','-I'+linux(src),'-I'+linux(build),'-pthread','-DXZ_XP_TEST_RENDER_HOOK']
if a.arch=='host':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-fno-pie','-no-pie']
else:flags+=['-march=armv7-a']
run([cc,*flags,'-fsyntax-only',linux(src/'shell_runtime.c')])
objects=[]
for name in ['runtime_adapter','shell_adapter','native_adapter','integration']:
 obj=build/(name+'.o');run([cc,*flags,'-c',linux(build/(name+'.c')),'-o',linux(obj)]);objects.append(linux(obj))
 if name=='runtime_adapter':run([oc,'--remove-section=.init_array','--remove-section=.fini_array',linux(obj)])
sources=['extra_player.c','key/keyshift.c','key/music.c','key/control.c','anlz_extra.c','shell_analysis.c','ui/layered_wave_source.c','ui/layered_wave.c','audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c','deck_select.c','settings.c','shell_runtime_core.c','sync_native.c','arcade/native_clock.c']
binary=build/'integration';run([cc,*flags,'-Wl,--gc-sections',*objects,*[linux(src/n) for n in sources],'-lm','-o',linux(binary)])
results={}
for mode in ['positive','negative','coherence','uncertainty','refusal','fence','master','prefence','drawing','observation','completion','boundary','guards'] if a.mode=='all' else [a.mode]:
 result=run([*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if a.arch=='arm' else []),linux(binary),linux(data),mode],1 if a.expect_red else 0)
 assert 'RESULT checks=' in result.stdout,'Missing assertion summary'
 if a.expect_red:assert 'FAIL ' in result.stderr,'Expected behavioral RED, not a tool/process failure'
 results[mode]={'exit':result.returncode,'summary':result.stdout.splitlines()[-1]}
after={str(p.relative_to(src)):digest(p) for p in src.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.py','.inc')}
assert before==after,'Source changed during tests'
(build/'receipt.json').write_text(json.dumps({'binary_sha256':digest(binary),'expected_red':a.expect_red,'arch':a.arch,'results':results,'physical_acceptance':False,'source_drift':[],'source_sha256':digest(src/'shell_runtime.c'),'fixture_hashes':{p.name:digest(p) for p in HERE.iterdir() if p.is_file()}},indent=2)+'\n')
