"""End-to-end stem builder check: import_stems on a synthetic Rekordbox USB, then the
upstream OverCue validator and the device reader (xz-overcue-check) on the result.

Creates a throwaway engine environment (NumPy/SciPy only) with uv, as setup_engine does.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', type=Path, required=True)
parser.add_argument('--overcue-validator', type=Path, required=True, help='verify_usb.py from OverCue-gg/overcue-stems-format')
parser.add_argument('--uv', type=Path, default=shutil.which('uv'))
parser.add_argument('--seconds', type=float, default=20)
args = parser.parse_args()
toolkit = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(toolkit), str(toolkit/'builder/tests')]
from pdb_fixture import write_export


def tone(path, frequencies, level):
    frames = int(44100 * args.seconds)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(2); out.setsampwidth(2); out.setframerate(44100)
        out.writeframes(b''.join(struct.pack('<hh', *[int(level * sum(math.sin(i * f * 2 * math.pi / 44100) for f in frequencies))] * 2)
                                 for i in range(frames)))


with tempfile.TemporaryDirectory(prefix='xz-stem-builder-') as temporary:
    root = Path(temporary)
    resources, data, usb = root/'resources', root/'data', root/'USB café'
    (resources/'inference').mkdir(parents=True)
    shutil.copyfile(toolkit/'builder/overcue_roles.py', resources/'inference/overcue_roles.py')
    subprocess.run([sys.executable, str(toolkit/'builder/build_overcue_check.py'), '--zig', str(args.zig),
                    '--output', str(resources/'xz-overcue-check.exe')], check=True)
    python = data/'engine-python'/('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
    subprocess.run([str(args.uv), 'venv', '--python', '3.11', str(python.parents[1])], check=True, capture_output=True)
    subprocess.run([str(args.uv), 'pip', 'install', '--python', str(python), 'numpy==2.2.6', 'scipy==1.16.1'], check=True, capture_output=True)
    (data/'setup-umxhq.json').write_text('{}')
    os.environ.update(XZ_BUILDER_RESOURCES=str(resources), XZ_BUILDER_DATA=str(data))

    source = usb/'Contents/Artist/Track 音楽.wav'
    tone(source, (220, 110, 660), 5000)
    tone(root/'vocals.wav', (660,), 5000 * 0.5)
    tone(root/'harmonics.wav', (220,), 5000 * 2)
    write_export(usb/'PIONEER/rekordbox/export.pdb', {41: '/Contents/Artist/Track 音楽.wav', 42: '/Contents/Other.wav'})
    original = source.read_bytes()

    from builder import service
    from builder.jobs import Job
    request = {'method': 'import_stems', 'source': str(source), 'separation_id': 'synthetic-v1',
               'vocals': str(root/'vocals.wav'), 'vocals_gain': 0.5, 'harmonics': str(root/'harmonics.wav'), 'harmonics_gain': 2}
    receipt = service.dispatch(request, Job())
    print(json.dumps(receipt))
    assert (receipt['track_id'], receipt['file_path'], receipt['verified'], receipt['reused']) == (41, '/Contents/Artist/Track 音楽.wav', True, False)
    assert source.read_bytes() == original
    manifest = json.loads((usb/'CDJMODS/stems'/receipt['bundle']/'overcue-manifest.json').read_text(encoding='utf-8'))
    assert {role['loudness_gain'] for role in manifest['roles'].values()} == {1.0}, 'device alignment already undoes headroom'
    assert 0 < manifest['runtime']['headroom_gain'] <= 1

    upstream = subprocess.run([sys.executable, str(args.overcue_validator), str(usb), receipt['file_path']],
                              capture_output=True, text=True, encoding='utf-8')
    print('upstream verify_usb.py:', upstream.stdout.strip() or upstream.stderr.strip())
    assert upstream.returncode == 0 and receipt['bundle'] in upstream.stdout

    checker = subprocess.run([str(resources/'xz-overcue-check.exe'), str(source)], capture_output=True, text=True, encoding='utf-8')
    print('xz-overcue-check:', checker.stdout.strip() or checker.stderr.strip())
    assert checker.returncode == 0 and json.loads(checker.stdout)['verified_mixes'] == 7
    assert json.loads(checker.stdout)['frames'] == receipt['frames'] == -(-int(44100 * args.seconds) * 320 // 147)
    inspected = service.dispatch({'method': 'inspect_overcue', 'source': str(source)}, Job())
    assert inspected['all_pages_verified'] and inspected['source_identity_verified']

    again = service.dispatch(request, Job())
    assert again['reused'] and again['bundle'] == receipt['bundle']
print('PASS stem builder: Rekordbox-keyed import, upstream OverCue validator, device reader, reuse')
