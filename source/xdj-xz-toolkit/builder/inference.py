"""Offline inference adapter for approved XZ stem models.

Run under a managed, locked Python environment with ``python -I inference.py``.
This adapter never installs packages, downloads models or contacts a device.
The caller owns the trusted packaged manifest and immutable approved directories.

One process serves many tracks: ``--serve`` reads JSON requests on stdin and keeps
loaded models between them, so a batch pays the model load once. Progress and
results are JSON lines on the original stdout; library chatter goes to stderr.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import sys
import time
import types
import wave
import tempfile

import numpy as np

MANIFEST_PATH = Path(__file__).with_name("models.json")
RATE = 44100
# OverCue roles are 96 kHz and capped at 4096 pages of 128 KiB; resampling multiplies frames by 320/147.
MAX_FRAMES = (4096 * 131072 // 4) * 147 // 320
IO_FRAMES = 65536
STAGE_WEIGHTS = {"load-model": 5, "separate": 85, "postprocess": 10}


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


def checked_batch(audio, shape, label):
    array = np.asarray(audio)
    if array.shape != shape or array.dtype.kind not in "fiu" or not np.isfinite(array).all():
        raise ValueError(f"{label}: expected finite estimates shaped {shape}")
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


def encode_parts(mix, estimates, parts, work=None):
    """Encode the requested parts with encode_part's convention; harmonics is the mix minus every estimate."""
    frames = len(mix)
    if not 0 < frames <= MAX_FRAMES:
        raise ValueError("Invalid frame count or OverCue page budget exceeded")
    if not parts or not set(parts) <= {"vocals", "drums", "harmonics"} or any(name not in estimates for name in parts if name != "harmonics"):
        raise ValueError("Parts must be drawn from vocals, drums and harmonics with their estimates present")
    m = checked_audio(mix, frames, "mix")
    sources = {name: checked_audio(audio, frames, name) for name, audio in estimates.items()}
    result = {}
    for name in parts:
        def chunk(start):
            end = start + IO_FRAMES
            if name != "harmonics":
                return sources[name][start:end].astype(np.float32)
            residual = m[start:end].astype(np.float32)
            for audio in sources.values():
                residual = residual - audio[start:end].astype(np.float32)
            return residual
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


def postprocess(mix, vocals, drums, work=None):
    return encode_parts(mix, {"vocals": vocals, "drums": drums}, ("harmonics", "vocals"), work)


def overlap_inference(mix, infer_chunks, targets, chunk_frames, hop_frames, work=None, prefix='sep', batch=1, on_progress=None):
    """Bound model memory by chunks; preserve absolute frame positions with OLA.

    ``infer_chunks`` receives ``(batch, chunk_frames, 2)`` float32 and returns one
    equally shaped array per target.
    """
    if not 0 < hop_frames <= chunk_frames or batch < 1:
        raise ValueError("Invalid inference chunk/hop/batch")
    count = len(mix)
    result = {target: buffer(work, prefix + '-' + target + '.f32', (count, 2)) for target in targets}
    weights = buffer(work, prefix + '-weights.f32', (count,))
    for start in range(0, count, IO_FRAMES):
        weights[start:start + IO_FRAMES] = 0
        for audio in result.values():audio[start:start + IO_FRAMES] = 0
    window = np.sin(np.pi * (np.arange(chunk_frames) + 0.5) / chunk_frames) ** 2
    window = window.astype(np.float32)
    starts = list(range(0, count, hop_frames))
    for first in range(0, len(starts), batch):
        group = starts[first:first + batch]
        chunks = np.zeros((len(group), chunk_frames, 2), dtype=np.float32)
        for index, start in enumerate(group):
            length = min(chunk_frames, count - start)
            chunks[index, :length] = mix[start:start + length]
        estimates = infer_chunks(chunks)
        for target in targets:
            samples = checked_batch(estimates[target], chunks.shape, target)
            for index, start in enumerate(group):
                length = min(chunk_frames, count - start)
                result[target][start:start + length] += samples[index, :length] * window[:length, None]
        for start in group:
            length = min(chunk_frames, count - start)
            weights[start:start + length] += window[:length]
        if on_progress:
            on_progress(min(first + batch, len(starts)), len(starts))
    for target in targets:
        for start in range(0, count, IO_FRAMES):
            result[target][start:start + IO_FRAMES] /= weights[start:start + IO_FRAMES, None]
        checked_audio(result[target], count, target)
    return result


