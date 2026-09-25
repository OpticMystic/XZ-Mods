"""Exercise the packaged Deflate64 extractor from a fresh app-data directory."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--resources',type=Path,required=True)
parser.add_argument('--cached-source',type=Path,required=True)
parser.add_argument('--firmware',type=Path,required=True)
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='xz-boot-check-') as temporary:
    data=Path(temporary).resolve()
    if not data.is_relative_to(Path(tempfile.gettempdir()).resolve()):raise ValueError('Temporary directory escaped the system temp root')
    downloads=data/'boot-support/downloads'
    downloads.mkdir(parents=True)
    for name in ('pioneerdj_xdj_xz.tar.bz2.00.zip','pioneerdj_xdj_xz.tar.bz2.01.zip'):
        shutil.copyfile(args.cached_source/name,downloads/name)
    env={**os.environ,'XZ_BUILDER_DATA':str(data),'XZ_BUILDER_RESOURCES':str(args.resources.resolve())}
    process=subprocess.run([str(args.resources.resolve()/'backend/xz-mods-service.exe')],
        input=json.dumps({'method':'inspect_firmware','firmware':str(args.firmware.resolve())}),
        text=True,capture_output=True,env=env,timeout=180)
    if process.returncode:raise RuntimeError(process.stderr[-2000:])
    result=json.loads(process.stdout.splitlines()[-1])
    if not result.get('ok') or not result['result']['key_verified_against_input']:
        raise RuntimeError(str(result))
    if not (data/'boot-support/aes256.key').is_file():raise RuntimeError('Fresh boot support was not saved')
print('Fresh packaged boot-support extraction and official firmware inspection passed')
