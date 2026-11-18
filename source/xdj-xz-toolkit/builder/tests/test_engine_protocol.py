"""Offline tests: stdlib + NumPy only, no torch/model/network/device execution."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

ADAPTER = Path(__file__).resolve().parents[1] / "inference.py"
spec = importlib.util.spec_from_file_location("xz_builder_inference_protocol", ADAPTER)
inference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inference)

REGISTRY = inference.manifest()


def session(preset_id, preset=None):
    value = inference.Session.__new__(inference.Session)
    value.registry, value.preset_id = REGISTRY, preset_id
    value.preset = preset or REGISTRY["presets"][preset_id]
    return value


class PassSelectionTests(unittest.TestCase):
    def models(self, preset_id, parts, preset=None):
        return [item["model"] for item in session(preset_id, preset).passes_for(parts)]

    def test_balanced_vocals_and_harmonics_run_both_passes(self):
        self.assertEqual(self.models("balanced", ["vocals", "harmonics"]), ["melband-roformer-kj-vocals", "htdemucs-ft-drums"])

    def test_balanced_drums_runs_only_the_drums_pass(self):
        self.assertEqual(self.models("balanced", ["drums"]), ["htdemucs-ft-drums"])

    def test_fast_drums_runs_the_joint_pass(self):
        self.assertEqual(self.models("fast", ["drums"]), ["htdemucs"])

    def test_uncovered_part_is_refused(self):
        vocals_only = {"passes": [{"model": "melband-roformer-kj-vocals", "sources": ["vocals"]}]}
        with self.assertRaisesRegex(ValueError, "cannot produce \\['drums'\\]"):
            self.models("vocals-only", ["drums"], vocals_only)
        with self.assertRaisesRegex(ValueError, "cannot produce \\['drums'\\]"):
            self.models("vocals-only", ["vocals", "harmonics"], vocals_only)


def stub_torch():
    def refuse():
        raise AssertionError("empty_cache called without CUDA")
    return types.SimpleNamespace(cuda=types.SimpleNamespace(is_available=lambda: False, empty_cache=refuse))


class AcceleratedTests(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.session = session("balanced")
        self.session.loaded = {"htdemucs-ft-drums": object()}
        self.session.diagnostics = {}
        self.session.emit = self.events.append
        torch = mock.patch.dict(sys.modules, {"torch": stub_torch()})
        torch.start()
        self.addCleanup(torch.stop)

    def failing_once(self, error):
        calls = []

        def action():
            calls.append(self.session.device)
            if len(calls) == 1:
                raise error
            return "estimates"
        return action, calls

    def test_gpu_failure_falls_back_to_cpu(self):
        self.session.device = "cuda"
        action, calls = self.failing_once(RuntimeError("CUDA error: out of memory"))
        self.assertEqual(self.session.accelerated(action), "estimates")
        self.assertEqual(calls, ["cuda", "cpu"])
        self.assertEqual(self.session.device, "cpu")
        self.assertEqual(self.session.loaded, {})
        self.assertEqual(self.session.diagnostics, {"device_error": "CUDA error: out of memory",
                                                    "failed_device": "cuda", "fallback_device": "cpu"})
        device_events = [event for event in self.events if event.get("stage") == "device"]
        self.assertEqual(len(self.events), 1)
        self.assertEqual(len(device_events), 1)
        self.assertIs(device_events[0]["fallback"], True)
        self.assertEqual(device_events[0]["event"], "progress")

    def test_cpu_failure_propagates(self):
        self.session.device = "cpu"
        error = RuntimeError("cpu kernel failed")
        action, calls = self.failing_once(error)
        with self.assertRaises(RuntimeError) as raised:
            self.session.accelerated(action)
        self.assertIs(raised.exception, error)
        self.assertEqual(calls, ["cpu"])
        self.assertEqual(self.events, [])
        self.assertEqual(self.session.diagnostics, {})


class FakeSession:
    built = []

    def __init__(self, registry, preset_id, model_dir, code_dir, device, vram_bytes, emit):
        self.args = (registry, preset_id, model_dir, code_dir, device, vram_bytes, emit)
        self.device, self.diagnostics, self.requests = device, {}, []
        FakeSession.built.append(self)

    def separate(self, request, work):
        self.requests.append(request)
        if request.get("boom"):
            raise ValueError("boom")
        output = Path(request["output_dir"])
        output.mkdir()
        result = {"model_id": "xz-balanced-deadbeef", "timing": {"frames": 44100, "separate_seconds": 1.0},
                  "diagnostics": {}}
        (output / "result.json").write_text(json.dumps(result), encoding="utf-8")
        return result


class ServeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        built = mock.patch.object(FakeSession, "built", [])
        built.start()
        self.addCleanup(built.stop)
        fake = mock.patch.object(inference, "Session", FakeSession)
        fake.start()
        self.addCleanup(fake.stop)

    def argv(self, preset="balanced"):
        return ["--serve", "--preset", preset, "--model-dir", str(self.root / "models"),
                "--code-dir", str(self.root / "code"), "--device", "cuda", "--vram-bytes", "123"]

    def request(self, name, **extra):
        return json.dumps({"op": "separate", "input": str(self.root / "source.wav"),
                           "output_dir": str(self.root / name), "parts": ["vocals", "harmonics"], **extra}) + "\n"

    def test_serve_loop_answers_each_request_until_quit(self):
        stdin = io.StringIO("\n" + self.request("first") + self.request("second", boom=True)
                            + '{"op": "quit"}\n' + self.request("after-quit"))
        events = []
        with mock.patch("sys.stdin", stdin):
            self.assertEqual(inference.main(self.argv(), emit=events.append), 0)
        self.assertEqual([event["event"] for event in events], ["ready", "done", "failed"])
        self.assertEqual(events[0]["device"], "cuda")
        self.assertEqual(events[1]["result"], str(self.root / "first" / "result.json"))
        self.assertEqual((events[1]["model_id"], events[1]["device"]), ("xz-balanced-deadbeef", "cuda"))
        self.assertEqual(events[1]["timing"], {"frames": 44100, "separate_seconds": 1.0})
        self.assertEqual(events[2]["error"], "ValueError: boom")
        self.assertEqual(events[2]["diagnostics"], {})
        self.assertEqual([request["output_dir"] for request in FakeSession.built[0].requests],
                         [str(self.root / "first"), str(self.root / "second")])
        self.assertFalse((self.root / "after-quit").exists())
        self.assertEqual(len(FakeSession.built), 1)
        registry, preset_id, model_dir, code_dir, device, vram_bytes, emit = FakeSession.built[0].args
        self.assertEqual((preset_id, model_dir, code_dir, device, vram_bytes),
                         ("balanced", self.root / "models", self.root / "code", "cuda", 123))
        self.assertEqual(registry, REGISTRY)
        self.assertEqual(emit, events.append)

    def test_unknown_preset_fails_before_session(self):
        with mock.patch("sys.stdin", io.StringIO(self.request("first"))):
            with self.assertRaisesRegex(ValueError, "preset"):
                inference.main(self.argv("nope"), emit=[].append)
        self.assertEqual(FakeSession.built, [])


if __name__ == "__main__":
    unittest.main()
