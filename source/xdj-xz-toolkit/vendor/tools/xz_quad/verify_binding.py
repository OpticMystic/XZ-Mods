"""Run actual bridge identity tests with in-memory I/O; never access a device."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess

ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--arm',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
for name in ['bridge.c','contract.h','port_layout.h','test_binding.c']:shutil.copyfile(ROOT/name,out/name)
crypto=ROOT.parents[2]/'mods/audio/vendor/sha256'
for name in ['sha256.c','sha256.h']:shutil.copyfile(crypto/name,out/name)
contract=(out/'contract.h').read_text()
for pin,data in [('RBP_SHA',b'synthetic application'),('RUNTIME_SHA',b'synthetic runtime'),('RECEIVER_SHA',b'synthetic receiver')]:
    contract=re.sub(r'(#define '+pin+r' ")[^"]+',r'\g<1>'+hashlib.sha256(data).hexdigest(),contract)
(out/'contract.h').write_text(contract)
cc='arm-linux-gnueabi-gcc' if a.arm else 'gcc'
flags=['-std=c11','-Wall','-Wextra','-Werror','-O1','-g']+(['-static','-mcpu=cortex-a9','-mfloat-abi=soft'] if a.arm else ['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie'])
binary=out/'test-binding';commands=[[cc,*flags,str(out/'test_binding.c'),str(out/'sha256.c'),'-o',str(binary)],(['qemu-arm','-cpu','cortex-a9'] if a.arm else [])+[str(binary)]]
runs=[]
for stage,command in zip(['compile','run'],commands):
    done=subprocess.run(command,capture_output=True,timeout=120);(out/(stage+'.log')).write_bytes(done.stdout+done.stderr)
    runs.append({'command':command,'exit':done.returncode});print(stage,done.returncode)
    if done.returncode:break
(out/'receipt.json').write_text(json.dumps({'arm':a.arm,'hardware':False,'source_sha256':hashlib.sha256((ROOT/'bridge.c').read_bytes()).hexdigest(),'runs':runs},indent=2)+'\n')
raise SystemExit(any(x['exit'] for x in runs))
