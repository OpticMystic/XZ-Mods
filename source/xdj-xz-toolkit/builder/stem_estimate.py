# SPDX-License-Identifier: MIT
"""Storage estimates for OverCue stem bundles, learned from bundles written here.

Seven 96 kHz stereo 16-bit roles are 384,000 bytes per second each before
FLAC pages. Compression depends on the music: sparse roles (vocal, drums)
shrink more than the full mix. Priors come from a measured real track; every
bundle this machine writes refines them per role.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import statistics
import tempfile

ROLES = ('vocal', 'instrumental', 'drums', 'harmonics', 'vocals-drums', 'vocals-harmonics', 'full-mix')
ROLE_RATE = 96000
ROLE_BYTES_PER_FRAME = 4
PAGE_BYTES = 131072
MAX_PAGES = 4096
PAGE_TABLE_ENTRY = 48
PAGE_HEADER = 24
MANIFEST_BYTES = 2600
INDEX_ENTRY_BYTES = 1200
INDEX_LIMIT = 4 * 1024 * 1024
FILESYSTEM_CLUSTER = 32768
# FLAC page size over raw PCM, measured on a real 44.1 kHz master upsampled to
# 96 kHz (full mix 0.40). Partial roles are sparser; these start a little high
# so first estimates err on the safe side until observations replace them.
PRIORS = {'vocal': 0.28, 'drums': 0.32, 'harmonics': 0.38, 'instrumental': 0.43,
          'vocals-drums': 0.40, 'vocals-harmonics': 0.42, 'full-mix': 0.45}
SPREAD = 0.20
MAX_SAMPLES = 50
MODEL_FILE = 'stem-size-model.json'
WAVEFORM_COLUMN_FRAMES = 640
WAVEFORM_HEADER = 28 + 24
WAVEFORM_STEMS = 3


def role_frames(frames_44100):
    return -(-int(frames_44100) * 320 // 147)


def frames_from_seconds(seconds):
    return int(round(float(seconds) * 44100))


class SizeModel:
    """Per-role compression ratios: observed medians, else priors."""

    def __init__(self, samples=None):
        self.samples = {role: list(samples.get(role, [])) if samples else [] for role in ROLES}

    @classmethod
    def load(cls, path):
        path = Path(path)
        if not path.is_file():
            return cls()
        try:
            data = json.loads(path.read_text(encoding='utf-8'))
            samples = data.get('samples', {}) if isinstance(data, dict) else {}
            clean = {}
            for role in ROLES:
                values = samples.get(role, [])
                if isinstance(values, list):
                    clean[role] = [float(v) for v in values if isinstance(v, (int, float)) and 0 < v <= 1.5][-MAX_SAMPLES:]
            return cls(clean)
        except (ValueError, OSError):
            return cls()

    def save(self, path):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        handle, pending = tempfile.mkstemp(prefix='.size-model.', suffix='.tmp', dir=path.parent)
        os.close(handle)
        try:
            Path(pending).write_text(json.dumps({'version': 1, 'samples': self.samples}, indent=0), encoding='utf-8')
            os.replace(pending, path)
        finally:
            if Path(pending).exists():
                Path(pending).unlink()

    def ratio(self, role):
        values = self.samples[role]
        return statistics.median(values) if values else PRIORS[role]

    def observations(self):
        return min(len(self.samples[role]) for role in ROLES)

    def observe(self, role_bytes, frames_96k):
        """Record one written bundle: stored bytes per role for a role of frames_96k frames."""
        raw = int(frames_96k) * ROLE_BYTES_PER_FRAME
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


def waveform_bytes(frames_96k):
    """The six per-stem waveform files (three PWV7 .2EX, three PWV3 .EXT), each rounded to a cluster.

    Mirrors stem_waveforms.bytes_for_frames without importing numpy into the frozen service;
    test_stem_estimate pins the two together.
    """
    columns = -(-int(frames_96k) // WAVEFORM_COLUMN_FRAMES)
    detail = WAVEFORM_HEADER + 3 * columns
    overview = WAVEFORM_HEADER + columns
    return WAVEFORM_STEMS * (_cluster(detail) + _cluster(overview))


def estimate_track(frames_44100, model=None, extra_bytes_per_second=0):
    """Expected USB bytes for one track's bundle, with a low/high band."""
    model = model or SizeModel()
    frames = role_frames(frames_44100)
    raw = frames * ROLE_BYTES_PER_FRAME
    pages = -(-raw // PAGE_BYTES)
    seconds = frames / ROLE_RATE
    roles = {}
    for role in ROLES:
        payload = int(raw * model.ratio(role))
        stored = PAGE_HEADER + PAGE_TABLE_ENTRY * pages + payload
        roles[role] = _cluster(stored)
    extra = waveform_bytes(frames) + (_cluster(int(seconds * extra_bytes_per_second)) if extra_bytes_per_second else 0)
    audio = sum(roles.values())
    total = audio + extra + _cluster(MANIFEST_BYTES)
    return {'frames_44100': int(frames_44100), 'frames_96k': frames, 'raw_bytes': raw * len(ROLES),
            'pages': pages, 'fits_format': pages <= MAX_PAGES, 'roles': roles, 'extra_bytes': extra,
            'bytes': total, 'low_bytes': int(total * (1 - SPREAD)), 'high_bytes': int(total * (1 + SPREAD)),
            'learned': model.observations() > 0, 'observations': model.observations()}


def estimate_selection(estimates, free_bytes, index_bytes=0, reserve_bytes=64 * 1024 * 1024):
    """Totals for a set of per-track estimates against the USB's free space and index room."""
    estimates = list(estimates)
    total = sum(e['bytes'] for e in estimates)
    high = sum(e['high_bytes'] for e in estimates)
    low = sum(e['low_bytes'] for e in estimates)
    index_after = int(index_bytes) + INDEX_ENTRY_BYTES * len(estimates)
    return {'tracks': len(estimates), 'bytes': total, 'low_bytes': low, 'high_bytes': high,
            'free_bytes': int(free_bytes), 'remaining_bytes': int(free_bytes) - total,
            'fits': high + reserve_bytes <= int(free_bytes),
            'tight': total + reserve_bytes <= int(free_bytes) < high + reserve_bytes,
            'index_bytes_after': index_after, 'index_fits': index_after <= INDEX_LIMIT,
            'index_room_tracks': max(0, (INDEX_LIMIT - int(index_bytes)) // INDEX_ENTRY_BYTES)}


def required_free_bytes(frames_44100, model=None):
    """Space to demand before preparing one track: the high estimate plus a margin."""
    return estimate_track(frames_44100, model)['high_bytes'] + 16 * 1024 * 1024


def _cluster(value):
    return -(-int(value) // FILESYSTEM_CLUSTER) * FILESYSTEM_CLUSTER


def model_path(data_root):
    return Path(data_root) / MODEL_FILE


def observe_bundle(folder, frames_96k, data_root):
    """Learn from a bundle folder just written to a USB. Never raises for a bad bundle."""
    folder = Path(folder)
    try:
        sizes = {role: (folder / f'stems-sidecar-{role}.s16le.pgz').stat().st_size for role in ROLES}
        path = model_path(data_root)
        model = SizeModel.load(path)
        model.observe(sizes, frames_96k)
        model.save(path)
        return True
    except (OSError, ValueError, KeyError):
        return False
