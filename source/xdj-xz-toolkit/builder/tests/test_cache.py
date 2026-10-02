import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import wave

MODULE = Path(__file__).resolve().parents[1] / "cache.py"
spec = importlib.util.spec_from_file_location("xz_builder_cache", MODULE)
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


def fixture(path, frames=20000, rate=44100, channels=2, width=2):
    with wave.open(str(path), "wb") as out:
        out.setparams((channels, width, rate, frames, "NONE", "NONE"))
        out.writeframes(bytes((i % 251 for i in range(frames * channels * width))))
    return path


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = fixture(self.root / "source.wav")

    def test_hash_matches_independent_integer_oracle_for_both_windows(self):
        for frames in (100, 20000):
            fixture(self.source, frames)
            data = self.source.read_bytes()
            payload = struct.pack("<QQ", len(data), frames) + data[:65536]
            if len(data) > 65536:
                payload += data[-65536:]
            value = 1469598103934665603
            for byte in payload:
                value = ((value ^ byte) * 1099511628211) % (1 << 64)
            self.assertEqual(cache.track_key(self.source), f"{value:016x}")

    def test_incompatible_and_truncated_audio(self):
        self.assertEqual(cache.inspect_audio(self.source)["frames"], 20000)
        for changes in ({"rate": 48000}, {"channels": 1}, {"width": 3}, {"frames": 0}):
            fixture(self.source, **changes)
            with self.assertRaises(ValueError):
                cache.inspect_audio(self.source)
        fixture(self.source)
        self.source.write_bytes(self.source.read_bytes()[:-4])
        # The Python reader rejects truncation; the native helper reports the frames present.
        try:
            frames = cache.inspect_audio(self.source)["frames"]
        except ValueError:
            frames = None
        self.assertNotEqual(frames, 20000)

    def test_gains(self):
        self.assertEqual(cache._gain(0.25), 0.25)
        for gain in (0, -1, float("nan"), float("inf"), 1e-50, 1e-38, 1e40):
            with self.assertRaises(ValueError):
                cache._gain(gain)

    def test_unsafe_names_and_links(self):
        for name in ("..", "../evil", "x/y", "x\\y", "x:", "NUL", "COM1.wav", "foo."):
            with self.assertRaises(ValueError):
                cache.validate_model_id(name)
        with self.assertRaises(ValueError):
            cache.inspect_audio(self.root / "sub" / ".." / "source.wav")
        link = self.root / "link"
        try:
            link.symlink_to(self.root / "elsewhere", target_is_directory=True)
        except OSError:
            self.skipTest("Creating symlinks is unavailable")
        with self.assertRaises(ValueError):
            cache._safe_path(link / "file.wav")

    def test_atomic_publication_refuses_even_empty_existing_directory(self):
        source, target = self.root / "new", self.root / "existing"
        source.mkdir()
        target.mkdir()
        with self.assertRaises(OSError):
            cache._publish_new(source, target)
        self.assertTrue(source.is_dir())
        self.assertTrue(target.is_dir())


if __name__ == "__main__":
    unittest.main()
