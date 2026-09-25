"""Rebuild the standalone Python backend without changing the paired ARM runtime."""
import argparse
import hashlib
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

APP=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--toolkit',type=Path,default=APP.parent/'xdj-xz-toolkit')
args=parser.parse_args()
toolkit=args.toolkit.resolve()
resources=APP/'resources'
if not (resources/'runtime/manifest.json').is_file():raise ValueError('Prepare the paired runtime before building the backend')

source=APP/'third_party/inflate64-1.0.4.tar.gz'
expected='b398c686960c029777afc0ed281a86f66adb956cfc3fbf6667cc6453f7b407ce'
if hashlib.sha256(source.read_bytes()).hexdigest()!=expected:raise ValueError('inflate64 source archive identity mismatch')
distribution=importlib.metadata.distribution('inflate64')
if distribution.version!='1.0.4':raise ValueError('Build with inflate64 1.0.4')
licenses=resources/'licenses'
(licenses/'source').mkdir(parents=True,exist_ok=True)
shutil.copyfile(source,licenses/'source'/source.name)
for entry in distribution.files or []:
    if entry.name=='COPYING':
        path=Path(distribution.locate_file(entry))
        if path.is_file():
            target=licenses/'dependencies/inflate64'/str(entry)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,target)

with tempfile.TemporaryDirectory(prefix='xz-backend-package-') as temporary:
    subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir','--debug=noarchive','--noupx',
        '--name','xz-mods-service','--distpath',temporary+'/dist','--workpath',temporary+'/work','--specpath',temporary,
        '--paths',str(toolkit),'--add-data',str(toolkit/'builder/models.json')+':builder',
        str(toolkit/'builder/entry.py')],check=True,env={**os.environ,'PYINSTALLER_CONFIG_DIR':temporary+'/cache'})
    destination=resources/'backend'
    incoming=resources/('.backend-new-'+uuid.uuid4().hex)
    backup=resources/('.backend-old-'+uuid.uuid4().hex)
    shutil.copytree(Path(temporary)/'dist/xz-mods-service',incoming)
    for path in (incoming,destination,backup):
        if not path.resolve().is_relative_to(resources.resolve()) or path.is_symlink() or path.is_junction():
            raise ValueError('Backend staging path escaped the resource directory')
    if destination.exists():
        if not (destination/'xz-mods-service.exe').is_file():raise ValueError('Existing backend folder is not a generated builder bundle')
        destination.rename(backup)
    try:incoming.rename(destination)
    except BaseException:
        if backup.exists():backup.rename(destination)
        raise
    finally:
        for owned in (incoming,backup):
            if owned.exists():shutil.rmtree(owned)
