import math
from pathlib import Path
import tempfile
import unittest

try:
    import numpy as np
    import soundfile as sf
    from builder import resample_source
except ImportError:
    np = sf = resample_source = None


@unittest.skipUnless(sf is not None, 'needs numpy, scipy and soundfile (the managed import environment)')
class ResampleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def tone(self, path, rate, frames, hz=1000.0):
        t = np.arange(frames) / rate
        audio = np.stack([0.5 * np.sin(2 * math.pi * hz * t), 0.25 * np.sin(2 * math.pi * hz * t)], axis=1)
        sf.write(path, audio, rate, subtype='PCM_16')
        return path

    def peak_hz(self, path):
        audio, rate = sf.read(path, dtype='float32', always_2d=True)
        spectrum = np.abs(np.fft.rfft(audio[:, 0] * np.hanning(len(audio))))
        return np.fft.rfftfreq(len(audio), 1 / rate)[int(np.argmax(spectrum))], rate, len(audio)

    def test_48k_to_44k_keeps_pitch_and_predicted_length(self):
        source = self.tone(self.root / 'in.wav', 48000, 48000 * 3 + 17)
        target = self.root / 'out.wav'
        frames = resample_source.resample(source, target, resample_source.output_frames(48000 * 3 + 17, 48000))
        hz, rate, count = self.peak_hz(target)
        self.assertEqual((rate, count), (44100, frames))
        self.assertEqual(count, -(-(48000 * 3 + 17) * 44100 // 48000))
        self.assertAlmostEqual(hz, 1000.0, delta=2.0)
        info = sf.info(target)
        self.assertEqual((info.channels, info.subtype), (2, 'PCM_16'))

    def test_96k_long_block_boundaries_do_not_click(self):
        source = self.tone(self.root / 'in.wav', 96000, 96000 * 4, hz=440.0)
        target = self.root / 'out.wav'
        resample_source.resample(source, target)
        audio = sf.read(target, dtype='float32', always_2d=True)[0][:, 0]
        steps = np.abs(np.diff(audio))
        self.assertLess(float(steps.max()), 0.5 * 2 * math.pi * 440 / 44100 * 1.5)

    def test_wrong_expected_length_and_native_rate_are_refused(self):
        source = self.tone(self.root / 'in.wav', 48000, 48000)
        with self.assertRaises(ValueError):
            resample_source.resample(source, self.root / 'bad.wav', 12345)
        self.assertFalse((self.root / 'bad.wav').exists())
        native = self.tone(self.root / 'native.wav', 44100, 44100)
        with self.assertRaises(ValueError):
            resample_source.resample(native, self.root / 'same.wav')


if __name__ == '__main__':
    unittest.main()
