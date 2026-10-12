"""Prove the separation engine on this machine: plan, set up, separate through the persistent
worker, derive drums for the import path, and check that the encoded parts sum back to the mix.

Needs a resources folder with uv and inference/{inference.py,models.json}, a data root
(XZ_BUILDER_DATA) that may be empty, and a 44.1 kHz stereo 16-bit WAV. Downloads the
preset's weights into the data root when they are missing.

    python builder/tests/verify_separation_engine.py --resources E:/scratch/resources \
        --data E:/scratch/data --track track.wav --preset balanced --preset fast
"""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import wave

import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--resources', type=Path, required=True)
parser.add_argument('--data', type=Path, required=True)
parser.add_argument('--track', type=Path, required=True)
parser.add_argument('--preset', action='append', default=[])
parser.add_argument('--tolerance', type=float, default=1e-4, help='max |mix - vocals - drums - harmonics| after gain restore')
args = parser.parse_args()
toolkit = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(toolkit))
os.environ.update(XZ_BUILDER_RESOURCES=str(args.resources.resolve()), XZ_BUILDER_DATA=str(args.data.resolve()))

from builder import engines
from builder.jobs import Job


class Recorder(Job):
    def __init__(self):
        super().__init__()
        self.events = []

    def progress(self, stage, message, **details):
        self.events.append({'stage': stage, **details})
        if details.get('pct') in (0, 100.0, 100) or stage not in ('separate', 'separate-vocals', 'separate-drums'):
            print(f'  [{stage}] {message} {details.get("overall", "")}', flush=True)


def read(path, gain=1.0):
    with wave.open(str(path), 'rb') as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100), path
        pcm = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2').reshape(-1, 2)
    return pcm.astype(np.float64) / 32767 / gain


summary = []
for preset in args.preset or ['balanced', 'fast']:
    job = Recorder()
    plan = engines.plan_engine_setup({'preset': preset}, job, args.resources)
    print(f'plan {preset}: device={plan["device"]} name={plan["device_name"]} vram={plan["vram_bytes"]} '
          f'flavour={plan["torch_flavour"]} installed={plan["torch_installed_flavour"]} '
          f'download~{plan["download_bytes_estimate"] / 1e6:.0f} MB recreate={plan["recreate_runtime"]}')
    started = time.monotonic()
    state = engines.setup({'preset': preset}, job, args.resources)
    print(f'setup {preset}: {time.monotonic() - started:.1f} s, device {state["device"]}, flavour {state["torch_flavour"]}')
    with tempfile.TemporaryDirectory(prefix='xz-verify-', dir=args.data) as temporary:
        temporary = Path(temporary)
        with engines.Engine(preset, job, args.resources) as engine:
            started = time.monotonic()
            first_event = len(job.events)
            result = engine.separate(args.track, temporary / 'parts', ('vocals', 'harmonics'))
            wall = time.monotonic() - started
            first_run = job.events[first_event:]
            timing = result['timing']
            started = time.monotonic()
            again = engine.separate(args.track, temporary / 'again', ('vocals', 'harmonics'))
            wall_again = time.monotonic() - started
        assert again['timing']['load_seconds'] < 0.5, 'second track in the same engine must not reload models'
        with engines.engine_lock():
            derived = engines.separate_drums(args.track, temporary / 'drums', job, args.resources)
        stems = {item['name']: item for item in result['stems']}
        mix = read(args.track)
        vocals = read(temporary / 'parts' / stems['vocals']['file'], stems['vocals']['gain'])
        harmonics = read(temporary / 'parts' / stems['harmonics']['file'], stems['harmonics']['gain'])
        drums = read(derived['drums']['path'], derived['drums']['gain'])
        remainder = read(derived['harmonics']['path'], derived['harmonics']['gain'])
        assert float(np.max(np.abs(mix - drums - remainder))) <= args.tolerance, 'drums + remainder must sum to the source'
        assert len(vocals) == len(harmonics) == len(drums) == len(mix), 'parts must match the track length'
        residual = float(np.max(np.abs(mix - vocals - harmonics - drums)))
        overall = [event['overall'] for event in first_run if 'overall' in event]
        assert all(later >= earlier for earlier, later in zip(overall, overall[1:])), 'overall progress must be monotonic'
        estimate = engines.estimate_separation_seconds(preset, timing['frames'])
        seconds = timing['frames'] / 44100
        line = {'preset': preset, 'device': result['identity']['device'], 'track_seconds': round(seconds, 1),
                'load_seconds': round(timing['load_seconds'], 1), 'separate_seconds': round(timing['separate_seconds'], 1),
                'postprocess_seconds': round(timing['postprocess_seconds'], 2), 'wall_first': round(wall, 1), 'wall_second': round(wall_again, 1),
                'seconds_per_source_second': round(timing['separate_seconds'] / seconds, 4),
                'peak_device_bytes': timing.get('peak_device_bytes'), 'residual_max': residual,
                'estimate_seconds': round(estimate, 1), 'diagnostics': result.get('diagnostics')}
        print(json.dumps(line))
        assert residual <= args.tolerance, f'{preset}: parts do not sum to the mix ({residual})'
        summary.append(line)
print('PASS separation engine:', json.dumps(summary))
