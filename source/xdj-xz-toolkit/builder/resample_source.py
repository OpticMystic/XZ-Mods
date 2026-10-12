# SPDX-License-Identifier: MIT
"""Resample a 16-bit stereo WAV to 44.1 kHz in bounded blocks.

Runs in the managed import environment. The output frame count is
ceil(frames * 44100 / rate), the same rule grouped_stems and the storage
estimator use, so every later stage agrees on the track length.
"""
import argparse
import math
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RATE = 44100


def output_frames(frames, rate):
    return -(-frames * RATE // rate)


def resample(source, target, expected=None):
    info = sf.info(source)
    if info.channels != 2 or info.subtype != 'PCM_16':
        raise ValueError('Expected the helper\'s 16-bit stereo WAV')
    if info.samplerate == RATE:
        raise ValueError('Already 44.1 kHz')
    factor = math.gcd(info.samplerate, RATE)
    up, down = RATE // factor, info.samplerate // factor
    total = output_frames(info.frames, info.samplerate)
    if expected is not None and total != expected:
        raise ValueError(f'Resampled length {total} does not match the inspected length {expected}')
    block = down * max(1, min(2048, 65536 // down))
    radius = -(-10 * max(up, down) // up)
    context = -(-radius // down) * down
    written = 0
    with sf.SoundFile(source) as reader, sf.SoundFile(target, 'x', samplerate=RATE, channels=2, subtype='PCM_16') as writer:
        for start in range(0, info.frames, block):
            end = min(info.frames, start + block)
            first, last = max(0, start - context), min(info.frames, end + context)
            reader.seek(first)
            samples = reader.read(last - first, dtype='float32', always_2d=True)
            if len(samples) != last - first:
                raise ValueError('Source WAV is truncated')
            converted = resample_poly(samples, up, down, axis=0)
            out_first, out_last = start * up // down, -(-end * up // down)
            trim = (start - first) * up // down
            chunk = np.clip(converted[trim:trim + out_last - out_first], -1.0, 1.0)
            writer.write(chunk)
            written += len(chunk)
    if written != total:
        raise ValueError('Resampled output length mismatch')
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, help='Expected 44.1 kHz frame count from inspection')
    args = parser.parse_args()
    print(resample(args.input, args.output, args.frames))


if __name__ == '__main__':
    main()
