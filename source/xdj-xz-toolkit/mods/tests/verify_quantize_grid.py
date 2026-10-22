"""Verify the existing native settings read seam with synthetic memory, host and ARM."""
from pathlib import Path
import argparse,hashlib,json,subprocess

p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
mods=Path(__file__).resolve().parents[1]
def linux(path):
    s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[mods/'shell_model.c',mods/'shell_model.h',mods/'shell/shell.h',mods/'tests/test_quantize_grid.c']
receipt={'inputs':{str(f):sha(f) for f in inputs},'runs':[],'controller_contacted':False}
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=out/('quantize-'+arch)
    flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-I'+linux(mods)]
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=softfp']
    commands=[('compile',[cc,*flags,linux(inputs[0]),linux(inputs[-1]),'-Wl,--gc-sections','-o',linux(binary)]),
              ('run',(['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)])]
    for kind,cmd in commands:
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=120)
        (out/(arch+'-'+kind+'.stdout')).write_bytes(r.stdout);(out/(arch+'-'+kind+'.stderr')).write_bytes(r.stderr)
        receipt['runs'].append({'arch':arch,'kind':kind,'command':cmd,'exit':r.returncode})
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(arch,kind,r.returncode,r.stdout.decode(errors='replace')[-500:],r.stderr.decode(errors='replace')[-1000:],flush=True)
        if r.returncode:raise SystemExit(r.returncode)
assert receipt['inputs']=={str(f):sha(f) for f in inputs}
