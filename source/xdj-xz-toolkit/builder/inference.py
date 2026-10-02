"""Offline inference adapter for approved XZ stem models.

Run under a managed, locked Python environment with ``python -I inference.py``.
This adapter never installs packages, downloads models or contacts a device.
The caller owns the trusted packaged manifest and immutable approved directories.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import importlib.metadata
import json
from pathlib import Path
import sys
import types
import wave
import tempfile

import numpy as np

MANIFEST_PATH = Path(__file__).with_name("models.json")
RATE = 44100
# OverCue roles are 96 kHz and capped at 4096 pages of 128 KiB; resampling multiplies frames by 320/147.
MAX_FRAMES = (4096 * 131072 // 4) * 147 // 320
IO_FRAMES = 65536


class DiskBuffers:
    def __init__(self, root):
        self.root, self.maps = Path(root), []

    def allocate(self, name, shape, dtype):
        array = np.memmap(self.root / name, dtype=dtype, mode='w+', shape=shape)
        self.maps.append(array)
        return array

    def __enter__(self):return self

    def __exit__(self, *error):
        # Tracebacks may retain arrays; close handles before Windows removes scratch files.
        for array in self.maps:array._mmap.close()
        self.maps.clear()


def buffer(work, name, shape, dtype=np.float32):
    return work.allocate(name, shape, dtype) if work is not None else np.empty(shape, dtype=dtype)


def cpu_flex_attention(q, k, v, **options):
    """Pinned Smule batch-independent window/sink mask; keep its exact CPU attention."""
    from torch.nn.attention.flex_attention import flex_attention
    output = q.new_empty((q.shape[0], q.shape[1], q.shape[2], v.shape[3]))
    for first in range(0, q.shape[0], 4):
        last = min(first + 4, q.shape[0])
        output[first:last] = flex_attention(q[first:last], k[first:last], v[first:last], **options)
    return output


def manifest():
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def sha256_file(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


@contextmanager
def verified_artifact(directory, record):
    root = Path(directory).resolve(strict=True)
    name = record["filename"]
    if Path(name).name != name or name in (".", ".."):
        raise ValueError("Artifact filename must be a flat manifest entry")
    candidate = root / name
    if candidate.is_symlink() or candidate.resolve(strict=True).parent != root:
        raise ValueError("Artifact escapes the approved directory")
    with candidate.open("rb") as stream:
        stream.seek(0, 2)
        if stream.tell() != record["bytes"]:
            raise ValueError(f"Artifact size mismatch: {name}")
        stream.seek(0)
        if hashlib.file_digest(stream, "sha256").hexdigest() != record["sha256"]:
            raise ValueError(f"Artifact SHA-256 mismatch: {name}")
        stream.seek(0)
        yield stream


def read_source(path, work=None):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (2, 2, RATE, "NONE"):
            raise ValueError("Input must be uncompressed stereo PCM16 WAV at 44100 Hz")
        frames = source.getnframes()
        if not 0 < frames <= MAX_FRAMES:
            raise ValueError("Input is empty or exceeds the OverCue 4096-page limit (about 23 minutes)")
        audio = buffer(work, 'source.f32', (frames, 2))
        for start in range(0, frames, IO_FRAMES):
            count = min(IO_FRAMES, frames - start)
            raw = source.readframes(count)
            if len(raw) != count * 4:
                raise ValueError("Input WAV is truncated")
            audio[start:start + count] = np.frombuffer(raw, dtype='<i2').reshape(count, 2).astype(np.float32) / 32768.0
    return audio


def checked_audio(audio, frames, label):
    array = np.asarray(audio)
    if array.shape != (frames, 2) or array.dtype.kind not in "fiu":
        raise ValueError(f"{label}: expected exactly {frames} stereo frames")
    for start in range(0, frames, IO_FRAMES):
        if not np.isfinite(array[start:start + IO_FRAMES]).all():
            raise ValueError(f"{label}: non-finite samples")
    return array


def encode_part(audio):
    peak = float(np.max(np.abs(audio)))
    gain = np.float32(min(1.0, 1.0 / peak)) if peak else np.float32(1.0)
    while float(gain) * peak > 1.0:
        gain = np.nextafter(gain, np.float32(0))
    if not gain > 0 or float(gain) < 1.0 / np.finfo(np.float32).max:
        raise ValueError("Stem gain cannot be represented by the native float mixer")
    stored = np.rint(np.asarray(audio, dtype=np.float32) * np.float32(float(gain) * 32767.0))
    if not np.isfinite(stored).all() or np.max(np.abs(stored)) > 32767:
        raise ValueError("Stem encoding would clip")
    return stored.astype("<i2"), float(gain)


def postprocess(mix, vocals, drums, work=None):
    frames = len(mix)
    if not 0 < frames <= MAX_FRAMES:
        raise ValueError("Invalid frame count or OverCue page budget exceeded")
    m, v, d = (checked_audio(audio, frames, name) for audio, name in ((mix,'mix'),(vocals,'vocals'),(drums,'drums')))
    result = {}
    for name in ('harmonics', 'vocals'):
        def chunk(start):
            end = start + IO_FRAMES
            vocal = v[start:end].astype(np.float32)
            return m[start:end].astype(np.float32) - vocal - d[start:end].astype(np.float32) if name == 'harmonics' else vocal
        peak = max(float(np.max(np.abs(chunk(start)))) for start in range(0, frames, IO_FRAMES))
        gain = np.float32(min(1.0, 1.0 / peak)) if peak else np.float32(1.0)
        while float(gain) * peak > 1.0:
            gain = np.nextafter(gain, np.float32(0))
        if not gain > 0 or float(gain) < 1.0 / np.finfo(np.float32).max:
            raise ValueError('Stem gain cannot be represented by the native float mixer')
        pcm = buffer(work, name + '.s16', (frames, 2), '<i2')
        for start in range(0, frames, IO_FRAMES):
            stored = np.rint(chunk(start) * np.float32(float(gain) * 32767.0))
            if not np.isfinite(stored).all() or np.max(np.abs(stored)) > 32767:
                raise ValueError('Stem encoding would clip')
            pcm[start:start + len(stored)] = stored.astype('<i2')
        result[name] = pcm, float(gain)
    return result


def overlap_inference(mix, infer_chunk, targets, chunk_frames, hop_frames, work=None, prefix='umx'):
    """Bound model memory by chunks; preserve absolute frame positions with OLA."""
    if not 0 < hop_frames <= chunk_frames:
        raise ValueError("Invalid inference chunk/hop")
    count = len(mix)
    result = {target: buffer(work, prefix + '-' + target + '.f32', (count, 2)) for target in targets}
    weights = buffer(work, prefix + '-weights.f32', (count,))
    for start in range(0, count, IO_FRAMES):
        weights[start:start + IO_FRAMES] = 0
        for audio in result.values():audio[start:start + IO_FRAMES] = 0
    window = np.sin(np.pi * (np.arange(chunk_frames) + 0.5) / chunk_frames) ** 2
    window = window.astype(np.float32)
    for start in range(0, count, hop_frames):
        length = min(chunk_frames, count - start)
        chunk = np.zeros((chunk_frames, 2), dtype=np.float32)
        chunk[:length] = mix[start:start + length]
        estimates = infer_chunk(chunk)
        for target in targets:
            samples = checked_audio(estimates[target], chunk_frames, target)
            result[target][start:start + length] += samples[:length] * window[:length, None]
        weights[start:start + length] += window[:length]
    for target in targets:
        for start in range(0, count, IO_FRAMES):
            result[target][start:start + IO_FRAMES] /= weights[start:start + IO_FRAMES, None]
        checked_audio(result[target], count, target)
    return result


def runtime_versions(preset, registry):
    expected = dict(registry["runtime_versions"])
    if preset == "vocal-focus":
        expected.update(registry["vocal_focus_dependencies"])
    for name, version in expected.items():
        actual = importlib.metadata.version(name)
        if actual.split("+", 1)[0] != version:
            raise ValueError(f"Managed runtime requires {name}=={version}; found {actual}")
    return dict(sorted((dist.metadata["Name"].lower(), dist.version)
                       for dist in importlib.metadata.distributions() if dist.metadata["Name"]))


def run_umx(mix, model_dir, preset, registry, device, work=None):
    import torch
    import openunmix

    targets = preset["umx_targets"]
    separator = openunmix.umxhq(targets=targets, pretrained=False, device=device,
        residual=preset["umx_residual"], niter=preset["niter"],
        wiener_win_len=preset["wiener_win_len"], filterbank="torch")
    for target in targets:
        name = next(name for name in preset["models"] if name.startswith(target + "-"))
        with verified_artifact(model_dir, registry["models"][name]) as stream:
            state = torch.load(stream, map_location="cpu", weights_only=True)
        # Original HQ checkpoints include preprocessing buffers now owned by Separator.
        # Remove only those known legacy keys; learned parameters still load strictly.
        for key in ("sample_rate", "stft.window", "transform.0.window"):
            state.pop(key, None)
        separator.target_models[target].load_state_dict(state, strict=True)
        del state
    separator.eval()

    def infer(chunk):
        tensor = torch.from_numpy(chunk.T.copy()).unsqueeze(0).to(device)
        with torch.inference_mode():
            estimates = separator.to_dict(separator(tensor))
        return {name: estimates[name][0].detach().cpu().numpy().T.copy() for name in stored_targets}

    # Keep all target models in the joint separator; only vocals/drums need disk accumulators.
    stored_targets = [name for name in targets if name in ('vocals', 'drums')]
    return overlap_inference(mix, infer, stored_targets, preset['umx_chunk_frames'], preset['umx_hop_frames'], work=work)


@contextmanager
def smule_modules(code_dir, registry):
    names = ("flex_attention_utils", "modules", "model", "main")
    if any(name in sys.modules for name in names):
        raise ValueError("Smule module names already loaded; use a fresh isolated adapter process")
    sources = {}
    for name in names:
        record = registry["smule_code"]["files"][name + ".py"]
        with verified_artifact(code_dir, record) as stream:
            sources[name] = stream.read()
    loaded = []
    try:
        for name in names:
            module = types.ModuleType(name)
            module.__file__ = str(Path(code_dir).resolve() / (name + ".py"))
            sys.modules[name] = module
            loaded.append(name)
            exec(compile(sources[name], module.__file__, "exec"), module.__dict__)
        yield sys.modules["main"], sys.modules["model"]
    finally:
        for name in loaded:
            sys.modules.pop(name, None)


def run_smule(mix, model_dir, code_dir, preset, registry, device, work=None):
    import torch

    with smule_modules(code_dir, registry) as (api, architecture):
        model = architecture.MelBandRoformerWSA()
        with verified_artifact(model_dir, registry["models"]["mbr-win10-sink8.ckpt"]) as stream:
            state = torch.load(stream, map_location="cpu", weights_only=True)
        model.load_state_dict(state, strict=True)
        del state
        model = model.to(device).eval()
        if device == "cpu":
            # The upstream compiled FlexAttention kernel requires an accelerator.
            # Keep its exact mask and attention operation, using PyTorch's eager path on CPU.
            for module in model.modules():
                if isinstance(module, sys.modules["flex_attention_utils"].FlexAttention):
                    module.flex_attn_fn = cpu_flex_attention
        def infer(chunk):
            tensor = torch.from_numpy(chunk.T.copy())
            output = api.demix(model, tensor, device=device, sample_rate=RATE,
                chunk_size=preset['smule_chunk_seconds'], batch_size=1)
            return {'vocals': checked_audio(output.detach().cpu().numpy().T, len(chunk), 'Smule vocals').copy()}
        chunk = RATE * preset['smule_chunk_seconds']
        return overlap_inference(mix, infer, ['vocals'], chunk, chunk // 2, work=work, prefix='smule')['vocals']


def write_output(output_dir, parts, frames, preset_id, registry, versions, source_hash, device):
    if set(parts) != {"harmonics", "vocals"} or not 0 < frames <= MAX_FRAMES:
        raise ValueError("Output must contain exactly two bounded stem parts")
    for name, (pcm, gain) in parts.items():
        if pcm.shape != (frames, 2) or pcm.dtype != np.dtype("<i2") or not np.isfinite(gain) or gain <= 0:
            raise ValueError(f"Invalid encoded part: {name}")
    identity = {"preset": preset_id, "definition": registry["presets"][preset_id],
        "model_hashes": {name: registry["models"][name]["sha256"] for name in registry["presets"][preset_id]["models"]},
        "pipeline_version": registry["pipeline_version"], "runtime_versions": versions,
        "adapter_sha256": sha256_file(__file__), "device": device,
        "smule_code": registry["smule_code"] if preset_id == "vocal-focus" else None}
    digest = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    result = {"schema_version": 1, "preset": preset_id, "model_id": f"xz-{preset_id}-{digest[:8]}",
        "frames": frames, "sample_rate": RATE, "channels": 2, "source_sha256": source_hash,
        "stems": [], "runtime_versions": versions, "licenses": registry["licenses"],
        "alignment_verified": False, "runtime_validation_verified": False, "identity": identity}
    output = Path(output_dir)
    output.mkdir(exist_ok=False)
    for name, (pcm, gain) in parts.items():
        path = output / (name + ".wav")
        with wave.open(str(path), "wb") as file:
            file.setnchannels(2); file.setsampwidth(2); file.setframerate(RATE)
            for start in range(0, frames, IO_FRAMES):file.writeframes(pcm[start:start + IO_FRAMES].tobytes(order='C'))
        result["stems"].append({"name": name, "file": path.name, "gain": gain, "sha256": sha256_file(path)})
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def execute(args, registry, source_hash, work):
    mix = read_source(args.input, work)
    if sha256_file(args.input) != source_hash:
        raise ValueError('Source file changed while reading PCM')
    preset = registry['presets'][args.preset]
    for name in preset['models']:
        with verified_artifact(args.model_dir, registry['models'][name]):pass
    versions = runtime_versions(args.preset, registry)
    estimates = run_umx(mix, args.model_dir, preset, registry, args.device, work)
    vocals = estimates['vocals'] if args.preset == 'umxhq' else run_smule(
        mix, args.model_dir, args.smule_code, preset, registry, args.device, work)
    parts = postprocess(mix, vocals, estimates['drums'], work)
    if sha256_file(args.input) != source_hash:
        raise ValueError('Source file changed during inference')
    return write_output(args.output_dir, parts, len(mix), args.preset, registry, versions, source_hash, args.device)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True, choices=("umxhq", "vocal-focus"))
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--smule-code", type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    args = parser.parse_args(argv)
    registry = manifest()
    if args.output_dir.exists():
        raise ValueError("Output directory must not already exist")
    if args.preset == "vocal-focus" and args.smule_code is None:
        raise ValueError("Vocal focus requires --smule-code")
    source_hash = sha256_file(args.input)
    with tempfile.TemporaryDirectory(prefix='xz-inference-pcm-', dir=args.output_dir.parent) as temporary:
        with DiskBuffers(temporary) as work:
            result = execute(args, registry, source_hash, work)
    print(json.dumps({"status": "complete", "result": str(args.output_dir / "result.json"), "model_id": result["model_id"]}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, ImportError, RuntimeError, wave.Error) as error:
        print(json.dumps({"status": "failed", "error": str(error)}), file=sys.stderr)
        raise SystemExit(1)
