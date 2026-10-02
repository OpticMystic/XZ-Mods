"""Offline role rendering tests: NumPy + SciPy, as in the managed engine environment."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import wave

import numpy as np

try:
    from builder import overcue_roles as roles
except ImportError as error:  # SciPy is only guaranteed inside the engine environment.
    raise unittest.SkipTest(f"overcue_roles needs NumPy and SciPy: {error}")
from builder import overcue_writer


def write_wav(path, samples):
    with wave.open(str(path), "wb") as target:
        target.setnchannels(2)
        target.setsampwidth(2)
        target.setframerate(44100)
        target.writeframes(np.asarray(samples, dtype="<i2").tobytes())
    return path


def tone(frequency, level, frames=4410):
    t = np.arange(frames) / 44100
    return np.stack([np.sin(2 * np.pi * frequency * t), np.cos(2 * np.pi * frequency * t)], axis=1) * level


class RoleTests(unittest.TestCase):
    def render(self, root, vocals, drums, harmonics, gains=(1.0, 1.0)):
        mix = vocals + drums + harmonics
        paths = [write_wav(root / "mix.wav", np.rint(mix * 32768)),
                 write_wav(root / "v.wav", np.rint(vocals * gains[0] * 32767)),
                 write_wav(root / "h.wav", np.rint(harmonics * gains[1] * 32767))]
        result = roles.render(paths[0], paths[1], gains[0], paths[2], gains[1], root / "out")
        pcm = {name: np.frombuffer((root / "out" / info["file"]).read_bytes(), dtype="<i2").reshape(-1, 2)
               for name, info in result["roles"].items()}
        return result, pcm

    def test_roles_follow_spec_order_and_recipe(self):
        self.assertEqual(roles.ROLES, overcue_writer.ROLES)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vocals, drums, harmonics = tone(440, 0.2), tone(110, 0.15), tone(660, 0.1)
            result, pcm = self.render(root, vocals, drums, harmonics, gains=(2.0, 0.5))
            self.assertEqual(result["frames"], -(-4410 * 320 // 147))
            self.assertEqual((result["sample_rate"], result["headroom_gain"]), (96000, 1.0))
            self.assertEqual(json.loads((root / "out/result.json").read_text()), result)
            self.assertEqual(sorted(p.name for p in (root / "out").iterdir()), sorted([f"{r}.s16le" for r in roles.ROLES] + ["result.json"]))
            for name, info in result["roles"].items():
                self.assertEqual(hashlib.sha256((root / "out" / info["file"]).read_bytes()).hexdigest(), info["sha256"])
                self.assertEqual(pcm[name].shape, (result["frames"], 2))
            core = slice(2000, -2000)  # away from resampler edge transients
            def level(name):
                return pcm[name][core].astype(np.float64) / 32767
            parts = {"vocal": 0.2, "drums": 0.15, "harmonics": 0.1}
            for name, peak in parts.items():
                self.assertAlmostEqual(float(np.max(np.abs(level(name)))), peak, delta=0.004)
            np.testing.assert_allclose(level("instrumental"), level("drums") + level("harmonics"), atol=3e-4)
            np.testing.assert_allclose(level("vocals-drums"), level("vocal") + level("drums"), atol=3e-4)
            np.testing.assert_allclose(level("vocals-harmonics"), level("vocal") + level("harmonics"), atol=3e-4)
            np.testing.assert_allclose(level("full-mix"), level("vocal") + level("instrumental"), atol=3e-4)

    def test_common_headroom_gain_prevents_clipping(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vocals, drums, harmonics = tone(440, 0.6), tone(440, -0.3), tone(440, 0.6)
            result, pcm = self.render(root, vocals, drums, harmonics, gains=(0.5, 0.5))
            gain = result["headroom_gain"]
            self.assertTrue(0.7 < gain < 0.9)
            self.assertEqual(np.float32(gain), gain)
            loudest = max(int(np.abs(p.astype(np.int32)).max()) for p in pcm.values())
            self.assertLessEqual(loudest, 32767)
            self.assertGreater(loudest, 32000)

    def test_rejects_mismatched_lengths_and_existing_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_wav(root / "a.wav", np.zeros((10, 2)))
            write_wav(root / "b.wav", np.zeros((11, 2)))
            with self.assertRaisesRegex(ValueError, "same frame count"):
                roles.render(root / "a.wav", root / "b.wav", 1.0, root / "a.wav", 1.0, root / "out")
            self.assertFalse((root / "out").exists())
            (root / "out").mkdir()
            with self.assertRaises(FileExistsError):
                roles.render(root / "a.wav", root / "a.wav", 1.0, root / "a.wav", 1.0, root / "out")
            self.assertEqual(roles.MAX_SOURCE_FRAMES, overcue_writer.MAX_FRAMES * 147 // 320)
            self.assertLessEqual(-(-roles.MAX_SOURCE_FRAMES * 320 // 147), overcue_writer.MAX_FRAMES)
            self.assertGreater(-(-(roles.MAX_SOURCE_FRAMES + 1) * 320 // 147), overcue_writer.MAX_FRAMES)


if __name__ == "__main__":
    unittest.main()
