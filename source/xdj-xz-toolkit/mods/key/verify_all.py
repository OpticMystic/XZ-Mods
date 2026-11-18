"""Rerun the key revision's musical, audio, controls and browser acceptance."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parent
def linux_path(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+str(path)[2:].replace('\\','/')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    results={}
    for script in ('verify_music.py','verify_runtime.py','verify.py','verify_controls.py','verify_browser.py'):
        if script in ('verify_controls.py','verify_browser.py') and os.name=='nt':
            command=['wsl','-d','Ubuntu','--exec','python3',linux_path(ROOT/script)]
            if script=='verify_browser.py':command+=['--output',linux_path(args.output/'browser')]
        else:
            command=[sys.executable,str(ROOT/script)]
            if script=='verify_browser.py':command+=['--output',str((args.output/'browser').resolve())]
            elif script!='verify_controls.py':command+=['--zig',str(args.zig.resolve())]
        run=subprocess.run(command,capture_output=True,text=True)
        (args.output/(script+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        results[script]={'passed':run.returncode==0,'log':script+'.log'}
        print(script+': '+('PASS' if run.returncode==0 else 'FAIL'),flush=True)
        (args.output/'results.json').write_text(json.dumps(results,indent=2))
        if run.returncode:print(run.stdout+run.stderr);run.check_returncode()
