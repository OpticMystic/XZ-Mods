# SPDX-License-Identifier: MIT
"""Render the seven OverCue stem roles at 96 kHz from a mix and two separated parts.

Run in the managed engine environment with ``python -I overcue_roles.py``.
Parts use inference.encode_part's convention: stored PCM = true * gain * 32767.
Writes one raw s16le file per role plus result.json; packing is overcue_writer's job.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys
import wave

import numpy as np
from scipy.signal import resample_poly

SOURCE_RATE, RATE, UP, DOWN = 44100, 96000, 320, 147
MAX_FRAMES = 4096 * 131072 // 4  # OverCue readers cap a role at 4096 pages of 128 KiB.
MAX_SOURCE_FRAMES = MAX_FRAMES * DOWN // UP
CHUNK = 1 << 20
# Spec table order; it also feeds the bundle name. full-mix is the original mix, not a sum.
ROLE_PARTS = (
    ("vocal", ("vocals",)),
    ("instrumental", ("drums", "harmonics")),
    ("drums", ("drums",)),
    ("harmonics", ("harmonics",)),
    ("vocals-drums", ("vocals", "drums")),
    ("vocals-harmonics", ("vocals", "harmonics")),
    ("full-mix", ("mix",)),
)
ROLES = tuple(name for name, _ in ROLE_PARTS)


def read_wav(path, gain=None, target=None):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (2, 2, SOURCE_RATE, "NONE"):
            raise ValueError(f"{Path(path).name}: expected uncompressed stereo PCM16 WAV at 44100 Hz")
        frames = source.getnframes()
        if not 0 < frames <= MAX_SOURCE_FRAMES:
            raise ValueError("Track is empty or longer than the OverCue 4096-page limit (about 23 minutes)")
        audio = np.memmap(target, dtype=np.float32, mode='w+', shape=(frames, 2)) if target else np.empty((frames, 2), dtype=np.float32)
        scale = np.float32(1 / 32768 if gain is None else 1 / (gain * 32767))
        for start in range(0, frames, 65536):
            count = min(65536, frames - start)
            raw = source.readframes(count)
            if len(raw) != count * 4:
                raise ValueError(f"{Path(path).name}: WAV data is truncated")
            audio[start:start + count] = np.frombuffer(raw, dtype='<i2').reshape(count, 2).astype(np.float32) * scale
    return audio


def spill(audio, path):
    """Resample aligned chunks with FIR context; retain the exact whole-file phase and edges."""
    part = np.memmap(path, dtype=np.float32, mode="w+", shape=(-(-len(audio) * UP // DOWN), 2))
    block = DOWN * 2048
    for start in range(0, len(audio), block):
        end = min(len(audio), start + block)
        # Multiples of DOWN preserve phase. One DOWN of context exceeds the default FIR radius.
        first, last = max(0, start - DOWN), min(len(audio), end + DOWN)
        out_first, out_last = start * UP // DOWN, -(-end * UP // DOWN)
        trim = (start - first) * UP // DOWN
        for channel in range(2):
            converted = resample_poly(audio[first:last, channel], UP, DOWN)
            part[out_first:out_last, channel] = converted[trim:trim + out_last - out_first]
    return part


def role_chunks(parts, frames):
    for start in range(0, frames, CHUNK):
        mix, vocals, harmonics = (part[start:start + CHUNK] for part in parts[:3])
        source = {"mix": mix, "vocals": vocals, "harmonics": harmonics, "drums": parts[3][start:start + CHUNK] if len(parts)>3 else mix - vocals - harmonics}
        yield [(name, sum(source[member] for member in members)) for name, members in ROLE_PARTS]


def headroom_gain(peak):
    """One gain for every role so none clips. The device undoes it by aligning the stored
    full mix to its own decode, so each role's manifest loudness_gain stays 1."""
    limit = 32767 / 32768
    gain = np.float32(min(1.0, limit / peak)) if peak else np.float32(1)
    while float(gain) * peak > limit:
        gain = np.nextafter(gain, np.float32(0))
    if not gain > 0:
        raise ValueError("Stem headroom gain cannot be represented")
    return float(gain)


def render(mix_path, vocals_path, vocals_gain, harmonics_path, harmonics_gain, output_dir, drums_path=None, drums_gain=1.0):
    for gain in (vocals_gain, harmonics_gain, drums_gain):
        if not (math.isfinite(gain) and gain > 0):
            raise ValueError("Stem gains must be finite and positive")
    sources = [(mix_path, None), (vocals_path, vocals_gain), (harmonics_path, harmonics_gain)]
    if drums_path is not None:sources.append((drums_path,drums_gain))
    output = Path(output_dir)
    output.mkdir(exist_ok=False)
    parts = []
    try:
        result = _render(sources, output, parts)
    except BaseException:
        for part in parts:part._mmap.close()
        parts.clear()
        shutil.rmtree(output, ignore_errors=True)
        raise
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def _render(sources, output, parts):
    spilled = [output / f".part-{number}.f32" for number in range(len(sources))]
    source_frames = None
    for (path, gain), target in zip(sources, spilled):
        source_map = output / '.source.f32'
        audio = read_wav(path, gain, source_map)
        try:
            if source_frames not in (None, len(audio)):
                raise ValueError("The mix and both stems must have exactly the same frame count")
            source_frames = len(audio)
            parts.append(spill(audio, target))
        finally:audio._mmap.close()
        source_map.unlink()
    frames = len(parts[0])
    if frames > MAX_FRAMES:
        raise ValueError("Track exceeds the OverCue 4096-page limit")
    peak = max(float(np.max(np.abs(audio))) for chunk in role_chunks(parts, frames) for _, audio in chunk)
    gain = headroom_gain(peak)
    scale = np.float32(gain * 32767)
    digests = {name: hashlib.sha256() for name in ROLES}
    files = {name: (output / f"{name}.s16le").open("xb") for name in ROLES}
    try:
        for chunk in role_chunks(parts, frames):
            for name, audio in chunk:
                data = np.rint(audio * scale).astype("<i2").tobytes()
                files[name].write(data)
                digests[name].update(data)
    finally:
        for file in files.values():
            file.close()
    for part in parts:part._mmap.close()
    parts.clear()
    for path in spilled:
        path.unlink()
    return {"frames": frames, "sample_rate": RATE, "channels": 2, "format": "s16le",
            "headroom_gain": gain, "source_frames": source_frames,
            "roles": {name: {"file": f"{name}.s16le", "sha256": digests[name].hexdigest()} for name in ROLES}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mix", required=True, type=Path)
    parser.add_argument("--vocals", required=True, type=Path)
    parser.add_argument("--vocals-gain", required=True, type=float)
    parser.add_argument("--harmonics", required=True, type=Path)
    parser.add_argument("--harmonics-gain", required=True, type=float)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args(argv)
    render(args.mix, args.vocals, args.vocals_gain, args.harmonics, args.harmonics_gain, args.output_dir)
    print(json.dumps({"status": "complete", "result": str(args.output_dir / "result.json")}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, wave.Error) as error:
        print(json.dumps({"status": "failed", "error": str(error)}), file=sys.stderr)
        raise SystemExit(1)