def runtime_versions(registry):
    for name, version in registry["runtime_versions"].items():
        actual = importlib.metadata.version(name)
        if actual.split("+", 1)[0] != version:
            raise ValueError(f"Managed runtime requires {name}=={version}; found {actual}")
    return dict(sorted((dist.metadata["Name"].lower(), dist.version)
                       for dist in importlib.metadata.distributions() if dist.metadata["Name"]))


def vendored_package(code_dir, group):
    """Execute verified pinned sources under their upstream package names, once per process."""
    package = group["package"]
    loaded = sys.modules.get(package)
    if loaded is not None:
        if getattr(loaded, "__xz_vendored__", None) != group["commit"]:
            raise ValueError(f"Module name {package} is already taken; use a fresh isolated adapter process")
        return loaded
    sources = {}
    for name in group["order"]:
        with verified_artifact(code_dir, group["files"][name]) as stream:
            sources[name] = stream.read()
    parents = package.split(".")
    for depth in range(1, len(parents) + 1):
        name = ".".join(parents[:depth])
        if name not in sys.modules:
            module = types.ModuleType(name)
            module.__path__ = []
            module.__xz_vendored__ = group["commit"]
            sys.modules[name] = module
    root = Path(code_dir).resolve()
    for name in group["order"]:
        module_name = package + "." + Path(name).stem
        module = types.ModuleType(module_name)
        module.__file__ = str(root / name)
        sys.modules[module_name] = module
        setattr(sys.modules[package], Path(name).stem, module)
        exec(compile(sources[name], module.__file__, "exec"), module.__dict__)
    return sys.modules[package]


class UmxSeparator:
    """Open-Unmix HQ: all four targets run jointly so the Wiener filter sees the whole mix."""
    def __init__(self, record, registry, model_dir, code_dir, device):
        import torch
        import openunmix
        targets = record["umx_targets"]
        separator = openunmix.umxhq(targets=targets, pretrained=False, device=device, residual=False,
            niter=record["niter"], wiener_win_len=record["wiener_win_len"], filterbank="torch")
        for target in targets:
            name = next(name for name in record["weights"] if name.startswith(target + "-"))
            with verified_artifact(model_dir, registry["models"][name]) as stream:
                state = torch.load(stream, map_location="cpu", weights_only=True)
            # Original HQ checkpoints include preprocessing buffers now owned by Separator.
            for key in ("sample_rate", "stft.window", "transform.0.window"):
                state.pop(key, None)
            separator.target_models[target].load_state_dict(state, strict=True)
            del state
        self.separator, self.device, self.torch = separator.eval(), device, torch

    def begin(self, mix):
        pass

    def infer(self, chunks):
        tensor = self.torch.from_numpy(np.ascontiguousarray(chunks.transpose(0, 2, 1))).to(self.device)
        with self.torch.inference_mode():
            estimates = self.separator.to_dict(self.separator(tensor))
        return {name: estimates[name].cpu().numpy().transpose(0, 2, 1) for name in ("vocals", "drums")}


class HtDemucsSeparator:
    """Hybrid Transformer Demucs with the reference whole-track normalisation."""
    def __init__(self, record, registry, model_dir, code_dir, device):
        import torch
        from demucs.states import load_model
        name = record["weights"][0]
        with verified_artifact(model_dir, registry["models"][name]) as stream:
            package = torch.load(stream, map_location="cpu", weights_only=False)
        model = load_model(package, strict=True)
        del package
        if record["chunk_frames"] > int(model.segment * model.samplerate) or model.samplerate != RATE:
            raise ValueError("Demucs chunk exceeds the training segment")
        self.sources = list(model.sources)
        self.model, self.device, self.torch = model.to(device).eval(), device, torch
        self.offset, self.scale = 0.0, 1.0

    def begin(self, mix):
        total, squares, count = 0.0, 0.0, 0
        for start in range(0, len(mix), IO_FRAMES):
            mono = np.asarray(mix[start:start + IO_FRAMES], dtype=np.float64).mean(axis=1)
            total += float(mono.sum()); squares += float((mono * mono).sum()); count += len(mono)
        mean = total / count
        variance = max(0.0, (squares - count * mean * mean) / max(1, count - 1))
        self.offset, self.scale = mean, variance ** 0.5 + 1e-8

    def infer(self, chunks):
        normalised = (chunks.transpose(0, 2, 1) - np.float32(self.offset)) / np.float32(self.scale)
        tensor = self.torch.from_numpy(np.ascontiguousarray(normalised, dtype=np.float32)).to(self.device)
        with self.torch.inference_mode():
            out = self.model(tensor)
        out = out.cpu().numpy() * np.float32(self.scale) + np.float32(self.offset)
        return {name: out[:, self.sources.index(name)].transpose(0, 2, 1) for name in ("vocals", "drums")}


