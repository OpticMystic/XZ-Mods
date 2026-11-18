# SPDX-License-Identifier: MIT
"""Per-stem scrolling and overview waveforms for a stem bundle, in the files the XZ runtime reads.

mods/ui/layered_wave_source.c (STEMS look) reads stems-{drums,harmonics,vocal}-waveform.2EX: PMAI with a
PWV7 section, 24-byte section header, 3-byte entries (low, mid, high), each stem's bands summed per column.
mods/audio/overcue_file.c load_wave reads stems-{drums,harmonics,vocal}-waveform.EXT: the first PWV3
section, 24-byte header, entry size 1, low 5 bits = height (the per-stem overview strips).

Both run at rekordbox's 150 columns per second from the start of the role PCM: exactly 640 frames per column
at 96 kHz and 294 at 44.1 kHz, so column i of a stem lines up with column i of the track's own ANLZ0000.2EX.

Run in the import environment (numpy, scipy, soundfile):
    python -I stem_waveforms.py --roles <overcue_roles output> --output <folder>
    python -I stem_waveforms.py --bundle <CDJMODS/stems/<bundle>> --scratch <empty folder> --output <folder>
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import zlib

import numpy as np
from scipy.signal import butter, hilbert, resample_poly, sosfiltfilt

STEMS = ('drums', 'harmonics', 'vocal')
RATE = 96000  # the rate of roles that do not state one
COLUMNS_PER_SECOND = 150
COLUMN_FRAMES = RATE // COLUMNS_PER_SECOND
# Crossovers of the low/mid/high families that builder/tests/verify_three_band_order.py matched to
# rekordbox's PWV7 bytes; that script reads them from here.
BAND_HZ = ((30, 250), (250, 2000), (2000, 16000))
FILTER_ORDER = 6
# Each band is analysed at the lowest rate that still holds it: 6 kHz for low and mid, 48 kHz for high at
# 96 kHz; 6.3 kHz and the full 44.1 kHz at 44.1 kHz. Every factor divides the column and the margin.
DECIMATION = (16, 16, 2)
# Zero-phase filtering and the Hilbert envelope settle within this many frames of real context.
MARGIN = 32768
PROFILES = {96000: (DECIMATION, MARGIN), 44100: ((7, 7, 1), 294 * 112)}
CHUNK_COLUMNS = 2048
DETAIL_FULL = 127
OVERVIEW_FULL = 31
SECTION_HEADER = 24
PMAI_HEADER = 28
PMAI_TAIL = bytes.fromhex('00000001000100000001000000000000')
SECTION_TAIL = 0x00960000
PAGE_BYTES = 131072


def detail_file(stem):
    return f'stems-{stem}-waveform.2EX'


def overview_file(stem):
    return f'stems-{stem}-waveform.EXT'


def column_frames(rate):
    if rate not in PROFILES:
        raise ValueError(f'Unsupported stem sample rate {rate}')
    return rate // COLUMNS_PER_SECOND


def columns(frames, rate=RATE):
    return -(-frames // column_frames(rate))


def bytes_for_frames(frames, rate=RATE):
    """Bytes the six waveform files add to a bundle of this many role frames (before cluster slack)."""
    n = columns(frames, rate)
    per_stem = 2 * (PMAI_HEADER + SECTION_HEADER) + 3 * n + n
    return len(STEMS) * per_stem


BYTES_PER_MINUTE = bytes_for_frames(RATE * 60) - bytes_for_frames(0)


def analyse(pcm, scale, rate=RATE):
    """Per-column band amplitudes (n, 3) on 0..127 and overview heights (n,) on 0..31.

    pcm is (frames, 2) int16; scale maps the sum of both channels to full scale 1.0. Each band is a
    zero-phase Butterworth band-pass (so adjacent bands meet at -6 dB and sum flat), and its column value
    is the peak of the band's Hilbert envelope: a full-scale sine fills its band at 127 at any frequency,
    and its overview column at 31.
    """
    frames = len(pcm)
    count = columns(frames, rate)
    step, (decimation, context) = column_frames(rate), PROFILES[rate]
    filters = [butter(FILTER_ORDER, band, 'bandpass', fs=rate / factor, output='sos')
               for band, factor in zip(BAND_HZ, decimation)]
    detail = np.empty((count, 3), np.uint8)
    overview = np.empty(count, np.uint8)
    for first in range(0, count, CHUNK_COLUMNS):
        n = min(count, first + CHUNK_COLUMNS) - first
        start = first * step
        mono = _mono(pcm, start - context, start + n * step + context, scale)
        decimated = {1: mono}
        for factor in sorted(set(decimation) - {1}):
            previous = max(decimated)
            decimated[factor] = resample_poly(decimated[previous], 1, factor // previous)
        for band, (sos, factor) in enumerate(zip(filters, decimation)):
            margin, width = context // factor, step // factor
            envelope = np.abs(hilbert(sosfiltfilt(sos, decimated[factor]).astype(np.float32)))
            peak = envelope[margin:margin + n * width].reshape(n, width).max(axis=1)
            detail[first:first + n, band] = np.minimum(DETAIL_FULL, np.rint(peak * DETAIL_FULL))
        peak = np.abs(mono[context:context + n * step]).reshape(n, step).max(axis=1)
        overview[first:first + n] = np.minimum(OVERVIEW_FULL, np.rint(peak * OVERVIEW_FULL))
    return detail, overview


def _mono(pcm, start, stop, scale):
    out = np.zeros(stop - start, np.float32)
    low, high = max(start, 0), min(stop, len(pcm))
    if high > low:
        out[low - start:high - start] = pcm[low:high].astype(np.float32).sum(axis=1) * np.float32(scale)
    return out


def _section(tag, entry_size, entries, count):
    return tag + struct.pack('>IIIII', SECTION_HEADER, SECTION_HEADER + len(entries), entry_size, count, SECTION_TAIL) + entries


def _anlz(section):
    return b'PMAI' + struct.pack('>II', PMAI_HEADER, PMAI_HEADER + len(section)) + PMAI_TAIL + section


def encode_detail(detail):
    return _anlz(_section(b'PWV7', 3, np.ascontiguousarray(detail, np.uint8).tobytes(), len(detail)))


def encode_overview(overview):
    return _anlz(_section(b'PWV3', 1, np.ascontiguousarray(overview, np.uint8).tobytes(), len(overview)))


def write(stems, headroom_gain, output, rate=RATE):
    """stems maps each STEMS name to (frames, 2) int16 PCM of the same length at ``rate``; writes the six files."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    lengths = {len(stems[stem]) for stem in STEMS}
    if len(lengths) != 1 or not lengths.pop():
        raise ValueError('Stem waveforms need three non-empty stems of the same length')
    if not 0 < headroom_gain <= 1:
        raise ValueError('Headroom gain must be in (0, 1]')
    scale = 1 / (2 * 32768 * headroom_gain)
    with ThreadPoolExecutor(max_workers=len(STEMS)) as pool:
        analysed = dict(zip(STEMS, pool.map(lambda stem: analyse(stems[stem], scale, rate), STEMS)))
    for stem, (detail, overview) in analysed.items():
        (output / detail_file(stem)).write_bytes(encode_detail(detail))
        (output / overview_file(stem)).write_bytes(encode_overview(overview))


