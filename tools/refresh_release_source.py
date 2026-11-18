"""Rebuild source/xdj-xz-toolkit and release-source.json for the current app version.

The snapshot is the exact source of what the app ships: the builder (and the three vendor files
prepare_resources imports) from one toolkit commit, and the native mods/ and vendor/ trees from
resources/runtime/source.zip, the archive paired with the prepared runtime. Run it after the version
bump and before building the backend, then build with --toolkit source/xdj-xz-toolkit.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

APP = Path(__file__).resolve().parents[1]
BUILDER_PATHS = ['LICENSE', 'builder', 'vendor/tools/__init__.py', 'vendor/tools/xz_firmware/__init__.py',
                 'vendor/tools/xz_firmware/mods_bundle.py']
NATIVE_ROOTS = ('mods/', 'vendor/')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--toolkit', type=Path, default=APP.parent / 'xdj-xz-toolkit', help='xdj-xz-toolkit git checkout')
parser.add_argument('--commit', required=True, help='Toolkit commit the backend is built from')
args = parser.parse_args()


def sha(data):
    return hashlib.sha256(data).hexdigest()


version = json.loads((APP / 'src-tauri/tauri.conf.json').read_text())['version']
commit = subprocess.check_output(['git', '-C', str(args.toolkit), 'rev-parse', '--verify', args.commit + '^{commit}'], text=True).strip()
archive = subprocess.run(['git', '-C', str(args.toolkit), 'archive', '--format=tar', commit, *BUILDER_PATHS],
                         check=True, capture_output=True).stdout
runtime = APP / 'resources/runtime'
manifest = json.loads((runtime / 'manifest.json').read_text())
native = runtime / 'source.zip'

with tempfile.TemporaryDirectory(dir=APP / 'source') as temporary:
    staged = Path(temporary) / 'xdj-xz-toolkit'
    staged.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(staged, filter='data')
    with zipfile.ZipFile(native) as zipped:
        for info in zipped.infolist():
            if info.is_dir():
                continue
            if not info.filename.startswith(NATIVE_ROOTS):
                raise ValueError('Unexpected path in the native source archive: ' + info.filename)
            target = (staged / info.filename).resolve()
            if not target.is_relative_to(staged.resolve()) or target.exists():
                raise ValueError('Native source path overlaps the builder snapshot: ' + info.filename)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zipped.read(info))
    backend = re.search(r"^VERSION='([^']+)'", (staged / 'builder/service.py').read_text(), re.M)
    if not backend or backend[1] != version:
        raise ValueError(f'Toolkit {commit[:7]} reports backend version {backend and backend[1]}, the app is {version}')
    destination = APP / 'source/xdj-xz-toolkit'
    if destination.exists():
        shutil.rmtree(destination)
    staged.rename(destination)

files = {path.relative_to(destination).as_posix(): sha(path.read_bytes())
         for path in sorted(destination.rglob('*')) if path.is_file()}
record_path = APP / 'release-source.json'
record = json.loads(record_path.read_text())
record.pop('files', None)
record.update(version=version, local_build=True,
              matching_archive=f'XZ-Mods-{version}-source.zip (local, not published)',
              runtime_sha256=manifest['runtime_sha256'], receiver_sha256=manifest['receiver_sha256'],
              native_source_archive_sha256=sha(native.read_bytes()),
              native_source={'repository': manifest['source_repository'], 'directory': manifest['source_directory'],
                             'commit': manifest['source_commit'], 'paths': [root.rstrip('/') for root in NATIVE_ROOTS]},
              builder_source={'repository': 'https://github.com/OpticMystic/xdj-xz-toolkit', 'commit': commit, 'paths': BUILDER_PATHS},
              files=files)
record_path.write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({'version': version, 'files': len(files), 'builder_commit': commit,
                  'native_commit': manifest['source_commit'], 'snapshot_sha256': sha(json.dumps(files, sort_keys=True).encode())}, indent=2))
