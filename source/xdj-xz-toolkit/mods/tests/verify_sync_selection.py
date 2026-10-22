from pathlib import Path
import argparse,hashlib,json,subprocess
tests=Path(__file__).resolve().parent
p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=tests.parent)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--arch',choices=['host','arm'],default='host')
a=p.parse_args();m=a.source.resolve();o=a.output.resolve();o.mkdir(parents=True,exist_ok=False)
def linux(path):
 s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
runtime=(m/'runtime.c').read_text();begin=runtime.find('int xz_runtime_deck_selection_snapshot(')
body=runtime[begin:runtime.index('\n/* Publishes',begin)] if begin>=0 else ''
(o/'runtime-selection.inc').write_text(body)
inputs=[m/'runtime.c',m/'runtime.h',m/'deck_select.c',m/'deck_select.h',tests/'test_sync_selection_epoch.c']
receipt={'arch':a.arch,'inputs':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},'runs':[],'hardware_verified':False}
flags=['-std=c11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-I'+linux(m),'-I'+linux(o)]
if a.arch=='host':
 cc='gcc';flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie'];run=[]
else:
 cc='arm-linux-gnueabi-gcc';flags+=['-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=softfp'];run=['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi']
binary=o/'selection'
for name,cmd in [('compile',[cc,*flags,linux(m/'deck_select.c'),linux(tests/'test_sync_selection_epoch.c'),'-lm','-o',linux(binary)]),('run',run+[linux(binary)])]:
 r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=60)
 (o/(name+'.stdout')).write_bytes(r.stdout);(o/(name+'.stderr')).write_bytes(r.stderr)
 receipt['runs'].append({'name':name,'command':cmd,'exit':r.returncode});(o/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(name,r.returncode,r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace'),flush=True)
 if r.returncode:raise SystemExit(r.returncode)
