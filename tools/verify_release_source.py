"""Verify the exact public source snapshot and its paired prepared runtime."""
import argparse
import hashlib
import json
from pathlib import Path

app = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--resources', type=Path, default=app / 'resources')
args = parser.parse_args()
record = json.loads((app / 'release-source.json').read_text())
source = (app / record['source_directory']).resolve()
for relative, expected in record['files'].items():
    path = (source / relative).resolve()
    if not path.is_relative_to(source) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise ValueError('Public source mismatch: ' + relative)
config = json.loads((app / 'src-tauri/tauri.conf.json').read_text())
if config['version'] != record['version']:
    raise ValueError('Public source version differs from the application')
resources = args.resources.resolve()
manifest = json.loads((resources / 'runtime/manifest.json').read_text())
for field, name in [('runtime_sha256', 'libxz-mods.so'), ('receiver_sha256', 'libxz-receiver.so')]:
    actual = hashlib.sha256((resources / 'runtime' / name).read_bytes()).hexdigest()
    if actual != manifest[field]:
        raise ValueError('Prepared runtime mismatch: ' + name)
if manifest['runtime_sha256'] != record['runtime_sha256']:
    raise ValueError('Public source is paired with a different native runtime')
print(json.dumps({'version': record['version'], 'source_files_verified': len(record['files']),
                  'runtime_sha256': record['runtime_sha256'], 'matching_archive': record['matching_archive']}, indent=2))