class MelBandSeparator:
    """Mel-Band RoFormer single-stem model from pinned Music-Source-Separation-Training code."""
    def __init__(self, record, registry, model_dir, code_dir, device):
        import torch
        package = vendored_package(Path(code_dir) / record["code"], registry["code"][record["code"]])
        options = dict(record["model"])
        options["multi_stft_resolutions_window_sizes"] = tuple(options["multi_stft_resolutions_window_sizes"])
        model = package.mel_band_roformer.MelBandRoformer(**options)
        with verified_artifact(model_dir, registry["models"][record["weights"][0]]) as stream:
            state = torch.load(stream, map_location="cpu", weights_only=True)
        model.load_state_dict(state, strict=True)
        del state
        self.model, self.device, self.torch, self.source = model.to(device).eval(), device, torch, record["source"]

    def begin(self, mix):
        pass

    def infer(self, chunks):
        tensor = self.torch.from_numpy(np.ascontiguousarray(chunks.transpose(0, 2, 1))).to(self.device)
        with self.torch.inference_mode():
            out = self.model(tensor)
        return {self.source: out.cpu().numpy().transpose(0, 2, 1)}


FAMILIES = {"umx": UmxSeparator, "htdemucs": HtDemucsSeparator, "melband-roformer": MelBandSeparator}


