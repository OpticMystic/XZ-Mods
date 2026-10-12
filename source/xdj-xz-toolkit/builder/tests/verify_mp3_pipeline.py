# SPDX-License-Identifier: MIT
"""End-to-end check: an MP3 in a rekordbox-style USB becomes a verified OverCue bundle.

Builds a staging "USB" (export.pdb fixture + Contents/<track>), runs the real
separation path, verifies the bundle with the native checker, then proves two
things the device cares about and one the user cares about:

- the full-mix role cross-correlates with an independent decode of the MP3
  within the device's +-8192 frame search (r > 0.995);
- every role decodes from its FLAC pages to the indexed hashes;
- the size estimate made before preparing is within the +-20% band of the
  bytes actually written.

Run under a Python with numpy, scipy and soundfile, from the toolkit root:
  python builder/tests/verify_mp3_pipeline.py --mp3 <track.mp3> --resources <dir> --data <dir> --work <dir> [--preset umxhq]
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import time

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))


def build_usb(work, mp3):
    from builder import rekordbox_pdb
    from builder.tests.pdb_fixture import write_export
    usb = work / 'usb'
    if usb.exists():
        shutil.rmtree(usb)
    track = '/Contents/Verify/' + mp3.name
    write_export(usb.joinpath(*rekordbox_pdb.EXPORT_PDB), {7: track})
    target = usb / track.lstrip('/')
    target.parent.mkdir(parents=True)
    shutil.copyfile(mp3, target)
    return usb, track, target


def decode_role(path):
    data = path.read_bytes()
    magic, page_bytes, count, total = data[:8], *struct.unpack('>IIQ', data[8:24])
    assert magic == b'OVPGZ003', magic
    pcm = bytearray()
    for n in range(count):
        offset, comp, exp = struct.unpack('>QII', data[24 + 48 * n:24 + 48 * n + 16])
        page_hash = data[24 + 48 * n + 16:24 + 48 * n + 48]
        decoded, rate = sf.read(io.BytesIO(data[offset:offset + comp]), dtype='int16', always_2d=True)
        raw = decoded.astype('<i2').tobytes()
        assert rate == 96000 and len(raw) == exp and hashlib.sha256(raw).digest() == page_hash, f'page {n} of {path.name}'
        pcm += raw
    assert len(pcm) == total
    return np.frombuffer(bytes(pcm), dtype='<i2').reshape(-1, 2), hashlib.sha256(bytes(pcm)).hexdigest()


def correlate(native, prepared, search=8192):
    n = min(len(native), len(prepared))
    mid = n // 2
    span = min(96000 * 10, n // 2 - search - 1)
    a = native[mid - span:mid + span, 0].astype(np.float64)
    best = (-2.0, 0)
    for lag in range(-search, search + 1, 64):
        b = prepared[mid - span + lag:mid + span + lag, 0].astype(np.float64)
        r = float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12))
        if r > best[0]:
            best = (r, lag)
    coarse = best[1]
    for lag in range(coarse - 64, coarse + 65):
        b = prepared[mid - span + lag:mid + span + lag, 0].astype(np.float64)
        r = float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12))
        if r > best[0]:
            best = (r, lag)
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mp3', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--preset', default='umxhq')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    os.environ['XZ_BUILDER_RESOURCES'] = str(args.resources.resolve())
    os.environ['XZ_BUILDER_DATA'] = str(args.data.resolve())
    os.environ['XZ_AUDIO_HELPER'] = str((args.resources / ('xz-audio-helper.exe' if os.name == 'nt' else 'xz-audio-helper')).resolve())
    from builder import engines, cache, stem_estimate, overcue_writer
    from builder.jobs import Job

    class LoudJob(Job):
        def progress(self, stage, message, **details):
            print(f'[{stage}] {message}', flush=True)

    args.work.mkdir(parents=True, exist_ok=True)
    usb, track_path, source = build_usb(args.work, args.mp3)
    info = cache.inspect_audio(source)
    estimate = stem_estimate.estimate_track(info['frames'], stem_estimate.SizeModel.load(stem_estimate.model_path(args.data)))
    print(f"source: {info['format']} {info['sample_rate']} Hz, {info['source_frames']} frames -> {info['frames']} at 44.1k")
    print(f"estimate before: {estimate['bytes'] / 1e6:.1f} MB (band {estimate['low_bytes'] / 1e6:.1f}-{estimate['high_bytes'] / 1e6:.1f}), learned={estimate['learned']}")
    started = time.time()
    result = engines.separate({'source': str(source), 'preset': args.preset}, LoudJob(), args.resources.resolve())
    elapsed = time.time() - started
    print(f"separated + published in {elapsed:.0f} s: bundle {result['bundle']}, frames {result['frames']}, reused={result.get('reused')}")
    bundle = usb / 'CDJMODS' / 'stems' / result['bundle']
    index, _ = overcue_writer.read_index(usb / 'CDJMODS')
    entry = index['tracks']['7']
    assert entry['file_path'] == track_path and entry['bundle'] == result['bundle']
    actual = sum((bundle / f'stems-sidecar-{role}.s16le.pgz').stat().st_size for role in overcue_writer.ROLES)
    ratio = actual / estimate['bytes']
    print(f"actual role bytes: {actual / 1e6:.1f} MB; actual/estimate = {ratio:.3f}")
    checker = args.resources / ('xz-overcue-check.exe' if os.name == 'nt' else 'xz-overcue-check')
    import subprocess
    verified = json.loads(subprocess.run([str(checker), str(source)], capture_output=True, text=True, check=True).stdout)
    print('native checker:', {k: verified.get(k) for k in ('format', 'verified_mixes', 'all_pages_verified', 'source_identity_verified')})
    roles = {}
    for role in overcue_writer.ROLES:
        pcm, digest = decode_role(bundle / f'stems-sidecar-{role}.s16le.pgz')
        assert digest == entry[role.replace('-', '_') + '_sha256'], role
        roles[role] = pcm
    native44, rate = sf.read(source, dtype='float32', always_2d=True)
    native96 = resample_poly(native44, 320, 147, axis=0)
    native96 = (np.clip(native96, -1, 1) * 32767).astype(np.int16)
    r, lag = correlate(native96, roles['full-mix'])
    timing = cache.inspect_audio(source).get('mp3') or {}
    expected_lag = round(timing.get('delay_frames', 0) * 320 / 147) if timing.get('lame_tag') else 0
    print(f'full-mix alignment vs independent decode: r={r:.5f} at lag {lag} frames (device needs r>0.995 within +-8192)')
    print(f'libsndfile decodes gapless; the bundle keeps every frame like a player, so the expected lag is {expected_lag} (LAME tag: {timing.get("lame_tag")})')
    v, d, h, full = (roles[k].astype(np.int32) for k in ('vocal', 'drums', 'harmonics', 'full-mix'))
    residual = np.abs(v + d + h - full).max()
    print(f'max |vocal+drums+harmonics-full| = {residual} (16-bit steps)')
    learned = stem_estimate.SizeModel.load(stem_estimate.model_path(args.data))
    print('size model observations after run:', learned.observations())
    report = {'mp3': str(args.mp3), 'preset': args.preset, 'seconds': elapsed, 'estimate_bytes': estimate['bytes'],
              'actual_bytes': actual, 'actual_over_estimate': ratio, 'alignment_r': r, 'alignment_lag': lag,
              'sum_residual_steps': int(residual), 'checker': verified, 'bundle': result['bundle'], 'frames': result['frames'],
              'expected_lag': expected_lag, 'mp3_timing': timing}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2))
    ok = r > 0.995 and abs(lag - expected_lag) <= 3 and verified.get('verified_mixes') == 7 and verified.get('all_pages_verified') is True and 0.8 <= ratio <= 1.2
    print('RESULT:', 'VERIFIED' if ok else 'NOT VERIFIED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
