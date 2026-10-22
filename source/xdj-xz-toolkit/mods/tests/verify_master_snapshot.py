"""Run the actual connector snapshot publication with controlled source/observer boundaries."""
from pathlib import Path
import argparse,hashlib,json,subprocess

p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
mods=Path(__file__).resolve().parents[1]
def linux(path):
    s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[mods/'extra_decks.c',mods/'extra_decks.h',mods/'extra_player.h',mods/'sync_master.h',mods/'shell_runtime_core.c',mods/'shell_runtime.h',mods/'tests/test_master_snapshot.c']
receipt={'inputs':{str(f):sha(f) for f in inputs},'runs':[],'controller_contacted':False}
failed=False
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=out/('snapshot-'+arch)
    flags=['-std=c11','-O1','-g','-UNDEBUG','-ffunction-sections','-fdata-sections','-I'+linux(mods)]
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=softfp']
    commands=[('compile',[cc,*flags,linux(inputs[-1]),linux(mods/'shell_runtime_core.c'),'-pthread','-Wl,--gc-sections','-o',linux(binary)]),
              ('run',(['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)])]
    for kind,cmd in commands:
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=120)
        (out/(arch+'-'+kind+'.stdout')).write_bytes(r.stdout);(out/(arch+'-'+kind+'.stderr')).write_bytes(r.stderr)
        receipt['runs'].append({'arch':arch,'kind':kind,'command':cmd,'exit':r.returncode})
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(arch,kind,r.returncode,r.stdout.decode(errors='replace')[-300:],flush=True)
        if kind=='compile' and r.returncode:raise SystemExit(r.stderr.decode(errors='replace'))
        failed|=bool(r.returncode)
assert receipt['inputs']=={str(f):sha(f) for f in inputs}
raise SystemExit(int(failed))
