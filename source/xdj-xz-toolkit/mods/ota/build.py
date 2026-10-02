"""Build the immutable XZ runtime OTA verifier for the firmware-1.26 ARM ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'doom'))
from build import inspect
def build(zig,output):
    output.mkdir(parents=True,exist_ok=True)
    source=json.loads((ROOT/'source.json').read_text())
    for name,digest in source['files'].items():
        if hashlib.sha256((ROOT/'vendor'/name).read_bytes()).hexdigest()!=digest:raise ValueError('Pinned verifier source changed: '+name)
    binary=output/'xz-updater'
    subprocess.run([str(zig.resolve()),'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9',
                    '-std=gnu11','-O2','-s','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                    '-I'+str(ROOT/'vendor'),str(ROOT/'updater.c'),str(ROOT/'vendor/monocypher.c'),
                    str(ROOT/'vendor/monocypher-ed25519.c'),str(ROOT.parent/'audio/vendor/sha256/sha256.c'),'-o',str(binary)],check=True)
    result=inspect(binary);result['trust_key_sha256']=hashlib.sha256((ROOT/'trusted-key.hex').read_bytes()).hexdigest()
    (output/'ota-build.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.zig,a.output)
