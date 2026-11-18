# SPDX-License-Identifier: MIT
"""Storage estimates for OverCue stem bundles, learned from bundles written here.

Seven stereo 16-bit roles are 176,400 bytes per second each at 44.1 kHz (the
default) or 384,000 at 96 kHz, before FLAC pages. Compression depends on the
music: sparse roles (vocal, drums) shrink more than the full mix. Priors come
from measured real tracks; every bundle this machine writes refines them per
role and per sample rate.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import statistics
import tempfile

ROLES = ('vocal', 'instrumental', 'drums', 'harmonics', 'vocals-drums', 'vocals-harmonics', 'full-mix')
RATES = (44100, 96000)
DEFAULT_RATE = 44100
ROLE_BYTES_PER_FRAME = 4
PAGE_BYTES = 131072
MAX_PAGES = 4096
PAGE_TABLE_ENTRY = 48
PAGE_HEADER = 24
MANIFEST_BYTES = 2600
INDEX_ENTRY_BYTES = 1200
INDEX_LIMIT = 4 * 1024 * 1024
MAX_INDEX_LIMIT = 16 * 1024 * 1024
FILESYSTEM_CLUSTER = 32768
# FLAC page size over raw PCM. 96 kHz: measured on a real 44.1 kHz master upsampled
# to 96 kHz (full mix 0.40). 44.1 kHz pages hold no empty upper octave, so each
# byte of raw PCM compresses less: these are the 96 kHz priors times 1.6. Two
# real 44.1 kHz masters measured 1.53 and 1.59 for the full mix through this
# packer at both rates (0.61/0.40 and 0.51/0.32). Partial roles are
# sparser; all priors start a little high so first estimates err on the safe side
# until observations replace them.
PRIORS_96K = {'vocal': 0.28, 'drums': 0.32, 'harmonics': 0.38, 'instrumental': 0.43,
              'vocals-drums': 0.40, 'vocals-harmonics': 0.42, 'full-mix': 0.45}
PRIOR_SCALE_44K1 = 1.6
PRIORS = {96000: PRIORS_96K,
          44100: {role: round(ratio * PRIOR_SCALE_44K1, 2) for role, ratio in PRIORS_96K.items()}}
# Learned samples per rate in the model file. 'samples' predates 44.1 kHz bundles, so it is the 96 kHz set.
SAMPLE_KEYS = {96000: 'samples', 44100: 'samples_44100'}
# The decoder and separation stages accept about 23 minutes of source at either rate.
MAX_SOURCE_FRAMES = (MAX_PAGES * PAGE_BYTES // ROLE_BYTES_PER_FRAME) * 147 // 320
SPREAD = 0.20
MAX_SAMPLES = 50
MODEL_FILE = 'stem-size-model.json'
WAVEFORM_COLUMNS_PER_SECOND = 150
WAVEFORM_HEADER = 28 + 24
WAVEFORM_STEMS = 3


def _rate(rate):
    if rate not in RATES:
        raise ValueError('Stem sample rate must be 44100 or 96000 Hz')
    return rate


def role_frames(frames_44100, rate=DEFAULT_RATE):
    """Frames in each stored role: the 44.1 kHz frames themselves, or resample_poly(320/147)'s output at 96 kHz."""
    return int(frames_44100) if _rate(rate) == 44100 else -(-int(frames_44100) * 320 // 147)


def frames_from_seconds(seconds):
    return int(round(float(seconds) * 44100))


class SizeModel:
    """Per-role compression ratios for one sample rate: observed medians, else priors."""

    def __init__(self, samples=None, rate=DEFAULT_RATE):
        self.rate = _rate(rate)
        self.samples = {role: list(samples.get(role, [])) if samples else [] for role in ROLES}

    @classmethod
    def load(cls, path, rate=DEFAULT_RATE):
        path = Path(path)
        if not path.is_file():
            return cls(rate=rate)
        try:
            data = json.loads(path.read_text(encoding='utf-8'))
            samples = data.get(SAMPLE_KEYS[_rate(rate)], {}) if isinstance(data, dict) else {}
            samples = samples if isinstance(samples, dict) else {}
            clean = {}
            for role in ROLES:
                values = samples.get(role, [])
                if isinstance(values, list):
                    clean[role] = [float(v) for v in values if isinstance(v, (int, float)) and 0 < v <= 1.5][-MAX_SAMPLES:]
            return cls(clean, rate)
        except (ValueError, OSError):
            return cls(rate=rate)

    def save(self, path):
        """Replace this rate's samples and keep the other rate's."""
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {'version': 1}
        for rate, key in SAMPLE_KEYS.items():
            if rate != self.rate:
                other = SizeModel.load(path, rate).samples
                if any(other.values()):
                    data[key] = other
        data[SAMPLE_KEYS[self.rate]] = self.samples
        handle, pending = tempfile.mkstemp(prefix='.size-model.', suffix='.tmp', dir=path.parent)
        os.close(handle)
        try:
            Path(pending).write_text(json.dumps(data, indent=0), encoding='utf-8')
            os.replace(pending, path)
        finally:
            if Path(pending).exists():
                Path(pending).unlink()

    def ratio(self, role):
        values = self.samples[role]
        return statistics.median(values) if values else PRIORS[self.rate][role]

    def observations(self):
        return min(len(self.samples[role]) for role in ROLES)

    def observe(self, role_bytes, frames):
        """Record one written bundle at this model's rate: stored bytes per role for roles of ``frames`` frames."""
        raw = int(frames) * ROLE_BYTES_PER_FRAME
        if raw <= 0:
            raise ValueError('Observed bundle has no audio')
        for role in ROLES:
            stored = int(role_bytes[role])
            pages = -(-raw // PAGE_BYTES)
            payload = stored - PAGE_HEADER - PAGE_TABLE_ENTRY * pages
            ratio = payload / raw
            if not 0 < ratio <= 1.5:
                raise ValueError(f'Observed {role} size is not plausible')
            self.samples[role] = (self.samples[role] + [round(ratio, 4)])[-MAX_SAMPLES:]


def waveform_bytes(frames, rate=DEFAULT_RATE):
    """The six per-stem waveform files (three PWV7 .2EX, three PWV3 .EXT), each rounded to a cluster.

    Mirrors stem_waveforms.bytes_for_frames without importing numpy into the frozen service;
    test_stem_estimate pins the two together. A column is 1/150 s of role frames at either rate.
    """
    columns = -(-int(frames) // (_rate(rate) // WAVEFORM_COLUMNS_PER_SECOND))
    detail = WAVEFORM_HEADER + 3 * columns
    overview = WAVEFORM_HEADER + columns
    return WAVEFORM_STEMS * (_cluster(detail) + _cluster(overview))


def estimate_track(frames_44100, model=None, extra_bytes_per_second=0, rate=None):
    """Expected USB bytes for one track's bundle, with a low/high band. ``rate`` defaults to the model's."""
    rate = _rate(rate or (model.rate if model else DEFAULT_RATE))
    model = model or SizeModel(rate=rate)
    if model.rate != rate:
        raise ValueError('The size model is for a different sample rate')
    frames = role_frames(frames_44100, rate)
    raw = frames * ROLE_BYTES_PER_FRAME
    pages = -(-raw // PAGE_BYTES)
    seconds = frames / rate
    roles = {}
    for role in ROLES:
        payload = int(raw * model.ratio(role))
        stored = PAGE_HEADER + PAGE_TABLE_ENTRY * pages + payload
        roles[role] = _cluster(stored)
    extra = waveform_bytes(frames, rate) + (_cluster(int(seconds * extra_bytes_per_second)) if extra_bytes_per_second else 0)
    audio = sum(roles.values())
    total = audio + extra + _cluster(MANIFEST_BYTES)
    return {'frames_44100': int(frames_44100), 'sample_rate': rate, 'role_frames': frames, 'raw_bytes': raw * len(ROLES),
            'pages': pages, 'fits_format': pages <= MAX_PAGES and int(frames_44100) <= MAX_SOURCE_FRAMES,
            'roles': roles, 'extra_bytes': extra,
            'bytes': total, 'low_bytes': int(total * (1 - SPREAD)), 'high_bytes': int(total * (1 + SPREAD)),
            'learned': model.observations() > 0, 'observations': model.observations()}


def estimate_selection(estimates, free_bytes, index_bytes=0, reserve_bytes=64 * 1024 * 1024):
    """Totals for a set of per-track estimates against the USB's free space and index room."""
    estimates = list(estimates)
    total = sum(e['bytes'] for e in estimates)
    high = sum(e['high_bytes'] for e in estimates)
    low = sum(e['low_bytes'] for e in estimates)
    index_after = int(index_bytes) + INDEX_ENTRY_BYTES * len(estimates)
    limit = MAX_INDEX_LIMIT if index_bytes > INDEX_LIMIT else INDEX_LIMIT
    return {'tracks': len(estimates), 'bytes': total, 'low_bytes': low, 'high_bytes': high,
            'free_bytes': int(free_bytes), 'remaining_bytes': int(free_bytes) - total,
            'fits': high + reserve_bytes <= int(free_bytes),
            'tight': total + reserve_bytes <= int(free_bytes) < high + reserve_bytes,
            'index_bytes_after': index_after, 'index_fits': index_after <= limit,
            'index_room_tracks': max(0, (limit - int(index_bytes)) // INDEX_ENTRY_BYTES)}


def required_free_bytes(frames_44100, model=None, rate=None):
    """Space to demand before preparing one track: the high estimate plus a margin."""
    return estimate_track(frames_44100, model, rate=rate)['high_bytes'] + 16 * 1024 * 1024


def _cluster(value):
    return -(-int(value) // FILESYSTEM_CLUSTER) * FILESYSTEM_CLUSTER


def model_path(data_root):
    return Path(data_root) / MODEL_FILE


def observe_bundle(folder, frames, data_root, rate=DEFAULT_RATE):
    """Learn from a bundle folder just written to a USB, with ``frames`` role frames at ``rate``.
    Never raises for a bad bundle."""
    folder = Path(folder)
    try:
        sizes = {role: (folder / f'stems-sidecar-{role}.s16le.pgz').stat().st_size for role in ROLES}
        path = model_path(data_root)
        model = SizeModel.load(path, rate)
        model.observe(sizes, frames)
        model.save(path)
        return True
    except (OSError, ValueError, KeyError):
        return False
