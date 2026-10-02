"""Re-prove the PWV6/PWV7 byte order that builder/three_band.py relies on, from the audio itself.

Correlates each byte of the .2EX detail waveform (PWV7, 150 entries per second) with the audio's
energy in octave bands, and optionally rekordbox's own PWV5 red/green/blue from the sibling .EXT.
Audio is raw mono s16le 44.1 kHz (.pcm), a WAV, or anything ffmpeg on PATH can decode.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import wave
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('audio', type=Path)
parser.add_argument('two_ex', type=Path)
parser.add_argument('--ext', type=Path, help='sibling ANLZ0000.EXT, adds rekordbox RGB channel rows')
parser.add_argument('--max-lag', type=int, default=8, help='entries of audio/waveform offset to search')
args = parser.parse_args()
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from builder import three_band

RATE = 44100
HOP = RATE // 150
EDGES = [30, 60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000]
FAMILY = {'low': (30, 250), 'mid': (250, 2000), 'high': (2000, 16000)}


def samples(path):
    if path.suffix.lower() == '.pcm':
        return np.frombuffer(path.read_bytes(), '<i2').astype(np.float64)
    if path.suffix.lower() == '.wav':
        with wave.open(str(path)) as file:
            assert file.getsampwidth() == 2 and file.getframerate() == RATE, 'expected 16-bit 44.1 kHz WAV'
            data = np.frombuffer(file.readframes(file.getnframes()), '<i2').reshape(-1, file.getnchannels())
        return data.mean(axis=1)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 's16le', '-ac', '1', '-ar', str(RATE), '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, '<i2').astype(np.float64)


def band_energy(audio, count):
    window = np.hanning(2048)
    padded = np.pad(audio, 1024)
    frequencies = np.fft.rfftfreq(2048, 1 / RATE)
    rows = []
    for index in range(count):
        power = np.abs(np.fft.rfft(padded[index * HOP:index * HOP + 2048] * window)) ** 2
        rows.append([np.sqrt(power[(frequencies >= lo) & (frequencies < hi)].sum()) for lo, hi in zip(EDGES, EDGES[1:])])
    return np.array(rows)


def correlations(series, energy, lag):
    count = len(energy)
    shifted = series[max(0, lag):count + lag] if lag >= 0 else np.pad(series, (-lag, 0))[:count]
    usable = min(len(shifted), count)
    return np.array([np.corrcoef(shifted[:usable], energy[:usable, band])[0, 1] for band in range(energy.shape[1])])


two_ex = args.two_ex.read_bytes()
start, count = three_band.waves(two_ex)[b'PWV7']
entries = np.frombuffer(two_ex[start:start + count * 3], np.uint8).reshape(count, 3).astype(np.float64)
audio = samples(args.audio)
usable = min(count, len(audio) // HOP)
energy = band_energy(audio, usable)
lags = range(-args.max_lag, args.max_lag + 1)
lag = max(lags, key=lambda value: sum(correlations(entries[:, byte], energy, value).max() for byte in range(3)))

rows = {f'PWV7 byte{byte}': entries[:, byte] for byte in range(3)}
if args.ext:
    ext = args.ext.read_bytes()
    detail_start, detail_count = three_band.waves(ext)[b'PWV5']
    values = np.frombuffer(ext[detail_start:detail_start + detail_count * 2], '>u2').astype(np.int64)
    for name, shift in (('red', 13), ('green', 10), ('blue', 7)):
        rows[f'PWV5 {name}'] = ((values >> shift) & 7).astype(np.float64)

labels = [f'{lo}-{hi}' for lo, hi in zip(EDGES, EDGES[1:])]
print(f'{args.two_ex}: {usable} of {count} entries ({usable / 150:.1f} s of audio), best lag {lag:+d} entries')
print(f'{"":14}' + ''.join(f'{label:>11}' for label in labels) + '   peak band')
derived = []
for name, series in rows.items():
    values = correlations(series, energy, lag)
    peak = int(values.argmax())
    lo, hi = EDGES[peak], EDGES[peak + 1]
    family = next(band for band, (low, high) in FAMILY.items() if low <= lo and hi <= high)
    if name.startswith('PWV7'):
        derived.append(family)
    print(f'{name:14}' + ''.join(f'{value:11.2f}' for value in values) + f'   {lo}-{hi} Hz ({family})')
verdict = 'MATCH' if tuple(derived) == three_band.BAND_ORDER else 'MISMATCH'
print(f'derived byte order {tuple(derived)}; three_band.BAND_ORDER {three_band.BAND_ORDER}: {verdict}')
sys.exit(verdict != 'MATCH')
