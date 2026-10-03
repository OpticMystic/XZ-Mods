"""Verify the exact public source snapshot and its paired prepared runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tomllib

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
version = config['version']
package = tomllib.loads((app / 'src-tauri/Cargo.toml').read_text())['package']
locked = tomllib.loads((app / 'src-tauri/Cargo.lock').read_text())['package']
if package['version'] != version or next(p['version'] for p in locked if p['name'] == package['name']) != version:
    raise ValueError('Rust application versions differ from the release')
labels = re.findall(r'data-app-version>([^<]+)', (app / 'ui/index.html').read_text())
if not labels or any(label != version for label in labels):
    raise ValueError('Bundled app contains an outdated visible version label')
backend = re.search(r"^VERSION='([^']+)'", (source / 'builder/service.py').read_text(), re.M)
if not backend or backend[1] != version:
    raise ValueError('Python backend version differs from the release')
notes = json.loads((app / 'release-notes.json').read_text())['releases']
if not notes or notes[0]['version'] != version:
    raise ValueError('Current release notes differ from the application')
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
