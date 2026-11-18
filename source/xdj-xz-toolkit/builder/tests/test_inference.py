"""Offline tests: stdlib + NumPy only, no torch/model/network/device execution."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
from unittest import mock
import wave

import numpy as np

ADAPTER = Path(__file__).resolve().parents[1] / "inference.py"
spec = importlib.util.spec_from_file_location("xz_builder_inference", ADAPTER)
inference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inference)


def write_wav(path, samples, rate=44100, channels=2):
    with wave.open(str(path), "wb") as target:
        target.setnchannels(channels)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(np.asarray(samples, dtype="<i2").tobytes())


def code_group(root, sources, package="xz_test_vendor.pkg"):
    files = {}
    for name, source in sources.items():
        (root / name).write_bytes(source)
        files[name] = {"filename": name, "bytes": len(source), "sha256": hashlib.sha256(source).hexdigest()}
    return {"package": package, "commit": "deadbeef", "order": list(sources), "files": files}


class ManifestTests(unittest.TestCase):
    def test_vendored_source_verified_before_any_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = b"raise RuntimeError('source executed before integrity check')\n"
            group = code_group(root, {"first.py": source, "second.py": source})
            group["files"]["second.py"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                inference.vendored_package(root, group)
            self.assertNotIn("xz_test_vendor", sys.modules)

    def test_vendored_package_resolves_upstream_import_names_once(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            group = code_group(root, {"attend.py": b"VALUE = 7\n",
                                      "model.py": b"from xz_test_vendor.pkg.attend import VALUE\nDOUBLE = VALUE * 2\n"})
            try:
                package = inference.vendored_package(root, group)
                self.assertEqual(package.model.DOUBLE, 14)
                self.assertIs(inference.vendored_package(root, group), package)
                with self.assertRaisesRegex(ValueError, "already taken"):
                    inference.vendored_package(root, {**group, "commit": "other"})
            finally:
                for name in [name for name in sys.modules if name.startswith("xz_test_vendor")]:
                    del sys.modules[name]

    def test_only_approved_presets_and_original_licenses(self):
        registry = inference.manifest()
        self.assertEqual(set(registry["presets"]), {"balanced", "fast", "umxhq"})
        self.assertEqual(registry["default_preset"], "balanced")
        self.assertEqual(registry["sample_rate"], 44100)
        self.assertEqual(registry["channels"], 2)
        self.assertEqual(registry["max_combined_pcm_bytes"], inference.MAX_FRAMES * 8)
        self.assertEqual([item["model"] for item in registry["presets"]["balanced"]["passes"]],
                         ["melband-roformer-kj-vocals", "htdemucs-ft-drums"])
        self.assertEqual(registry["presets"]["fast"]["passes"][0]["sources"], ["vocals", "drums"])
        self.assertEqual(registry["separators"]["umxhq"]["umx_targets"], ["vocals", "drums", "bass", "other"])
        for preset in registry["presets"].values():
            self.assertEqual(preset["harmonics"], "mix-vocals-drums")
            produced = set()
            for item in preset["passes"]:
                record = registry["separators"][item["model"]]
                self.assertIn(record["family"], inference.FAMILIES)
                self.assertGreater(record["cost"], 0)
                self.assertLessEqual(record["hop_frames"], record["chunk_frames"])
                produced |= set(item["sources"])
                for name in record["weights"]:
                    self.assertNotIn("roformer-sw", name.lower())
                    self.assertNotIn("umxl", name.lower())
                    weight = registry["models"][name]
                    self.assertEqual(weight["filename"], name)
                    self.assertRegex(weight["sha256"], r"^[0-9a-f]{64}$")
                    self.assertTrue(weight["url"].startswith("https://"))
                    license_record = registry["licenses"][weight["license"]]
                    self.assertEqual(license_record["spdx"], "MIT")
                    self.assertIn("Permission is hereby granted", license_record["text"])
                    self.assertTrue(license_record.get("weights_evidence_url"))
                if record.get("code"):
                    group = registry["code"][record["code"]]
                    self.assertEqual(registry["licenses"][group["license"]]["spdx"], "MIT")
                    self.assertEqual(set(group["order"]), set(group["files"]))
                    for file in group["files"].values():
                        self.assertIn(group["commit"], file["url"])
                        self.assertRegex(file["sha256"], r"^[0-9a-f]{64}$")
            self.assertEqual(produced, {"vocals", "drums"})
            for device in ("cuda", "mps", "cpu"):
                self.assertGreater(preset["prior_seconds_per_source_second"][device], 0)
        self.assertEqual(registry["models"]["drums-9619578f.pth"]["sha256"],
            "9619578f885c54737cb0234f9f9a4a679ee4f31438fd77fd1dbe02bb16c2da0a")
        self.assertEqual(registry["models"]["MelBandRoformer.ckpt"]["sha256"],
            "87201f4d31afb5bc79993230fc49446918425574db48c01c405e44f365c7559e")
        self.assertTrue(registry["torch"]["flavours"]["cu128"]["cuda"])
        self.assertFalse(registry["torch"]["flavours"]["cpu"]["cuda"])

    def test_verify_before_use_and_reject_path_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "model.pth").write_bytes(b"trusted bytes")
            record = {"filename": "model.pth", "bytes": 13,
                      "sha256": hashlib.sha256(b"trusted bytes").hexdigest()}
            with inference.verified_artifact(root, record) as handle:
                self.assertEqual(handle.read(), b"trusted bytes")
            (root / "model.pth").write_bytes(b"changed bytes")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                with inference.verified_artifact(root, record):
                    self.fail("Corrupt checkpoint reached caller")
            record["filename"] = "../outside.pth"
            with self.assertRaisesRegex(ValueError, "flat"):
                with inference.verified_artifact(root, record):
                    self.fail("Escaping checkpoint reached caller")


class InputTests(unittest.TestCase):
    def test_exact_pcm_shape_scaling_and_source_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "source.wav"
            data = np.array([[32767, -32768], [123, -456]], dtype="<i2")
            write_wav(path, data)
            original = path.read_bytes()
            output = inference.read_source(path)
            np.testing.assert_array_equal(output, data.astype(np.float32) / 32768)
            self.assertEqual(path.read_bytes(), original)
            write_wav(path, data, rate=48000)
            with self.assertRaisesRegex(ValueError, "44100"):
                inference.read_source(path)

    def test_cap_checked_from_header_before_large_read(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "huge.wav"
            data_bytes = (inference.MAX_FRAMES + 1) * 4
            path.write_bytes(struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", data_bytes + 36,
                b"WAVE", b"fmt ", 16, 1, 2, 44100, 176400, 4, 16, b"data", data_bytes))
            with self.assertRaisesRegex(ValueError, "4096-page"):
                inference.read_source(path)

    def test_missing_artifact_rejected_before_runtime(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source.wav"
            write_wav(source, np.zeros((10, 2), dtype="<i2"))
            events = []
            with mock.patch.object(inference, "runtime_versions", side_effect=AssertionError("runtime imported")):
                with self.assertRaises(FileNotFoundError):
                    inference.main(["--preset", "umxhq", "--input", str(source), "--model-dir", str(root),
                                    "--code-dir", str(root), "--output-dir", str(root / "output")], emit=events.append)
            self.assertFalse((root / "output").exists())
            self.assertEqual(events, [])


class PostprocessTests(unittest.TestCase):
    def test_residual_mapping_and_per_stem_gain_restore(self):
        mix = np.array([[0.5, -0.25], [0.1, -0.1]])
        vocals = np.array([[2.0, -2.0], [0.5, -0.5]])
        drums = np.array([[2.0, -1.0], [0.2, -0.2]])
        parts = inference.postprocess(mix, vocals, drums)
        self.assertEqual(set(parts), {"harmonics", "vocals"})
        h_pcm, h_gain = parts["harmonics"]
        v_pcm, v_gain = parts["vocals"]
        self.assertLess(h_gain, v_gain)
        self.assertLess(v_gain, 1)
        h = h_pcm.astype(np.float64) / 32767 / h_gain
        v = v_pcm.astype(np.float64) / 32767 / v_gain
        np.testing.assert_allclose(h, mix - vocals - drums, atol=1e-4)
        np.testing.assert_allclose(v, vocals, atol=1e-4)
        np.testing.assert_allclose(mix - h - v, drums, atol=1e-4)
        for pcm, gain in parts.values():
            self.assertLessEqual(int(np.abs(pcm.astype(np.int32)).max()), 32767)
            self.assertEqual(np.float32(f"{gain:.9g}"), np.float32(gain))

    def test_encode_parts_drums_and_remainder_for_instrumental_imports(self):
        mix = np.array([[0.5, -0.25], [0.1, -0.1]])
        drums = np.array([[0.2, -0.1], [0.3, -0.3]])
        parts = inference.encode_parts(mix, {"drums": drums}, ("drums", "harmonics"))
        self.assertEqual(list(parts), ["drums", "harmonics"])
        d = parts["drums"][0].astype(np.float64) / 32767 / parts["drums"][1]
        h = parts["harmonics"][0].astype(np.float64) / 32767 / parts["harmonics"][1]
        np.testing.assert_allclose(d, drums, atol=1e-4)
        np.testing.assert_allclose(h, mix - drums, atol=1e-4)
        with self.assertRaisesRegex(ValueError, "estimates present"):
            inference.encode_parts(mix, {"drums": drums}, ("vocals",))

    def test_invalid_shapes_nonfinite_and_budget(self):
        mix = np.zeros((3, 2))
        with self.assertRaisesRegex(ValueError, "exactly"):
            inference.postprocess(mix, np.zeros((2, 2)), mix)
        broken = mix.copy(); broken[0, 0] = np.nan
        with self.assertRaisesRegex(ValueError, "non-finite"):
            inference.postprocess(mix, broken, mix)
        with mock.patch.object(inference, "MAX_FRAMES", 2):
            with self.assertRaisesRegex(ValueError, "budget"):
                inference.postprocess(mix, mix, mix)
        parts = inference.postprocess(mix, mix, mix)
        self.assertEqual(parts["vocals"][1], 1)

    def test_overlap_keeps_edges_length_channels_and_input(self):
        mix = np.arange(74, dtype=np.float32).reshape(37, 2) / 100
        original = mix.copy()
        calls, reports = [], []
        def model(chunks):
            calls.append(chunks.shape)
            return {"vocals": chunks * 0.25, "drums": chunks * 0.5}
        output = inference.overlap_inference(mix, model, ["vocals", "drums"], 16, 8, on_progress=lambda d, t: reports.append((d, t)))
        np.testing.assert_allclose(output["vocals"], mix * 0.25, atol=1e-7)
        np.testing.assert_allclose(output["drums"], mix * 0.5, atol=1e-7)
        np.testing.assert_array_equal(mix, original)
        self.assertEqual(calls, [(1, 16, 2)] * 5)
        self.assertEqual(reports, [(1, 5), (2, 5), (3, 5), (4, 5), (5, 5)])
        calls.clear(); reports.clear()
        batched = inference.overlap_inference(mix, model, ["vocals", "drums"], 16, 8, batch=2, on_progress=lambda d, t: reports.append((d, t)))
        np.testing.assert_allclose(batched["vocals"], output["vocals"], atol=1e-7)
        self.assertEqual(calls, [(2, 16, 2), (2, 16, 2), (1, 16, 2)])
        self.assertEqual(reports, [(2, 5), (4, 5), (5, 5)])
        with self.assertRaisesRegex(ValueError, "finite"):
            inference.overlap_inference(mix, lambda chunks: {"vocals": chunks[:, :8]}, ["vocals"], 16, 8)

    def test_new_directory_and_identity(self):
        registry = inference.manifest()
        mix = np.zeros((3, 2))
        parts = inference.postprocess(mix, mix, mix)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a = inference.write_output(root / "a", parts, 3, "balanced", registry, {"numpy": "test"}, "a"*64, "cuda")
            b = inference.write_output(root / "b", parts, 3, "balanced", registry, {"numpy": "test"}, "b"*64, "cuda")
            self.assertEqual(a["model_id"], b["model_id"])
            self.assertTrue(re.fullmatch(r"[A-Za-z0-9._-]{1,31}", a["model_id"]))
            self.assertFalse(a["alignment_verified"])
            self.assertFalse((root / "a/meta").exists())
            self.assertEqual({s["name"] for s in a["stems"]}, {"harmonics", "vocals"})
            self.assertEqual(set(a["identity"]["model_hashes"]), {"MelBandRoformer.ckpt", "f7e0c4bc-ba3fe64a.th"})
            self.assertEqual(a["identity"]["code"], {"msst": registry["code"]["msst"]["commit"]})
            self.assertIn("kj-melband-mit", json.loads((root / "a/result.json").read_text())["licenses"])
            for name in ("harmonics", "vocals"):
                with wave.open(str(root / f"a/{name}.wav"), "rb") as wav:
                    self.assertEqual((wav.getnframes(), wav.getnchannels(), wav.getframerate()), (3, 2, 44100))
            with self.assertRaises(FileExistsError):
                inference.write_output(root / "a", parts, 3, "balanced", registry, {}, "x", "cpu")
            (root / "empty").mkdir()
            inference.write_output(root / "empty", parts, 3, "fast", registry, {}, "x", "cpu")
            changed = copy.deepcopy(registry); changed["pipeline_version"] += "-new"
            c = inference.write_output(root / "c", parts, 3, "balanced", changed, {"numpy": "test"}, "a"*64, "cuda")
            self.assertNotEqual(a["model_id"], c["model_id"])
            d = inference.write_output(root / "d", parts, 3, "balanced", registry, {"numpy": "test"}, "a"*64, "cpu")
            self.assertNotEqual(a["model_id"], d["model_id"])
            drums = inference.write_output(root / "drums", {"drums": parts["vocals"], "harmonics": parts["harmonics"]}, 3, "fast", registry, {}, "x", "cpu")
            self.assertEqual([s["name"] for s in drums["stems"]], ["drums", "harmonics"])
            with self.assertRaisesRegex(ValueError, "bounded"):
                inference.write_output(root / "bad", {"bass": parts["vocals"]}, 3, "fast", registry, {}, "x", "cpu")


class ProgressTests(unittest.TestCase):
    def test_overall_weights_passes_by_cost(self):
        registry = inference.manifest()
        events = []
        progress = inference.Progress(events.append, registry["presets"]["balanced"]["passes"], registry)
        progress.stage("load-model", 100, "loaded")
        progress.stage("separate-vocals", 50, "half", cost=3)
        progress.done_cost += 3
        progress.stage("separate-drums", 0, "start", cost=1)
        progress.stage("separate-drums", 100, "end", cost=1)
        progress.stage("postprocess", 100, "done")
        self.assertEqual([e["overall"] for e in events], [5.0, round(5 + 85 * 1.5 / 4, 1), round(5 + 85 * 3 / 4, 1), 90.0, 100.0])
        self.assertEqual([e["pct"] for e in events], [100, 50, 0, 100, 100])
        self.assertTrue(all(e["event"] == "progress" for e in events))
        self.assertNotIn("cost", events[1])

    def test_gpu_batch_fits_declared_memory(self):
        record = {"max_batch": 8, "gpu_reserve_bytes": 2 * 1024**3, "gpu_bytes_per_item": 1024**3}
        self.assertEqual(inference.gpu_batch(record, 0), 1)
        self.assertEqual(inference.gpu_batch(record, 8 * 1024**3), 6)
        self.assertEqual(inference.gpu_batch(record, 1024**3), 1)
        self.assertEqual(inference.gpu_batch(record, 64 * 1024**3), 8)


if __name__ == "__main__":
    unittest.main()
