"""Run both current CPU models through the frozen Apple Silicon backend to FLAC."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import tempfile
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--resources', type=Path, required=True)
parser.add_argument('--toolkit', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
args = parser.parse_args()
if platform.machine() != 'arm64':
    raise SystemExit('Current model wheels require Apple Silicon.')
sys.path.insert(0, str(args.toolkit.resolve() / 'builder/tests'))
from pdb_fixture import write_export
resources = args.resources.resolve()
checks = {}
with tempfile.TemporaryDirectory(prefix='xz-mac-models-') as temporary:
    root = Path(temporary).resolve()
    env = {**os.environ, 'XZ_BUILDER_RESOURCES': str(resources), 'XZ_BUILDER_DATA': str(root / 'data'),
           'XZ_AUDIO_HELPER': str(resources / 'xz-audio-helper'), 'OPENBLAS_NUM_THREADS': '1',
           'OMP_NUM_THREADS': '1', 'VECLIB_MAXIMUM_THREADS': '1', 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin'}
    for preset in ('umxhq', 'vocal-focus'):
        usb = root / preset
        source = usb / 'Contents/Test track.wav'
        source.parent.mkdir(parents=True)
        with wave.open(str(source), 'wb') as output:
            output.setparams((2, 2, 44100, 44100, 'NONE', 'NONE'))
            output.writeframes(b''.join(struct.pack('<hh', *[int(3000 * math.sin(i * 2 * math.pi * 220 / 44100))] * 2) for i in range(44100)))
        write_export(usb / 'PIONEER/rekordbox/export.pdb', {7: '/Contents/Test track.wav'})
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        result = subprocess.run([str(resources / 'backend/xz-mods-service')],
            input=json.dumps({'method': 'separate', 'preset': preset, 'source': str(source)}),
            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1500)
        print(result.stdout, flush=True)
        assert result.returncode == 0
        receipt = json.loads(result.stdout.splitlines()[-1])
        assert receipt['ok'], receipt
        assert receipt['result']['verified'], receipt
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
        verified = subprocess.run([str(resources / 'xz-overcue-check'), str(source)], env=env,
                                  capture_output=True, text=True, check=True)
        decoded = json.loads(verified.stdout)
        assert decoded['page_codec'] == 'flac-96k' and decoded['verified_mixes'] == 7
        checks[preset] = {'real_inference': True, 'flac_mixes': 7, 'source_unchanged': True,
                          'complete_pcm_verified': True}
args.evidence.write_text(json.dumps({'models': checks, 'device_access': False}, indent=2) + '\n')