class RawPcm:
    """(frames, 2) s16le PCM in a file, read one slice at a time so no handle outlives a read."""

    def __init__(self, path, frames):
        self.path, self.frames = path, frames

    def __len__(self):
        return self.frames

    def __getitem__(self, span):
        start, stop, _ = span.indices(self.frames)
        return np.fromfile(self.path, '<i2', count=max(0, stop - start) * 2, offset=start * 4).reshape(-1, 2)


def _roles(folder):
    folder = Path(folder)
    result = json.loads((folder / 'result.json').read_text(encoding='utf-8'))
    frames = result['frames']
    stems = {}
    for stem in STEMS:
        path = folder / Path(result['roles'][stem]['file']).name
        if path.stat().st_size != frames * 4:
            raise ValueError(f'{path.name}: rendered PCM length does not match result.json')
        stems[stem] = RawPcm(path, frames)
    return stems, float(result['headroom_gain']), int(result.get('sample_rate', RATE))


MAGIC = {96000: (b'OVPGZ001', b'OVPGZ003'), 44100: (b'OVPGZ004',)}


def _expand_role(path, frames, target, rate=RATE):
    """Decode an OVPGZ001 (zlib) or OVPGZ003/OVPGZ004 (FLAC) role to raw s16le, checking every page hash."""
    with open(path, 'rb') as source, open(target, 'wb') as output:
        header = source.read(24)
        magic, (page_bytes, count, total) = header[:8], struct.unpack('>IIQ', header[8:24])
        if magic not in MAGIC[rate] or page_bytes != PAGE_BYTES or total != frames * 4:
            raise ValueError(f'{Path(path).name}: not a stem role of this bundle')
        table = source.read(48 * count)
        for number in range(count):
            offset, compressed, expanded = struct.unpack_from('>QII', table, number * 48)
            source.seek(offset)
            payload = source.read(compressed)
            if magic == b'OVPGZ001':
                page = zlib.decompressobj().decompress(payload, expanded + 1)
            else:
                import soundfile
                pcm, decoded_rate = soundfile.read(io.BytesIO(payload), dtype='int16', always_2d=True)
                if decoded_rate != rate:
                    raise ValueError(f'{Path(path).name}: page {number} is not at {rate} Hz')
                page = pcm.astype('<i2').tobytes()
            if len(page) != expanded or hashlib.sha256(page).digest() != table[number * 48 + 16:number * 48 + 48]:
                raise ValueError(f'{Path(path).name}: page {number} failed verification')
            output.write(page)


def _bundle(folder, scratch):
    folder = Path(folder)
    manifest = json.loads((folder / 'overcue-manifest.json').read_text(encoding='utf-8'))
    runtime = manifest['runtime']
    frames, rate = runtime['frames'], int(runtime.get('sample_rate', RATE))
    if rate not in MAGIC:
        raise ValueError(f'Unsupported stem sample rate {rate}')
    stems = {}
    for stem in STEMS:
        raw = Path(scratch) / f'{stem}.s16le'
        _expand_role(folder / f'stems-sidecar-{stem}.s16le.pgz', frames, raw, rate)
        stems[stem] = RawPcm(raw, frames)
    return stems, float(runtime.get('headroom_gain', 1)), rate


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--roles', type=Path)
    source.add_argument('--bundle', type=Path)
    parser.add_argument('--scratch', type=Path, help='empty folder for the decoded roles; the caller deletes it')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    if args.bundle and not args.scratch:
        parser.error('--bundle needs --scratch')
    args.output.mkdir(parents=True, exist_ok=False)
    stems, gain, rate = _roles(args.roles) if args.roles else _bundle(args.bundle, args.scratch)
    write(stems, gain, args.output, rate)
    print(json.dumps({'status': 'complete', 'output': str(args.output)}))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, struct.error) as error:
        print(json.dumps({'status': 'failed', 'error': str(error)}), file=sys.stderr)
        raise SystemExit(1)
