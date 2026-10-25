"""Exercise the real support worker against an EOF-then-stall helper; no network."""
from pathlib import Path
import argparse,json,os,subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig',required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--wsl-distro',default='Ubuntu')
    args=parser.parse_args();root=Path(__file__).resolve().parents[1];out=args.out.resolve()
    out.mkdir(parents=True,exist_ok=False)
    def linux(path):
        value=path.resolve().as_posix()
        return '/mnt/'+value[0].lower()+value[2:] if os.name=='nt' else value
    prefix=['wsl','-d',args.wsl_distro,'--'] if os.name=='nt' else []
    helper=out/'eof-helper.sh';helper.write_bytes(b'#!/bin/sh\nexec 1>&- 2>&-\nexec sleep 5\n')
    subprocess.run(prefix+['chmod','+x',linux(helper)],check=True)
    records=[]
    for arch in ('host','arm'):
        exe=out/('worker-'+arch)
        files=[root/'tests/test_support_worker.c',root/'support/native_support.c']
        command=(prefix+['gcc','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie',*[linux(p) for p in files],'-o',linux(exe)] if arch=='host' else
                 [args.zig,'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9',*[str(p) for p in files],'-o',str(exe)])
        command+=['-O1','-g','-UNDEBUG','-std=c11','-Wall','-Wextra','-Werror','-pthread','-DXZ_SUPPORT_WORKER_TIMEOUT_MS=150']
        subprocess.run(command,check=True)
        run=prefix+(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(exe),linux(helper),linux(out/('state-'+arch))]
        result=subprocess.run(run,capture_output=True,timeout=10)
        (out/(arch+'.log')).write_bytes(result.stdout+result.stderr)
        records.append({'arch':arch,'compile':command,'run':run,'exit':result.returncode})
        assert result.returncode==0,result.stderr
    (out/'receipt.json').write_text(json.dumps(records,indent=2)+'\n')
    print('PASS host ASan/UBSan and ARM support worker')

if __name__=='__main__':main()
