"""Rebuild the standalone Python backend without changing the paired ARM runtime."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import uuid

APP=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--toolkit',type=Path,default=APP/'source/xdj-xz-toolkit' if (APP/'source/xdj-xz-toolkit').is_dir() else APP.parent/'xdj-xz-toolkit')
parser.add_argument('--quad-profile-sha256')
args=parser.parse_args()
toolkit=args.toolkit.resolve()
resources=APP/'resources'
if not (resources/'runtime/manifest.json').is_file():raise ValueError('Prepare the paired runtime before building the backend')

sys.path.insert(0,str(toolkit))
from builder.quad_payload import verified_files
quad=verified_files(resources,args.quad_profile_sha256)

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

def windows_identity(temporary):
    """Version resource and app icon for the service exe. Antivirus heuristics weigh a PyInstaller
    bootloader with no publisher metadata and the stock PyInstaller icon heavily."""
    config=json.loads((APP/'src-tauri/tauri.conf.json').read_text(encoding='utf-8'))
    version=config['version'];bundle=config['bundle']
    numbers=tuple((list(map(int,re.findall(r'\d+',version)))+[0,0,0,0])[:4])
    strings={'CompanyName':bundle['publisher'],'FileDescription':config['productName']+' background service',
        'FileVersion':version,'InternalName':'xz-mods-service','LegalCopyright':bundle['copyright'],
        'OriginalFilename':'xz-mods-service.exe','ProductName':config['productName'],'ProductVersion':version}
    record=Path(temporary)/'version.txt'
    record.write_text('VSVersionInfo(ffi=FixedFileInfo(filevers=%r,prodvers=%r,mask=0x3f,flags=0x0,OS=0x40004,fileType=0x1,'
        'subtype=0x0,date=(0,0)),kids=[StringFileInfo([StringTable("040904B0",[%s])]),VarFileInfo([VarStruct("Translation",[1033,1200])])])\n'
        %(numbers,numbers,','.join('StringStruct(%r,%r)'%item for item in strings.items())),encoding='utf-8')
    return ['--version-file',str(record),'--icon',str(APP/'src-tauri/icons/icon.ico')]

with tempfile.TemporaryDirectory(prefix='xz-backend-package-') as temporary:
    pin=Path(temporary)/'quad-profile.sha256'
    pin.write_text(args.quad_profile_sha256 if quad else '')
    identity=windows_identity(temporary) if os.name=='nt' else []
    subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir','--debug=noarchive','--noupx',*identity,
        '--name','xz-mods-service','--distpath',temporary+'/dist','--workpath',temporary+'/work','--specpath',temporary,
        '--add-data',str(pin)+':builder','--paths',str(toolkit),'--add-data',str(toolkit/'builder/models.json')+':builder',
        str(toolkit/'builder/entry.py')],check=True,env={**os.environ,'PYINSTALLER_CONFIG_DIR':temporary+'/cache'})
    destination=resources/'backend'
    incoming=resources/('.backend-new-'+uuid.uuid4().hex)
    backup=resources/('.backend-old-'+uuid.uuid4().hex)
    shutil.copytree(Path(temporary)/'dist/xz-mods-service',incoming,symlinks=True)
    for path in (incoming,destination,backup):
        if not path.resolve().is_relative_to(resources.resolve()) or path.is_symlink() or path.is_junction():
            raise ValueError('Backend staging path escaped the resource directory')
    if destination.exists():
        if not (destination/('xz-mods-service.exe' if os.name=='nt' else 'xz-mods-service')).is_file():raise ValueError('Existing backend folder is not a generated builder bundle')
        destination.rename(backup)
    try:incoming.rename(destination)
    except BaseException:
        if backup.exists():backup.rename(destination)
        raise
    finally:
        for owned in (incoming,backup):
            if owned.exists():shutil.rmtree(owned)
