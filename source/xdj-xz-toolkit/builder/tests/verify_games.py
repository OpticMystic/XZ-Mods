"""Download the real publisher files through the frozen backend and verify readback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend',type=Path,required=True)
parser.add_argument('--data',type=Path,required=True)
parser.add_argument('--evidence',type=Path,required=True)
args=parser.parse_args()
env={**os.environ,'XZ_BUILDER_DATA':str(args.data.resolve())}
env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
if os.name=='nt':env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
results=[]
for game in ('doom','chex','freedoom','myhouse'):
    request=json.dumps({'method':'download_game','game':game})
    process=subprocess.run([str(args.backend.resolve())],input=request,capture_output=True,text=True,encoding='utf8',env=env,timeout=600)
    response=json.loads(process.stdout.splitlines()[-1])
    if not response.get('ok'):raise RuntimeError(response.get('error',process.stderr))
    folder=Path(response['result']['folder']);manifest=json.loads((folder/'game.json').read_text())
    for name,digest in manifest['files'].items():
        assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==digest,name
    result={**response['result'],'sha256':manifest['files'],'readback_verified':True,'hardware_verified':False}
    results.append(result);print(game,'PASS',flush=True)
args.evidence.parent.mkdir(parents=True,exist_ok=True)
args.evidence.write_text(json.dumps({'format':'xz-games-acceptance/1','backend':str(args.backend.resolve()),'backend_sha256':hashlib.sha256(args.backend.read_bytes()).hexdigest(),'commercial_doom2_downloaded':False,'results':results},indent=2)+'\n')
