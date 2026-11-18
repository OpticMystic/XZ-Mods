import json
import os
from pathlib import Path
import statistics
import tempfile
import unittest
from unittest import mock

from builder import engines

PRESET = engines.registry()['presets']['balanced']
FRAMES = 44100 * 180


def timing(rate, load=None, frames=FRAMES):
    seconds = rate * frames / 44100
    entry = {'frames': frames, 'separate_seconds': seconds * 0.75, 'postprocess_seconds': seconds * 0.25}
    if load is not None:
        entry['load_seconds'] = load
    return entry


class DataRootTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        environment = mock.patch.dict(os.environ, {'XZ_BUILDER_DATA': str(Path(temp.name) / 'data')})
        environment.start()
        self.addCleanup(environment.stop)


class TimingStoreTests(DataRootTest):
    def prior(self, device, include_load=True):
        load = PRESET['prior_load_seconds'][device] if include_load else 0
        return PRESET['prior_seconds_per_source_second'][device] * FRAMES / 44100 + load

    def test_prior_before_any_run(self):
        for device in ('cuda', 'mps', 'cpu'):
            with self.subTest(device=device):
                self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device=device), self.prior(device))

    def test_median_of_recorded_runs(self):
        for rate, load in ((0.10, 10), (0.30, 20), (0.20, 40)):
            engines.record_timing('balanced', 'cuda', timing(rate, load))
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cuda'),
                               0.20 * FRAMES / 44100 + 20)
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cpu'), self.prior('cpu'))

    def test_publish_timings_are_kept_per_stem_sample_rate(self):
        self.assertAlmostEqual(engines.estimate_publish_seconds(FRAMES), engines.PUBLISH_PRIOR_RATE * 180)
        engines.record_publish_timing(FRAMES, 0.4 * 180, 96000)
        # No 44.1 kHz run yet: the slower 96 kHz runs stand in, so the first estimate errs high.
        self.assertAlmostEqual(engines.estimate_publish_seconds(FRAMES, 44100), 0.4 * 180)
        engines.record_publish_timing(FRAMES, 0.1 * 180, 44100)
        self.assertAlmostEqual(engines.estimate_publish_seconds(FRAMES, 44100), 0.1 * 180)
        self.assertAlmostEqual(engines.estimate_publish_seconds(FRAMES, 96000), 0.4 * 180)
        self.assertEqual(sorted(key for key in engines.read_timings() if key.startswith('publish/')), ['publish/44100', 'publish/any'])
        engines.record_publish_timing(FRAMES, 1.0, 48000)
        self.assertNotIn('publish/48000', engines.read_timings())

    def test_runs_without_load_keep_load_prior(self):
        engines.record_timing('balanced', 'cuda', timing(0.5))
        self.assertEqual(engines.read_timings()['balanced/cuda']['loads'], [])
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cuda'),
                               0.5 * FRAMES / 44100 + PRESET['prior_load_seconds']['cuda'])

    def test_keeps_only_the_last_runs(self):
        rates = [0.01 * (index + 1) for index in range(engines.TIMING_RUNS + 2)]
        for index, rate in enumerate(rates):
            engines.record_timing('balanced', 'cuda', timing(rate, load=index + 1))
        entry = engines.read_timings()['balanced/cuda']
        kept = rates[-engines.TIMING_RUNS:]
        self.assertEqual(len(entry['rates']), engines.TIMING_RUNS)
        for stored, rate in zip(entry['rates'], kept):
            self.assertAlmostEqual(stored, rate)
        self.assertEqual(entry['loads'], list(range(3, engines.TIMING_RUNS + 3)))
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cuda'),
                               statistics.median(kept) * FRAMES / 44100 + statistics.median(range(3, engines.TIMING_RUNS + 3)))

    def test_zero_frames_is_ignored(self):
        engines.record_timing('balanced', 'cuda', timing(0.5, load=3, frames=0))
        self.assertEqual(engines.read_timings(), {})
        self.assertFalse(engines.timing_store().exists())

    def test_corrupt_store_is_empty(self):
        engines.timing_store().write_text('{"balanced/cuda": {"rates": [', encoding='utf-8')
        self.assertEqual(engines.read_timings(), {})
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cuda'), self.prior('cuda'))
        engines.record_timing('balanced', 'cuda', timing(0.4, load=5))
        self.assertEqual(json.loads(engines.timing_store().read_text(encoding='utf-8'))['balanced/cuda']['loads'], [5])

    def test_without_load(self):
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cuda', include_load=False),
                               self.prior('cuda', include_load=False))
        engines.record_timing('balanced', 'cuda', timing(0.25, load=30))
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES, device='cuda', include_load=False),
                               0.25 * FRAMES / 44100)

    def test_unknown_preset_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'preset'):
            engines.estimate_separation_seconds('nope', FRAMES, device='cpu')


class PlannedDeviceTests(DataRootTest):
    def marker(self, preset, device, mtime):
        path = engines.data_root() / f'setup-{preset}.json'
        path.write_text(json.dumps({'preset': preset, 'device': device}), encoding='utf-8')
        os.utime(path, (mtime, mtime))

    def test_cpu_without_setup(self):
        self.assertEqual(engines.planned_device(), 'cpu')
        self.assertEqual(engines.planned_device('balanced'), 'cpu')

    def test_newest_marker_wins(self):
        self.marker('fast', 'cpu', 1_000_000)
        self.marker('balanced', 'cuda', 2_000_000)
        self.assertEqual(engines.planned_device(), 'cuda')

    def test_preset_marker_beats_newer_marker(self):
        self.marker('fast', 'cpu', 1_000_000)
        self.marker('balanced', 'cuda', 2_000_000)
        self.assertEqual(engines.planned_device('fast'), 'cpu')

    def test_estimate_uses_planned_device(self):
        self.marker('balanced', 'cuda', 2_000_000)
        self.assertAlmostEqual(engines.estimate_separation_seconds('balanced', FRAMES),
                               PRESET['prior_seconds_per_source_second']['cuda'] * FRAMES / 44100
                               + PRESET['prior_load_seconds']['cuda'])


if __name__ == '__main__':
    unittest.main()