def gpu_batch(record, vram_bytes):
    if not vram_bytes:
        return 1
    usable = vram_bytes - record["gpu_reserve_bytes"]
    return int(max(1, min(record["max_batch"], usable // record["gpu_bytes_per_item"])))


class Progress:
    """Stage percentages plus one overall figure weighted by the preset's declared pass costs."""
    def __init__(self, emit, passes, registry):
        self.emit = emit
        self.costs = [registry["separators"][item["model"]]["cost"] for item in passes]
        self.done_cost = 0.0

    def stage(self, stage, pct, message, **details):
        if stage == "load-model":
            overall = STAGE_WEIGHTS["load-model"] * pct / 100 if not self.done_cost else self.separated()
        elif stage == "postprocess":
            overall = STAGE_WEIGHTS["load-model"] + STAGE_WEIGHTS["separate"] + STAGE_WEIGHTS["postprocess"] * pct / 100
        else:
            overall = self.separated(details.pop("cost", 0) * pct / 100)
        self.emit({"event": "progress", "stage": stage, "pct": round(pct, 1), "overall": round(overall, 1), "message": message, **details})


    def separated(self, partial=0.0):
        return STAGE_WEIGHTS["load-model"] + STAGE_WEIGHTS["separate"] * (self.done_cost + partial) / sum(self.costs)


class Session:
    """Loaded separators on one device, falling back to the CPU when the accelerator fails at runtime."""
    def __init__(self, registry, preset_id, model_dir, code_dir, device, vram_bytes, emit):
        self.registry, self.preset_id, self.preset = registry, preset_id, registry["presets"][preset_id]
        self.model_dir, self.code_dir, self.device, self.vram_bytes, self.emit = model_dir, code_dir, device, vram_bytes, emit
        self.loaded, self.diagnostics = {}, {}
        for item in self.preset["passes"]:
            record = registry["separators"][item["model"]]
            for name in record["weights"]:
                with verified_artifact(model_dir, registry["models"][name]):pass
        self.versions = runtime_versions(registry)

    def peak_device_bytes(self):
        if self.device != "cuda":
            return None
        import torch
        peak = int(torch.cuda.max_memory_allocated())
        torch.cuda.reset_peak_memory_stats()
        return peak

    def accelerated(self, action):
        try:
            return action()
        except RuntimeError as error:
            if self.device == "cpu":
                raise
            self.diagnostics = {"device_error": str(error)[:2000], "failed_device": self.device, "fallback_device": "cpu"}
            self.emit({"event": "progress", "stage": "device", "pct": 0, "message": "The GPU failed; continuing on the CPU.", "fallback": True})
            self.loaded.clear()
            try:
                import torch
                if torch.cuda.is_available():torch.cuda.empty_cache()
            except ImportError:pass
            self.device = "cpu"
            return action()

    def separator(self, name, progress):
        if name not in self.loaded:
            record = self.registry["separators"][name]
            progress.stage("load-model", 0, f"Loading {record['title']}", model=name, device=self.device)
            if self.device != "cpu":
                import torch
                available = torch.cuda.is_available() if self.device == "cuda" else torch.backends.mps.is_available()
                if not available:
                    raise RuntimeError(f"Requested device {self.device} is not available in this runtime")
            self.loaded[name] = FAMILIES[record["family"]](record, self.registry, self.model_dir, self.code_dir, self.device)
            progress.stage("load-model", 100, f"Loaded {record['title']}", model=name, device=self.device)
        return self.loaded[name]

    def passes_for(self, parts):
        needed = {"drums"} | ({"vocals"} if "vocals" in parts else set())
        selected = []
        for item in self.preset["passes"]:
            if needed & set(item["sources"]):
                selected.append(item)
                needed -= set(item["sources"])
        if needed:
            raise ValueError(f"Preset {self.preset_id} cannot produce {sorted(needed)}")
        return selected

    def run_pass(self, item, mix, work, progress, timing):
        record = self.registry["separators"][item["model"]]
        stage = "separate" if len(item["sources"]) > 1 else "separate-" + item["sources"][0]
        def attempt():
            started = time.monotonic()
            model = self.separator(item["model"], progress)
            timing["load_seconds"] += time.monotonic() - started
            started = time.monotonic()
            model.begin(mix)
            batch = gpu_batch(record, self.vram_bytes) if self.device != "cpu" else 1
            def report(done, total):
                progress.stage(stage, 100 * done / total, f"{record['title']}: chunk {done} of {total}", cost=record["cost"], device=self.device)
            report(0, 1)
            estimates = overlap_inference(mix, model.infer, item["sources"], record["chunk_frames"], record["hop_frames"],
                                          work=work, prefix=item["model"], batch=batch, on_progress=report)
            timing["separate_seconds"] += time.monotonic() - started
            return estimates
        estimates = self.accelerated(attempt)
        progress.done_cost += record["cost"]
        return estimates

    def separate(self, request, work):
        source, output_dir, parts = Path(request["input"]), Path(request["output_dir"]), list(request["parts"])
        if not parts or len(set(parts)) != len(parts) or not set(parts) <= {"vocals", "drums", "harmonics"}:
            raise ValueError("Parts must be distinct names from vocals, drums and harmonics")
        if output_dir.exists() and any(output_dir.iterdir()):
            raise ValueError("Output directory must be empty")
        passes = self.passes_for(parts)
        progress = Progress(self.emit, passes, self.registry)
        timing = {"frames": 0, "load_seconds": 0.0, "separate_seconds": 0.0, "postprocess_seconds": 0.0}
        source_hash = sha256_file(source)
        mix = read_source(source, work)
        if sha256_file(source) != source_hash:
            raise ValueError("Source file changed while reading PCM")
        timing["frames"] = len(mix)
        estimates = {}
        for item in passes:
            estimates.update(self.run_pass(item, mix, work, progress, timing))
        started = time.monotonic()
        progress.stage("postprocess", 0, "Encoding stems")
        # Drums always leave the remainder; vocals only when the caller asked for them (instrumental imports have none).
        encoded = encode_parts(mix, {name: audio for name, audio in estimates.items() if name == "drums" or name in parts}, parts, work)
        result = write_output(output_dir, encoded, len(mix), self.preset_id, self.registry, self.versions, source_hash, self.device)
        if sha256_file(source) != source_hash:
            raise ValueError("Source file changed during inference")
        timing["postprocess_seconds"] = time.monotonic() - started
        timing["peak_device_bytes"] = self.peak_device_bytes()
        progress.stage("postprocess", 100, "Stems encoded")
        result["diagnostics"] = dict(self.diagnostics)
        result["timing"] = timing
        (output_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        return result


def write_output(output_dir, parts, frames, preset_id, registry, versions, source_hash, device):
    if not parts or not set(parts) <= {"harmonics", "vocals", "drums"} or not 0 < frames <= MAX_FRAMES:
        raise ValueError("Output must contain bounded stem parts named vocals, drums or harmonics")
    for name, (pcm, gain) in parts.items():
        if pcm.shape != (frames, 2) or pcm.dtype != np.dtype("<i2") or not np.isfinite(gain) or gain <= 0:
            raise ValueError(f"Invalid encoded part: {name}")
    preset = registry["presets"][preset_id]
    separators = {item["model"]: registry["separators"][item["model"]] for item in preset["passes"]}
    identity = {"preset": preset_id, "definition": preset, "separators": separators,
        "model_hashes": {name: registry["models"][name]["sha256"] for record in separators.values() for name in record["weights"]},
        "code": {record["code"]: registry["code"][record["code"]]["commit"] for record in separators.values() if record.get("code")},
        "pipeline_version": registry["pipeline_version"], "runtime_versions": versions,
        "adapter_sha256": sha256_file(__file__), "device": device}
    digest = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    result = {"schema_version": 2, "preset": preset_id, "model_id": f"xz-{preset_id}-{digest[:8]}",
        "frames": frames, "sample_rate": RATE, "channels": 2, "source_sha256": source_hash,
        "stems": [], "runtime_versions": versions, "licenses": registry["licenses"],
        "alignment_verified": False, "runtime_validation_verified": False, "identity": identity}
    output = Path(output_dir)
    output.mkdir(exist_ok=True)
    if any(output.iterdir()):
        raise FileExistsError(f"Output directory is not empty: {output}")
    for name, (pcm, gain) in parts.items():
        path = output / (name + ".wav")
        with wave.open(str(path), "wb") as file:
            file.setnchannels(2); file.setsampwidth(2); file.setframerate(RATE)
            for start in range(0, frames, IO_FRAMES):file.writeframes(pcm[start:start + IO_FRAMES].tobytes(order='C'))
        result["stems"].append({"name": name, "file": path.name, "gain": gain, "sha256": sha256_file(path)})
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def protocol_writer():
    """Own the original stdout for protocol lines; third-party prints land on stderr."""
    channel = os.fdopen(os.dup(sys.stdout.fileno()), "w", buffering=1, encoding="utf-8")
    sys.stdout = sys.stderr
    def emit(event):
        channel.write(json.dumps(event) + "\n")
        channel.flush()
    return emit


def serve_request(session, request, emit):
    with tempfile.TemporaryDirectory(prefix='xz-inference-pcm-', dir=Path(request["output_dir"]).parent) as temporary:
        with DiskBuffers(temporary) as work:
            result = session.separate(request, work)
    emit({"event": "done", "result": str(Path(request["output_dir"]) / "result.json"), "model_id": result["model_id"],
          "device": session.device, "timing": result["timing"], "diagnostics": result["diagnostics"]})
    return result


def main(argv=None, emit=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--code-dir", required=True, type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda", "mps"), default="cpu")
    parser.add_argument("--vram-bytes", type=int, default=0)
    parser.add_argument("--serve", action="store_true")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--parts", default="vocals,harmonics")
    args = parser.parse_args(argv)
    registry = manifest()
    if args.preset not in registry["presets"]:
        raise ValueError("Unknown separation preset")
    emit = emit or protocol_writer()
    session = Session(registry, args.preset, args.model_dir, args.code_dir, args.device, args.vram_bytes, emit)
    if not args.serve:
        if args.input is None or args.output_dir is None:
            raise ValueError("Give --input and --output-dir, or --serve")
        serve_request(session, {"input": str(args.input), "output_dir": str(args.output_dir), "parts": args.parts.split(",")}, emit)
        return 0
    emit({"event": "ready", "device": session.device})
    for line in sys.stdin:
        if not line.strip():
            continue
        request = json.loads(line)
        if request.get("op") == "quit":
            break
        try:
            serve_request(session, request, emit)
        except (ValueError, OSError, ImportError, RuntimeError, KeyError, wave.Error) as error:
            emit({"event": "failed", "error": f"{type(error).__name__}: {error}"[:4000], "diagnostics": session.diagnostics})
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, ImportError, RuntimeError, KeyError, wave.Error) as error:
        print(json.dumps({"event": "failed", "error": f"{type(error).__name__}: {error}"}), file=sys.stderr)
        raise SystemExit(1)
