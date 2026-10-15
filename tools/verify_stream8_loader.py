"""Build/decrypt an 8-bit trial loader in a private folder. Never writes a USB."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    app, work = a.app.resolve(), a.output.resolve()
    resources = app / 'resources'
    manifest = json.loads((resources / 'runtime/manifest.json').read_text(encoding='utf-8'))
    assert manifest['experimental_stream_color_bits'] == 8
    assert sha(resources / 'runtime/libxz-mods.so') == manifest['runtime_sha256']
    assert sha(resources / 'runtime/libxz-receiver.so') == manifest['receiver_sha256']
    cached = Path(os.environ['LOCALAPPDATA']) / 'com.opticmystic.xzmods'
    firmware_path, key = cached / 'local-firmware/XDJXZ_v126.zip', cached / 'boot-support/aes256.key'
    assert firmware_path.is_file() and key.is_file(), 'Verified local boot inputs are required'
    work.mkdir(parents=True, exist_ok=False)
    volume = work / 'private-volume'
    volume.mkdir()
    env = {**os.environ, 'XZ_BUILDER_RESOURCES': str(resources), 'XZ_BUILDER_DATA': str(work / 'app-data')}
    def call(method, **fields):
        result = subprocess.run(
            [str(resources / 'backend/xz-mods-service.exe')], input=json.dumps({'method': method, **fields}),
            capture_output=True, text=True, encoding='utf-8', env=env, timeout=300,
            creationflags=subprocess.CREATE_NO_WINDOW)
        with (work / 'backend-events.jsonl').open('a', encoding='utf-8') as log:
            log.write(result.stdout)
        assert result.returncode == 0, result.stderr
        final = json.loads(result.stdout.splitlines()[-1])
        assert final.get('ok'), final
        return final['result']
    versions = call('list_mod_versions')
    call('select_mod_version', mod_version_id=versions['bundled_id'], pinned=True, expected_revision=versions['selected']['revision'])
    result = call('build_usb', volume=str(volume), firmware=str(firmware_path), key=str(key), experimental=True,
                  mod_version_id=versions['bundled_id'], pin_mod_version=True)
    image = Path(result['image']).resolve()
    assert image.is_relative_to(work)
    sys.path.insert(0, str(app / 'source/xdj-xz-toolkit'))
    from builder import firmware
    archive = firmware._open(firmware.crypt(image.read_bytes(), firmware.effective_key(key), True))
    try:
        for member, name in [('tools/libxz-mods.so', 'libxz-mods.so'), ('tools/libxz-directfb-hook.so', 'libxz-receiver.so')]:
            assert firmware._member(archive, '/' + member) == (resources / 'runtime' / name).read_bytes()
        metadata = json.loads(firmware._member(archive, '/xz-mod-version.json'))
        assert metadata == result['mod_version']
        assert b'if false; then # XZ Mods: selected version is pinned; skip OTA apply' in firmware._member(archive, '/autoexec.sh')
    finally:
        archive.close()
    report = {'image': str(image), 'sha256': sha(image), 'runtime_sha256': manifest['runtime_sha256'],
              'receiver_sha256': manifest['receiver_sha256'], 'paired_module_readback': True,
              'pinned_trial': True, 'mod_version_id': versions['bundled_id'],
              'physical_usb_written': False, 'controller_contacted': False}
    (work / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
