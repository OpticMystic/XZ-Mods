"""Run PC waveform boundary and renderer acceptance, then compile ARM objects.

Uses synthetic native readers and analysis. No controller or USB access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--private-packet',action='append',type=Path,default=[])
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    zig=str(args.zig.resolve())
    common=[zig,'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-UNDEBUG']
    sources={
        'pc-wave':[root/'pc_control/test_wave.c',root/'ui/layered_wave.c',root/'ui/layered_wave_source.c',root/'audio/vendor/sha256/sha256.c'],
        'pc-wave-runtime':[root/'ui/test_pc_wave_runtime.c',root/'ui/layered_wave.c'],
    }
    results={}
    with tempfile.TemporaryDirectory(prefix='xz-pc-wave-') as work:
        for name,files in sources.items():
            binary=out/(name+'.exe')
            subprocess.run(common+['-ffunction-sections','-fdata-sections','-Wl,--gc-sections']+[str(p) for p in files]+['-o',str(binary)],check=True)
            command=[str(binary)]+([str(p.resolve()) for p in args.private_packet] if name=='pc-wave' else [])
            result=subprocess.run(command,cwd=work,capture_output=True,text=True)
            print(result.stdout,end='');print(result.stderr,end='');result.check_returncode();results[name]=result.stdout.strip()
        negative=subprocess.run(common+['-DNDEBUG','-c',str(root/'pc_control/test_wave.c'),'-o',str(out/'negative.o')],capture_output=True,text=True)
        if negative.returncode==0 or 'requires live assertions' not in negative.stderr:
            raise RuntimeError('PC waveform assertion gate failed')
    for name,source in (('pc-wave',root/'pc_control/wave.c'),('pc-layered-runtime',root/'ui/layered_wave_runtime.c')):
        obj=out/(name+'-arm.o')
        subprocess.run(common+['-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-mfloat-abi=softfp','-mfpu=neon','-fPIC','-fvisibility=hidden','-c',str(source),'-o',str(obj)],check=True)
        results[name+'-arm-object-sha256']=hashlib.sha256(obj.read_bytes()).hexdigest()
    results['production_sources']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'pc_control/wave.c',root/'pc_control/wave.h',root/'ui/layered_wave_runtime.c')}
    (out/'verification.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
    print('PASS assertion rejection and ARM32 soft-float object compile')


if __name__=='__main__':main()
