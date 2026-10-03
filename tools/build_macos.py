"""Build a private, self-contained macOS app and disk image from paired sources."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

APP = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    subprocess.run([str(item) for item in args], check=True, **kwargs)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(toolkit, bundle):
    resources = APP / 'resources'
    resources.mkdir(exist_ok=True)
    manifest = json.loads((bundle / 'manifest.json').read_text())
    for name, expected in manifest['files'].items():
        path = (bundle / name).resolve()
        if not path.is_relative_to(bundle.resolve()) or digest(path) != expected:
            raise ValueError('Runtime bundle failed verification: ' + name)
    runtime = resources / 'runtime'
    runtime.mkdir(exist_ok=True)
    for name in ('libxz-mods.so', 'libxz-receiver.so', 'xz-doom', 'xz-updater', 'xz-runtime-smoke', 'ota-trust.json', 'settings-schema.json'):
        shutil.copyfile(bundle / name, runtime / name)
    record = {'firmware': 'XDJ-XZ 1.26', 'profile': 'experimental', 'hardware_qualified': False,
              'prepared_formats': ['overcue-stems/4', 'stemd-cache/1'],
              'prepared_containers': ['OVPGZ001', 'OVPGZ003'], 'stem_page_codecs': ['zlib', 'flac-96k'],
              'bundle_manifest_sha256': digest(bundle / 'manifest.json'),
              'source_repository': manifest['source_repository'], 'source_directory': manifest['source_directory'],
              'source_commit': manifest['source_commit'], 'vjtools_connection': True, 'vjtools_required': False}
    for field, name in (('runtime_sha256', 'libxz-mods.so'), ('receiver_sha256', 'libxz-receiver.so'),
                        ('doom_sha256', 'xz-doom'), ('ota_updater_sha256', 'xz-updater'),
                        ('ota_runtime_smoke_sha256', 'xz-runtime-smoke'), ('settings_schema_sha256', 'settings-schema.json')):
        record[field] = digest(runtime / name)
    (runtime / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    shutil.copyfile(bundle / 'bootstrap.sh', resources / 'bootstrap.sh')
    shutil.copytree(bundle / 'branding', resources / 'branding', dirs_exist_ok=True)
    shutil.copytree(bundle / 'licenses', resources / 'licenses', dirs_exist_ok=True)
    shutil.copytree(bundle.parent / 'ota-release', resources / 'ota-release', dirs_exist_ok=True)
    source = resources / 'source/xdj-xz-toolkit'
    source.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(bundle / 'source.zip') as archive:
        archive.extractall(source)  # The private input manifest verified every archive byte.
    shutil.copytree(toolkit / 'builder', source / 'builder', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    import certifi
    shutil.copyfile(certifi.where(), resources / 'cacert.pem')
    for name in ('certifi', 'cryptography', 'cffi', 'pyinstaller', 'pycdlib', 'inflate64'):
        distribution = importlib.metadata.distribution(name)
        for item in distribution.files or []:
            if any(word in item.name.lower() for word in ('license', 'copying', 'notice')):
                original = Path(distribution.locate_file(item))
                if original.is_file():
                    target = resources / 'licenses/dependencies' / name / str(item).replace('..', '_')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(original, target)
            if name == 'certifi' and str(item).startswith('certifi/'):
                target = resources / 'licenses/source' / str(item)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(distribution.locate_file(item), target)
    inference = resources / 'inference'
    inference.mkdir(exist_ok=True)
    for name in ('inference.py', 'overcue_roles.py', 'overcue_flac_pack.py', 'grouped_stems.py', 'models.json'):
        shutil.copyfile(toolkit / 'builder' / name, inference / name)
    # C helpers use the same source decoder as the ARM runtime, compiled for this Mac.
    compiler = '/usr/bin/clang'
    run(compiler, '-O2', toolkit / 'builder/audio_helper.c', '-I', toolkit / 'mods/audio/vendor/dr_libs',
        '-o', resources / 'xz-audio-helper')
    run(compiler, '-O2', '-std=c11', '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS',
        toolkit / 'builder/overcue_check.c', toolkit / 'builder/overcue_windows.c',
        toolkit / 'mods/audio/stem_decode.c', toolkit / 'mods/audio/vendor/miniz/miniz_tinfl.c',
        toolkit / 'mods/audio/vendor/sha256/sha256.c', '-lm', '-o', resources / 'xz-overcue-check')
    arch = 'aarch64' if platform.machine() == 'arm64' else 'x86_64'
    asset = f'uv-{arch}-apple-darwin.tar.gz'
    url = 'https://github.com/astral-sh/uv/releases/download/0.11.33/' + asset
    checksum = urllib.request.urlopen(url + '.sha256', timeout=60).read().decode().split()[0]
    if len(checksum) != 64 or any(c not in '0123456789abcdef' for c in checksum):
        raise ValueError('Invalid upstream uv checksum')
    with tempfile.TemporaryDirectory(prefix='xz-uv-') as temporary:
        archive = Path(temporary) / asset
        archive.write_bytes(urllib.request.urlopen(url, timeout=60).read())
        if digest(archive) != checksum:
            raise ValueError('uv release checksum mismatch')
        with tarfile.open(archive) as tar:
            members = [m for m in tar.getmembers() if m.isfile() and Path(m.name).name == 'uv']
            if len(members) != 1:
                raise ValueError('Unexpected uv archive')
            (resources / 'uv').write_bytes(tar.extractfile(members[0]).read())
    (resources / 'uv').chmod(0o755)
    (resources / 'uv-source.json').write_text(json.dumps({'version': '0.11.33', 'url': url,
        'archive_sha256': checksum, 'executable_sha256': digest(resources / 'uv')}, indent=2) + '\n')
    run(sys.executable, APP / 'tools/build_backend.py', '--toolkit', toolkit)
    # PyInstaller's runtime dylibs and all helper binaries require an ad-hoc signature on ARM.
    for path in sorted(resources.rglob('*')):
        if not path.is_file() or path.is_symlink() or any(p.suffix == '.framework' for p in path.parents):
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
            run('/usr/bin/codesign', '--force', '--sign', '-', path)
    for framework in resources.rglob('*.framework'):
        if not framework.is_symlink():
            run('/usr/bin/codesign', '--force', '--sign', '-', framework)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolkit', type=Path, required=True)
    parser.add_argument('--runtime-bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('Build the Mac package on macOS.')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prepare(args.toolkit.resolve(), args.runtime_bundle.resolve())
    run(sys.executable, APP / 'tools/verify_release_source.py')
    run(sys.executable, '-m', 'unittest', 'builder.tests.test_macos_host', 'builder.tests.test_fat_publication',
        'builder.tests.test_cache', 'builder.tests.test_managed_usb', 'builder.tests.test_stem_batch',
        'builder.tests.test_stem_library', 'builder.tests.test_games',
        'builder.tests.test_layered_waveforms', '-q', cwd=args.toolkit.resolve())
    icon = APP / 'src-tauri/icons'
    from PIL import Image
    Image.open(icon / 'icon.ico').convert('RGBA').resize((1024, 1024)).save(icon / 'app-icon.png')
    run('npx', '--yes', '@tauri-apps/cli@2.12.0', 'icon', icon / 'app-icon.png', '--output', icon)
    run('cargo', 'test', '--manifest-path', APP / 'src-tauri/Cargo.toml', '--locked',
        env={**os.environ, 'CARGO_BUILD_JOBS': '2'})
    run('npx', '--yes', '@tauri-apps/cli@2.12.0', 'build', '--config',
        APP / 'src-tauri/tauri.macos.conf.json', '--bundles', 'app,dmg', cwd=APP,
        env={**os.environ, 'CARGO_BUILD_JOBS': '2', 'MACOSX_DEPLOYMENT_TARGET': '14.0', 'CI': 'true'})
    app = APP / 'src-tauri/target/release/bundle/macos/XZ Mods.app'
    for path in (app, *app.glob('Contents/Resources/resources/backend/_internal/*.dylib')):
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', path)
    for disk in (APP / 'src-tauri/target/release/bundle/dmg').glob('*.dmg'):
        shutil.copyfile(disk, output / disk.name.replace(' ', '-'))
    with tarfile.open(output / ('XZ-Mods-' + platform.machine() + '.app.tar.gz'), 'w:gz') as tar:
        tar.add(app, arcname=app.name)
    run(sys.executable, APP / 'tools/verify_backend.py', '--resources', app / 'Contents/Resources/resources',
        '--toolkit', args.toolkit.resolve(), '--evidence', output / 'backend-check.json')
    run(sys.executable, APP / 'tools/verify_stem_batch.py', '--resources', app / 'Contents/Resources/resources',
        '--toolkit', args.toolkit.resolve(), '--fixture-root', output / 'batch-fixture',
        '--evidence', output / 'batch-check.json')
    shutil.rmtree(output / 'batch-fixture')
    run(sys.executable, APP / 'tools/verify_macos.py', '--app', app, '--evidence', output / 'macos-check.json')
    if platform.machine() == 'arm64':
        run(sys.executable, APP / 'tools/verify_macos_models.py', '--resources', app / 'Contents/Resources/resources',
            '--toolkit', args.toolkit.resolve(), '--evidence', output / 'models-check.json')
    checks = [digest(p) + '  ' + p.name for p in sorted(output.iterdir()) if p.is_file()]
    (output / 'SHA256SUMS.txt').write_text('\n'.join(checks) + '\n')
    print('Private Mac packages: ' + str(output))


if __name__ == '__main__':
    main()
