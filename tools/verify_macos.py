"""Verify the packaged Mac app, helper closure, real FAT image and GUI launch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import platform
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--app', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
args = parser.parse_args()
app = args.app.resolve()
resources = app / 'Contents/Resources/resources'
binary = app / 'Contents/MacOS/xz-mods-builder'
checks = {}

for name in ('backend/xz-mods-service', 'xz-audio-helper', 'xz-overcue-check', 'uv'):
    helper = resources / name
    assert helper.is_file() and os.access(helper, os.X_OK), name
    info = subprocess.check_output(['/usr/bin/file', str(helper)], text=True)
    assert 'Mach-O' in info, info
    subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(helper)], check=True)
checks['native_helper_closure'] = True
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
assert info['LSMinimumSystemVersion'] == '14.0'
assert info['NSRemovableVolumesUsageDescription'] and info['NSLocalNetworkUsageDescription']
version = json.loads((Path(__file__).resolve().parents[1] / 'src-tauri/tauri.conf.json').read_text())['version']
assert info['CFBundleShortVersionString'] == version, info
checks['mac_permissions_and_minimum_version'] = True
runtime = json.loads((resources / 'runtime/manifest.json').read_text())
for field, name in (('runtime_sha256', 'libxz-mods.so'), ('receiver_sha256', 'libxz-receiver.so'),
                    ('doom_sha256', 'xz-doom'), ('ota_updater_sha256', 'xz-updater'),
                    ('ota_runtime_smoke_sha256', 'xz-runtime-smoke'), ('settings_schema_sha256', 'settings-schema.json')):
    with (resources / 'runtime' / name).open('rb') as stream:
        assert hashlib.file_digest(stream, 'sha256').hexdigest() == runtime[field], name
assert runtime['prepared_containers'] == ['OVPGZ001', 'OVPGZ003']
assert runtime['stem_page_codecs'] == ['zlib', 'flac-96k']
checks['exact_paired_runtime_and_stem_formats'] = True

env = {**os.environ, 'XZ_BUILDER_RESOURCES': str(resources), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin'}


def call(request):
    result = subprocess.run([str(resources / 'backend/xz-mods-service')], input=json.dumps(request),
                            text=True, capture_output=True, env=env, timeout=60, check=True)
    return json.loads(result.stdout.splitlines()[-1])


networks = call({'method': 'ota_networks'})
assert networks['ok'] and networks['result']['ready'], networks
assert isinstance(networks['result']['addresses'], list), networks
checks['current_signed_network_update_present'] = True

with tempfile.TemporaryDirectory(prefix='xz-mac-fat-') as temporary:
    root = Path(temporary)
    image = root / 'USB.dmg'
    subprocess.run(['/usr/bin/hdiutil', 'create', '-size', '128m', '-fs', 'MS-DOS FAT32',
                    '-volname', 'XZMACVERIFY', str(image)], check=True)
    attached = plistlib.loads(subprocess.check_output(['/usr/bin/hdiutil', 'attach', '-plist',
                                                      '-nobrowse', str(image)]))
    mounts = [e['mount-point'] for e in attached['system-entities'] if 'mount-point' in e]
    assert len(mounts) == 1
    volume = Path(mounts[0])
    try:
        (volume / 'keep-music.txt').write_text('Keep existing music')
        inspected = call({'method': 'inspect_usb', 'volume': str(volume)})
        assert inspected['ok'], inspected
        assert inspected['result']['filesystem'] == 'FAT32', inspected
        assert not inspected['result']['requires_copy_to_usb_root'], inspected
        checks['real_fat32_volume_detection'] = True
        from importlib.util import spec_from_file_location, module_from_spec
        cache_path = resources / 'source/xdj-xz-toolkit/builder/cache.py'
        # Source is also retained in the private sibling toolkit for the no-replace test.
        if not cache_path.exists():
            cache_path = Path(__file__).resolve().parents[2] / 'xdj-xz-toolkit/builder/cache.py'
        spec = spec_from_file_location('mac_cache', cache_path)
        cache = module_from_spec(spec)
        spec.loader.exec_module(cache)
        staged = volume / 'staged'
        staged.mkdir()
        destination = volume / 'destination'
        destination.mkdir()
        try:
            cache._publish_new(staged, destination)
        except FileExistsError:
            pass
        else:
            raise AssertionError('Existing directory was overwritten')
        destination.rmdir()
        cache._publish_new(staged, destination)
        assert destination.is_dir() and not staged.exists()
        checks['fat32_exclusive_no_replace'] = True
        assert (volume / 'keep-music.txt').read_text() == 'Keep existing music'
    finally:
        subprocess.run(['/usr/bin/hdiutil', 'detach', str(volume)], check=True)

# Launch the exact app executable with its bundled resource path. Capture a real window.
process = subprocess.Popen([str(binary)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
try:
    time.sleep(8)
    assert process.poll() is None, process.stderr.read().decode()
    subprocess.run(['/usr/sbin/screencapture', '-x', str(args.evidence.with_suffix('.png'))], check=True)
    window = subprocess.run(['/usr/bin/osascript', '-e',
        'tell application "System Events" to tell process "xz-mods-builder" to get name of every window'],
        text=True, capture_output=True)
    assert window.returncode == 0 and 'XZ Mods Builder' in window.stdout, window.stderr
    checks['packaged_app_window_launch'] = True
finally:
    process.terminate()
    process.wait(timeout=10)
args.evidence.write_text(json.dumps({'version': version, 'architecture': platform.machine(), 'runtime': runtime,
    'checks': checks, 'physical_usb': False, 'device_access': False,
    'apple_developer_signed': False, 'notarized': False, 'new_fat_loader_first_write_crash_atomic': False}, indent=2) + '\n')
print(json.dumps(checks, indent=2))
