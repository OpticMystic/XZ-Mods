"""Package the independently runnable preview and matching source. No publishing."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--exe',type=Path)
p.add_argument('--toolkit',type=Path,default=APP.parent/'xdj-xz-toolkit' if (APP.parent/'xdj-xz-toolkit').is_dir() else APP.parents[1]/'packages/xdj-xz-toolkit')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
portable=a.output/'XZ Mods Builder';portable.mkdir()
shutil.copyfile(a.exe or APP/'src-tauri/target/debug/xz-mods-builder.exe',portable/'XZ Mods.exe')
shutil.copytree(APP/'resources',portable/'resources')
for name in ('README.md','BUILDING.md','CHANGELOG.md','LICENSE'):shutil.copyfile(APP/name,portable/name)
shutil.copytree(APP/'docs',portable/'docs')
(portable/'START HERE.txt').write_text(
    'XZ Mods Builder - developer preview\n\n'
    'Run XZ Mods.exe. VJ.Tools and Python are not required.\n'
    'Windows requires the Microsoft Edge WebView2 Runtime.\n'
    'Choose a FAT/FAT32 USB in the app; verified official firmware and boot support download automatically.\n'
    'Firmware, boot support and generated personal images are not included.\n'
    'Model downloads are separate and use the reviewed original model sources.\n'
    'Do not treat this preview as a hardware-qualified public firmware release.\n'
    'Real model execution and final XZ boot/audio/control tests remain pending.\n'
    'Explore VJ.Tools: https://vj.tools\n',encoding='utf8')
for path in portable.rglob('*'):
    if path.is_file() and (path.suffix.lower() in ('.key','.upd','.pth','.ckpt','.safetensors') or path.name in ('rbp','rbp.patched','autoexec.bin','imagedata.dat','XDJXZ_v126.zip')):
        raise ValueError('Prohibited private firmware/key/model asset in preview: '+path.name)
def digest(path):
    with path.open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()
version=json.loads((APP/'src-tauri/tauri.conf.json').read_text())['version']
manifest={'product':'XZ Mods','version':version,'stage':'developer-preview','public_release_qualified':False,
    'source_commit':subprocess.check_output(['git','-C',str(APP),'rev-parse','HEAD'],text=True).strip() if (APP/'.git').exists() else None,
    'prepared_formats':['overcue-stems/4','stemd-cache/1'],
    'runtime':json.loads((portable/'resources/runtime/manifest.json').read_text()),
    'vjtools_required':False,'python_required':False,'firmware_included':False,'keys_included':False,
    'model_weights_included':False,'files':{str(path.relative_to(portable)).replace('\\','/'):digest(path)
    for path in sorted(portable.rglob('*')) if path.is_file()}}
(portable/'release-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
archive=a.output/'XZ-Mods-Builder-preview-win64.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
    for path in sorted(portable.rglob('*')):
        if path.is_file():output.write(path,str(Path(portable.name)/path.relative_to(portable)))
source_zip=a.output/'XZ-Mods-Builder-preview-source.zip'
source_files={}
for path in (APP/'resources/source').rglob('*'):
    if path.is_file():source_files[str(path.relative_to(APP/'resources/source'))]=path
for folder in ('ui','src-tauri','tools','docs'):
    for path in (APP/folder).rglob('*'):
        relative=path.relative_to(APP)
        if path.is_file() and 'target' not in relative.parts and '__pycache__' not in relative.parts:
            source_files[str(Path('XZ-Mods')/relative)]=path
for path in (APP/'third_party').rglob('*'):
    if path.is_file():source_files[str(Path('XZ-Mods')/path.relative_to(APP))]=path
for name in ('README.md','BUILDING.md','CHANGELOG.md','LICENSE','.gitignore'):
    source_files[str(Path('XZ-Mods')/name)]=APP/name
for path in (a.toolkit/'builder').rglob('*'):
    if path.is_file() and path.suffix in ('.py','.c','.json','.md') and '__pycache__' not in path.parts:
        source_files[str(Path('xdj-xz-toolkit')/path.relative_to(a.toolkit))]=path
source_files['xdj-xz-toolkit/LICENSE']=a.toolkit/'LICENSE'
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
    for relative,path in sorted(source_files.items()):output.write(path,relative)
print(json.dumps({'portable':str(archive),'portable_sha256':digest(archive),'source':str(source_zip),
    'source_sha256':digest(source_zip),'public_release_qualified':False},indent=2))
