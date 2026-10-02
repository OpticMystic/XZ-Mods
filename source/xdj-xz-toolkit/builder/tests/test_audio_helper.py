import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from test_cache import cache, fixture


@unittest.skipUnless(os.environ.get("XZ_AUDIO_HELPER") and shutil.which("ffmpeg"),
                     "Set XZ_AUDIO_HELPER and provide FFmpeg for FLAC fixture encoding")
class NativeAudioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.wav = fixture(self.root / "音楽.wav")
        self.flac = self.root / "音楽.flac"
        subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-i", str(self.wav),
                        "-c:a", "flac", str(self.flac)], check=True, capture_output=True)
        self.helper = os.environ["XZ_AUDIO_HELPER"]

    def test_flac_inspect_preserve_and_convert(self):
        info = cache.inspect_audio(self.flac)
        self.assertEqual((info["frames"], info["sample_rate"], info["channels"]), (20000, 44100, 2))
        original = self.flac.read_bytes()
        self.assertEqual(cache.track_key(self.flac), cache.track_key(self.flac, 20000))
        converted = self.root / "新しい.wav"
        result = subprocess.run([self.helper, "convert", str(self.flac), str(converted)],
                                capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(result.stdout)["frames"], 20000)
        self.assertEqual(converted.read_bytes(), self.wav.read_bytes())
        result = subprocess.run([self.helper, "convert", str(self.flac), str(converted)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(converted.read_bytes(), self.wav.read_bytes())
        self.assertEqual(self.flac.read_bytes(), original)

    def test_truncated_and_wrong_extension_refused(self):
        self.flac.write_bytes(self.flac.read_bytes()[:-100])
        with self.assertRaises(ValueError):
            cache.inspect_audio(self.flac)
        output = self.root / "failed.wav"
        result = subprocess.run([self.helper, "convert", str(self.flac), str(output)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        disguised = self.root / "disguised.flac"
        disguised.write_bytes(self.wav.read_bytes())
        with self.assertRaises(ValueError):
            cache.inspect_audio(disguised)

    def test_flac_requires_helper_and_does_not_execute_media(self):
        with patch.dict(os.environ, {"XZ_AUDIO_HELPER": ""}):
            with self.assertRaisesRegex(ValueError, "requires"):
                cache.inspect_audio(self.flac)

    def test_streaming_source_longer_than_old_resident_pcm_limit(self):
        source=self.root/'long.flac'
        subprocess.run([shutil.which('ffmpeg'),'-v','error','-f','lavfi','-i','anullsrc=r=44100:cl=stereo',
                        '-t','400','-c:a','flac','-sample_fmt','s16',str(source)],check=True,capture_output=True)
        info=cache.inspect_audio(source)
        self.assertEqual(info['frames'],44100*400)
        self.assertGreater(info['frames'],(128*1024*1024)//8)


if __name__ == "__main__":
    unittest.main()
