"""Freeze a locally verifiable 8-bit trial without changing the shared source snapshot.

Desktop source comes from a named commit. Native source comes from the candidate's
exact archive; the accepted audio binary remains unchanged. No install or USB write.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import tarfile
import zipfile

APP = Path(__file__).resolve().parents[1]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--commit', required=True)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--resources', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'XZ Mods/resources')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    bundle, baseline, output = a.bundle.resolve(), a.resources.resolve(), a.output.resolve()
    manifest = json.loads((bundle / 'manifest.json').read_text(encoding='utf-8'))
    if sha(baseline / 'runtime/libxz-mods.so') != manifest['runtime_sha256']:
        raise SystemExit('Accepted audio baseline changed; requalify before staging.')
    output.mkdir(parents=True, exist_ok=False)
    commit = subprocess.check_output(['git', '-C', str(APP), 'rev-parse', a.commit + '^{commit}'], text=True).strip()
    paths = ['ui', 'src-tauri', 'tools', 'source', 'README.md', 'BUILDING.md', 'CHANGELOG.md',
             'release-notes.json', 'release-source.json', 'LICENSE', '.gitignore']
    archive = subprocess.check_output(['git', '-C', str(APP), 'archive', commit, *paths])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        excluded = ('source/xdj-xz-toolkit/mods', 'source/xdj-xz-toolkit/vendor')
        members = [entry for entry in tar.getmembers() if not any(entry.name == root or entry.name.startswith(root + '/') for root in excluded)]
        tar.extractall(output, members=members, filter='data')
    shutil.copytree(baseline, output / 'resources')
    shutil.copytree(bundle, output / 'resources/runtime', dirs_exist_ok=True)
    installed_manifest = dict(manifest, bundle_manifest_sha256=sha(bundle / 'manifest.json'))
    (output / 'resources/runtime/manifest.json').write_text(json.dumps(installed_manifest, indent=2) + '\n', encoding='utf-8')
    for destination in [output / 'source/xdj-xz-toolkit', output / 'resources/source/xdj-xz-toolkit']:
        with zipfile.ZipFile(bundle / 'source.zip') as native:
            for entry in native.infolist():
                target = (destination / entry.filename).resolve()
                if not target.is_relative_to(destination) or not entry.filename.startswith(('mods/', 'vendor/')):
                    raise ValueError('Invalid native source member')
            native.extractall(destination)
    toolkit = output / 'source/xdj-xz-toolkit'
    record_path = output / 'release-source.json'
    record = json.loads(record_path.read_text(encoding='utf-8'))
    record.update(local_build=True, runtime_sha256=manifest['runtime_sha256'], receiver_sha256=manifest['receiver_sha256'],
                  native_source_archive_sha256=sha(bundle / 'source.zip'),
                  matching_archive='Local full-frame 8-bit experiment; not published',
                  native_source={'repository': manifest['source_repository'], 'directory': 'source/xdj-xz-toolkit',
                                 'commit': manifest['source_commit'], 'paths': ['mods', 'vendor'],
                                 'composite_snapshot': True, 'overlay_commit': commit},
                  experimental_stream_color_bits=8,
                  files={f.relative_to(toolkit).as_posix(): sha(f) for f in sorted(toolkit.rglob('*')) if f.is_file()})
    record_path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    provenance = {'desktop_commit': commit, 'native_base_archive_sha256': manifest['source_base_archive_sha256'],
                  'native_overlay': manifest['source_overlay'], 'audio_library_unchanged': True,
                  'runtime_sha256': manifest['runtime_sha256'], 'receiver_sha256': manifest['receiver_sha256'],
                  'hardware_qualified': False, 'public_published': False}
    (output / 'STREAM8-PROVENANCE.json').write_text(json.dumps(provenance, indent=2) + '\n')
    subprocess.run(['python', str(output / 'tools/verify_release_source.py')], check=True)
    print(json.dumps({'app': str(output), **provenance}, indent=2))

if __name__ == '__main__':
    main()
