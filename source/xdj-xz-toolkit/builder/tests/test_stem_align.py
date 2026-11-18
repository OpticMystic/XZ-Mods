import unittest

try:
    import numpy as np
    from scipy.signal import lfilter
    from builder import stem_align as align
except ImportError as exc:
    np = None
    MISSING = f'numpy and scipy are needed for alignment tests ({exc})'
else:
    MISSING = ''

RATE = 44100


def song(seed, seconds=12.0):
    """Drums, bass and pad stems at 44.1 kHz with a lead-in and fade-out; their sum is the mix."""
    rng = np.random.default_rng(seed)
    frames = int(seconds * RATE)
    t = np.arange(frames) / RATE
    tempo = 0.45 + 0.1 * rng.random()
    hits = np.zeros(frames)
    hits[(np.arange(0, seconds, tempo) * RATE).astype(int)] = 1
    drums = lfilter([1], [1, -0.9995], hits) * lfilter([0.3], [1, -0.7], rng.standard_normal(frames))
    notes = 40 + 25 * rng.random(int(seconds / (4 * tempo)) + 1)
    bass = 0.3 * np.sin(2 * np.pi * np.cumsum(notes[(t // (4 * tempo)).astype(int)]) / RATE)
    chord = 200 + 300 * rng.random(3)
    vocals = 0.15 * sum(np.sin(2 * np.pi * f * t * (1 + 0.003 * np.sin(2 * np.pi * 5 * t))) for f in chord)
    fade = np.clip((seconds - t) / 0.5, 0, 1) * (t >= 0.1)
    stereo = lambda x, pan: np.stack([x * (1 - pan), x * (1 + pan)], axis=1).astype(np.float32) * fade[:, None].astype(np.float32)
    parts = {'drums': stereo(drums, 0.2), 'bass': stereo(bass, 0.0), 'vocals': stereo(vocals, -0.1)}
    return sum(parts.values()), parts


def delayed(audio, samples):
    if samples >= 0:
        return np.concatenate([np.zeros((samples, 2), np.float32), audio])
    return audio[-samples:]


@unittest.skipIf(np is None, MISSING)
class AlignTests(unittest.TestCase):
    def setUp(self):
        self.mix, self.parts = song(1)

    def test_exact_stems(self):
        result = align.align(self.mix, sum(self.parts.values()))
        self.assertEqual(result['offset_samples'], 0)
        self.assertAlmostEqual(result['gain'], 1.0, places=4)
        self.assertEqual(result['verdict'], 'aligned')
        self.assertIsNone(result['reason'])

    def test_mp3_encoder_delays_and_early_stems(self):
        for shift in (576 + 529, 2257, 1, -300):
            result = align.align(self.mix, delayed(sum(self.parts.values()), shift))
            self.assertEqual(result['offset_samples'], shift)
            self.assertAlmostEqual(result['offset_ms'], 1000 * shift / RATE, places=2)
            self.assertLess(result['residual_db'], -60)
            self.assertEqual(result['verdict'], 'aligned')
            self.assertEqual(result['plan']['trim_start'], max(0, shift))
            self.assertEqual(result['plan']['pad_start'], max(0, -shift))

    def test_gain_is_fitted(self):
        result = align.align(self.mix, sum(self.parts.values()) * np.float32(0.5))
        self.assertAlmostEqual(result['gain'], 2.0, places=3)
        self.assertAlmostEqual(result['gain_db'], 6.02, places=2)
        self.assertEqual(result['verdict'], 'aligned')

    def test_stems_a_little_shorter_or_longer(self):
        total = delayed(sum(self.parts.values()), 1105)
        short = align.align(self.mix, total[:-RATE // 4])
        self.assertEqual((short['offset_samples'], short['verdict']), (1105, 'aligned'))
        self.assertEqual(short['plan']['pad_end'], RATE // 4)
        long = align.align(self.mix, np.concatenate([total, np.full((RATE, 2), 0.01, np.float32)]))
        self.assertEqual((long['offset_samples'], long['verdict']), (1105, 'aligned'))
        self.assertEqual(long['plan']['trim_end'], RATE)

    def test_two_stem_sum(self):
        instrumental = self.parts['drums'] + self.parts['bass']
        result = align.align(self.mix, delayed(self.parts['vocals'] + instrumental, 1105))
        self.assertEqual((result['offset_samples'], result['verdict']), (1105, 'aligned'))

    def test_missing_part_is_weak_not_aligned(self):
        result = align.align(self.mix, delayed(self.parts['drums'] + self.parts['bass'], 700))
        self.assertEqual(result['offset_samples'], 700)
        self.assertEqual(result['verdict'], 'weak')
        self.assertIn('do not add up', result['reason'])

    def test_wrong_track_fails_clearly(self):
        _, other = song(7)
        result = align.align(self.mix, sum(other.values()))
        self.assertEqual(result['verdict'], 'mismatch')
        self.assertLess(result['correlation'], align.WEAK_CORRELATION)
        self.assertIn('do not match this track', result['reason'])

    def test_thresholds_follow_the_least_squares_residual(self):
        noisy = sum(self.parts.values()) + np.random.default_rng(3).standard_normal(self.mix.shape).astype(np.float32) * 0.02
        result = align.align(self.mix, noisy)
        self.assertAlmostEqual(result['residual_db'], 10 * np.log10(1 - result['correlation'] ** 2), places=6)
        self.assertEqual(align.verdict(0.995, -20.0), 'aligned')
        self.assertEqual(align.verdict(0.99, -17.0), 'weak')
        self.assertEqual(align.verdict(0.1, -0.04), 'mismatch')

    def test_silence_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'stems are silent'):
            align.align(self.mix, np.zeros_like(self.mix))

    def test_short_tracks_use_one_window(self):
        mix = self.mix[:RATE * 2]
        result = align.align(mix, delayed(mix, 400))
        self.assertEqual((result['offset_samples'], result['verdict']), (400, 'aligned'))


if __name__ == '__main__':
    unittest.main()
