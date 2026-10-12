# SPDX-License-Identifier: MIT
"""Find the sample offset and gain that line imported stems up with the exported track.

Runs in the managed import environment (numpy, scipy). Arrays are (frames, 2) float32 at
44.1 kHz and may be memory maps; every pass reads them in bounded windows or chunks.
"""
from __future__ import annotations

import math

import numpy as np
from scipy.signal import correlate, resample_poly

RATE = 44100
SEARCH = RATE
DECIMATION = 8
REFINE = 2 * DECIMATION
WINDOW = 20 * RATE
MAX_WINDOWS = 8
CHUNK = 1 << 16
# Aligned: the stems sum to the track. With the least-squares gain the residual is
# 10*log10(1 - r^2), so -20 dB means r >= 0.99499 and is the binding limit; 0.98 guards rounding.
MIN_CORRELATION = 0.98
MAX_RESIDUAL_DB = -20.0
# Below this the stems do not line up with this track at any offset in the search range.
# Measured on a 172 s track: a different song 0.011, a mid-band-only partial set 0.374,
# a re-mastered (saturated) mix 0.979 at -13.9 dB, lowest-quality VBR MP3 stems 0.9948 at -19.8 dB.
WEAK_CORRELATION = 0.2
THRESHOLDS = {'aligned_correlation': MIN_CORRELATION, 'aligned_residual_db': MAX_RESIDUAL_DB,
              'match_correlation': WEAK_CORRELATION, 'search_ms': 1000.0 * SEARCH / RATE}


def segment(audio, start, length):
    """audio[start:start+length] with zeros outside the signal."""
    out = np.zeros((length, *audio.shape[1:]), dtype=np.float32)
    first, last = max(0, start), min(len(audio), start + length)
    if first < last:
        out[first - start:last - start] = audio[first:last]
    return out


def mono(audio, start, length):
    return segment(audio, start, length).mean(axis=1, dtype=np.float64)


def window_starts(frames, length):
    count = max(1, min(MAX_WINDOWS, -(-frames // length)))
    return np.linspace(0, max(0, frames - length), count).astype(np.int64)


def scan(mix, stems, center, radius, factor):
    """Normalised cross-correlation of the stems against the mix for lags center-radius..center+radius."""
    length = min(WINDOW, len(mix))
    lags = 2 * radius // factor + 1
    product = np.zeros(lags)
    stem_energy = np.zeros(lags)
    mix_energy = 0.0
    for start in window_starts(len(mix), length):
        reference = mono(mix, int(start), length)
        candidate = mono(stems, int(start) + center - radius, length + 2 * radius)
        if factor > 1:
            reference = resample_poly(reference, 1, factor)
            candidate = resample_poly(candidate, 1, factor)
        reference = reference[:length // factor]
        candidate = candidate[:len(reference) + lags - 1]
        product += correlate(candidate, reference, mode='valid', method='fft')
        squares = np.concatenate(([0.0], np.cumsum(candidate * candidate)))
        stem_energy += squares[len(reference):len(reference) + lags] - squares[:lags]
        mix_energy += float(reference @ reference)
    denominator = np.sqrt(np.maximum(stem_energy, 0.0) * mix_energy)
    return np.divide(product, denominator, out=np.zeros(lags), where=denominator > 0)


class Fit:
    """Least-squares gain, correlation and residual of mix ≈ gain * stems, accumulated chunk by chunk."""

    def __init__(self):
        self.mix = self.stems = self.cross = 0.0

    def add(self, mix, stems):
        mix = np.asarray(mix, dtype=np.float64).ravel()
        stems = np.asarray(stems, dtype=np.float64).ravel()
        self.mix += float(mix @ mix)
        self.stems += float(stems @ stems)
        self.cross += float(mix @ stems)

    def result(self):
        if not self.mix > 0:
            raise ValueError('The complete track is silent')
        if not self.stems > 0:
            raise ValueError('The stems are silent')
        correlation = self.cross / math.sqrt(self.mix * self.stems)
        residual = max(1.0 - correlation * correlation, 1e-12) if correlation > 0 else 1.0
        return {'gain': self.cross / self.stems, 'correlation': correlation, 'residual_db': 10 * math.log10(residual)}


def measure(mix, stems, offset=0, gain=1.0):
    """Fit of the stems shifted by offset over the whole mix; gain is applied before fitting."""
    fit = Fit()
    for start in range(0, len(mix), CHUNK):
        count = min(CHUNK, len(mix) - start)
        fit.add(mix[start:start + count], segment(stems, start + offset, count) * np.float32(gain))
    return fit.result()


def verdict(correlation, residual_db):
    if correlation >= MIN_CORRELATION and residual_db <= MAX_RESIDUAL_DB:
        return 'aligned'
    return 'weak' if correlation >= WEAK_CORRELATION else 'mismatch'


def reason(result):
    if result['verdict'] == 'aligned':
        return None
    if result['verdict'] == 'weak':
        return (f"The stems line up with this track but do not add up to it (residual {result['residual_db']:.1f} dB). "
                'A part may be missing, or the stems come from a different master.')
    return f"These stems do not match this track (correlation {result['correlation']:.2f})."


def plan(mix_frames, stem_frames, offset):
    """Trim or pad so that aligned[n] = stems[n + offset] for every mix frame n."""
    return {'trim_start': max(0, offset), 'pad_start': max(0, -offset),
            'trim_end': max(0, stem_frames - offset - mix_frames), 'pad_end': max(0, mix_frames - (stem_frames - offset))}


def align(mix, stems):
    """Integer offset (stems lag the mix by offset samples), gain, residual and verdict."""
    coarse = scan(mix, stems, 0, SEARCH, DECIMATION)
    center = int(np.argmax(coarse)) * DECIMATION - SEARCH
    fine = scan(mix, stems, center, REFINE, 1)
    offset = center - REFINE + int(np.argmax(fine))
    fit = measure(mix, stems, offset)
    result = {'offset_samples': offset, 'offset_ms': round(1000.0 * offset / RATE, 3), 'gain': fit['gain'],
              'gain_db': 20 * math.log10(fit['gain']) if fit['gain'] > 0 else None,
              'correlation': fit['correlation'], 'residual_db': fit['residual_db'],
              'mix_frames': len(mix), 'stem_frames': len(stems),
              'plan': plan(len(mix), len(stems), offset), 'thresholds': THRESHOLDS}
    result['verdict'] = verdict(fit['correlation'], fit['residual_db'])
    result['reason'] = reason(result)
    return result
