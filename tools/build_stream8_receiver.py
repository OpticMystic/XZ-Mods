"""Build an isolated raw-RGB332 receiver over a hash-guarded installed baseline.

Does not install, contact the controller, modify USB media or touch app resources.
The known audio library is retained byte for byte. The exact composite source
archive and overlay hashes accompany the experimental receiver.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import zipfile

sys.dont_write_bytecode = True
APP = Path(__file__).resolve().parents[1]
OVERLAYS = ('vendor/tools/xz_runtime/xz_screen_codec.h',
            'vendor/tools/xz_runtime/xz_screen_server.h',
            'mods/tests/test_screen_server_wire.c')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'XZ Mods/resources')
    parser.add_argument('--expected-runtime', required=True)
    parser.add_argument('--expected-receiver', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zig', type=Path)
    args = parser.parse_args()
    runtime = args.resources.resolve() / 'runtime'
    if sha(runtime / 'libxz-mods.so') != args.expected_runtime or sha(runtime / 'libxz-receiver.so') != args.expected_receiver:
        raise SystemExit('Installed native baseline changed. Requalify the baseline before building this trial.')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'source'
    source.mkdir()
    with zipfile.ZipFile(runtime / 'source.zip') as archive:
        for entry in archive.infolist():
            path = (source / entry.filename).resolve()
            if not path.is_relative_to(source) or not entry.filename.startswith(('mods/', 'vendor/')) or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Unsafe native archive member: ' + entry.filename)
        archive.extractall(source)
    overlay_hashes = {}
    for name in OVERLAYS:
        original = APP / 'source/xdj-xz-toolkit' / name
        shutil.copy2(original, source / name)
        overlay_hashes[name] = sha(original)
    if args.zig:
        zig = args.zig.resolve()
    else:
        import ziglang
        zig = Path(ziglang.__file__).parent / 'zig.exe'
    bundle = output / 'bundle'
    shutil.copytree(runtime, bundle)
    vendor = source / 'vendor'
    subprocess.run([str(zig), 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9',
                    '-O2', '-s', '-Wall', '-Wextra', '-Werror', '-std=gnu99', '-fPIC', '-shared',
                    '-Wl,-z,nodelete', '-Wl,-T,' + str(source / 'mods/xz-relocations.ld'),
                    '-I' + str(vendor / 'build/dfb-generated'),
                    '-I' + str(vendor / 'build/directfb-1.4-src/include'),
                    '-I' + str(vendor / 'build/directfb-1.4-src/lib'),
                    str(vendor / 'tools/xz_runtime/xz_directfb_hook.c'), '-ldl', '-pthread',
                    '-o', str(bundle / 'libxz-receiver.so')], check=True)
    sys.path.insert(0, str(source / 'mods'))
    spec = importlib.util.spec_from_file_location('stream8_native_inspect', source / 'mods/build.py')
    inspector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inspector)
    abi = inspector.inspect(bundle / 'libxz-receiver.so')
    files = {path.relative_to(source).as_posix(): sha(path) for path in sorted(source.rglob('*'))
             if path.is_file() and '__pycache__' not in path.parts}
    with zipfile.ZipFile(bundle / 'source.zip', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in files:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, (source / name).read_bytes())
    manifest = json.loads((bundle / 'manifest.json').read_text())
    manifest.update(receiver_sha256=sha(bundle / 'libxz-receiver.so'), hardware_qualified=False,
                    source_dirty=True, experimental_stream_color_bits=8,
                    source_snapshot_sha256=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                    source_base_archive_sha256=sha(runtime / 'source.zip'),
                    source_overlay=overlay_hashes,
                    stream_protocols=[1, 2, 3])
    manifest['files'] = {p.relative_to(bundle).as_posix(): sha(p) for p in sorted(bundle.rglob('*'))
                         if p.is_file() and p != bundle / 'manifest.json'}
    (bundle / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    assert sha(bundle / 'libxz-mods.so') == args.expected_runtime
    report = {'audio_library_unchanged': True, 'runtime_sha256': args.expected_runtime,
              'baseline_receiver_sha256': args.expected_receiver,
              'receiver': abi, 'source_files': len(files), 'overlays': overlay_hashes,
              'bundle': str(bundle), 'hardware_qualified': False, 'controller_contacted': False}
    (output / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
