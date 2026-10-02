"""Install a verified native arcade bundle into existing local XZ Mods resources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import zipfile

root = Path(__file__).resolve().parent
sys.path.insert(0, str(root.parent.parent / 'vendor'))
from tools.xz_firmware.mods_bundle import load_mods_bundle
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bundle', type=Path, required=True)
parser.add_argument('--backup', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
args = parser.parse_args()
bundle = args.bundle.resolve()
manifest = load_mods_bundle(bundle)
repo = root.parents[3]
installed = Path(os.environ['LOCALAPPDATA']) / 'XZ Mods'
if not (installed / 'xz-mods-builder.exe').is_file(): raise RuntimeError('Install XZ Mods before replacing its native resources')
targets = [('toolkit', root.parent.parent / 'runtime'),
           ('builder', repo / 'apps/xz-mods-builder/resources/runtime'),
           ('installed', installed / 'resources/runtime')]
args.backup.mkdir(parents=True, exist_ok=False)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
receipt = {'bundle_runtime_sha256': manifest['files']['libxz-mods.so'], 'targets': {}, 'public_publish': False}
with zipfile.ZipFile(bundle / 'source.zip') as archive:
    for item in archive.infolist():
        name = Path(item.filename)
        if name.is_absolute() or '..' in name.parts or (item.external_attr >> 16) & 0o170000 == 0o120000: raise ValueError('Unsafe source archive')
for label, target in targets:
    target = target.resolve()
    if target.exists(): shutil.copytree(target, args.backup / label)
    target.mkdir(parents=True, exist_ok=True)
    previous = json.loads((target / 'manifest.json').read_text()) if (target / 'manifest.json').exists() else {}
    for relative in manifest['files']:
        destination = (target / relative).resolve()
        if not destination.is_relative_to(target): raise ValueError('Bundle target escaped runtime directory')
        destination.parent.mkdir(parents=True, exist_ok=True)
        incoming = destination.with_name(destination.name + '.arcade-incoming')
        shutil.copyfile(bundle / relative, incoming)
        os.replace(incoming, destination)
    if label == 'toolkit': local_manifest = dict(manifest)
    else:
        local_manifest = dict(previous, firmware='XDJ-XZ 1.26', profile='experimental', hardware_qualified=False,
            runtime_sha256=manifest['files']['libxz-mods.so'], receiver_sha256=manifest['files']['libxz-receiver.so'],
            doom_sha256=manifest['files']['xz-doom'], ota_updater_sha256=manifest['files']['xz-updater'],
            ota_runtime_smoke_sha256=manifest['files']['xz-runtime-smoke'], source_repository=manifest['source_repository'],
            source_commit=manifest['source_commit'], source_directory='packages/xdj-xz-toolkit',
            source_zip_sha256=manifest['files']['source.zip'], bundle_manifest_sha256=sha(bundle / 'manifest.json'),
            native_arcade=True, physical_arcade_controls_verified=False, track_phase_verified=False)
        resources = target.parent
        shutil.copyfile(target / 'bootstrap.sh', resources / 'bootstrap.sh')
        source = resources / 'source/xdj-xz-toolkit'
        source.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(target / 'source.zip') as archive: archive.extractall(source)
    (target / 'manifest.json').write_text(json.dumps(local_manifest, indent=2) + '\n')
    readback = {relative: sha(target / relative) for relative in manifest['files']}
    if readback != manifest['files']: raise RuntimeError('Installed native bundle differs from candidate')
    receipt['targets'][label] = {'path': str(target), 'files_verified': len(readback), 'runtime_sha256': readback['libxz-mods.so'], 'source_zip_sha256': readback['source.zip']}
receipt['installed_exe'] = str(installed / 'xz-mods-builder.exe')
receipt['installed_exe_sha256'] = sha(installed / 'xz-mods-builder.exe')
args.evidence.parent.mkdir(parents=True, exist_ok=True)
args.evidence.write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
