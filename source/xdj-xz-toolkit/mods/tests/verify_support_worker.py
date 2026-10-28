"""Exercise the real support worker against an EOF-then-stall helper and a scripted report helper; no network."""
from pathlib import Path
import argparse,json,os,subprocess

REPORT_HELPER = b'''#!/bin/sh
dir=$(dirname "$0")
mode=$(cat "$dir/report-mode")
cat > "$dir/last-request"
id=$(sed -n 's/^report_id=//p' "$dir/last-request")
case "$mode" in
sent) printf 'SAVED_FIRST %s\\nSENT %s R-4F7K-2M9Q https://xz-mods-support.will-026.workers.dev/r/R-4F7K-2M9Q\\n' "$id" "$id";;
unsaved) echo "XZ_SUPPORT_ERROR CONNECT_FAILED" >&2; printf 'UNSAVED READ_ONLY OFFLINE\\n'; exit 1;;
saved) printf 'SAVED_FIRST %s\\nSAVED %s OFFLINE\\n' "$id" "$id";;
hang) printf 'SAVED_FIRST %s\\n' "$id"; exec sleep 5;;
esac
'''
IDENTITY = b'{"schema":"xz-loader-identity/1","loader_id":"L-4F7K-2M9Q","loader_key":"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"}\n'

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
        scratch=out/('report-'+arch);(scratch/'usb/VJ.Tools').mkdir(parents=True);(scratch/'bare').mkdir()
        (scratch/'usb/VJ.Tools/loader-identity.json').write_bytes(IDENTITY)
        report=scratch/'report-helper.sh';report.write_bytes(REPORT_HELPER)
        subprocess.run(prefix+['chmod','+x',linux(report)],check=True)
        exe=out/('worker-'+arch)
        files=[root/'tests/test_support_worker.c',root/'support/native_support.c',root/'support/loader_identity.c']
        command=(prefix+['gcc','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie',*[linux(p) for p in files],'-o',linux(exe)] if arch=='host' else
                 [args.zig,'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9',*[str(p) for p in files],'-o',str(exe)])
        command+=['-O1','-g','-UNDEBUG','-std=c11','-Wall','-Wextra','-Werror','-pthread','-DXZ_SUPPORT_WORKER_TIMEOUT_MS=1500']
        subprocess.run(command,check=True)
        run=prefix+(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(exe),linux(helper),linux(out/('state-'+arch)),linux(report),linux(scratch)]
        result=subprocess.run(run,capture_output=True,timeout=60)
        (out/(arch+'.log')).write_bytes(result.stdout+result.stderr)
        records.append({'arch':arch,'compile':command,'run':run,'exit':result.returncode})
        assert result.returncode==0,result.stdout+result.stderr
    (out/'receipt.json').write_text(json.dumps(records,indent=2)+'\n')
    print('PASS host ASan/UBSan and ARM support worker, including REPORT AN ISSUE')

if __name__=='__main__':main()
