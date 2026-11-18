import json
from pathlib import Path
import tempfile
import unittest

from builder import stem_estimate as est


class EstimateTests(unittest.TestCase):
    def test_three_minute_track_lands_in_a_plausible_band(self):
        frames = est.frames_from_seconds(180)
        for rate, low, high in ((44100, 0.4, 0.8), (96000, 0.25, 0.5)):
            result = est.estimate_track(frames, rate=rate)
            raw_per_role = est.role_frames(frames, rate) * 4
            self.assertEqual((result['sample_rate'], result['role_frames']), (rate, est.role_frames(frames, rate)))
            self.assertEqual(result['raw_bytes'], raw_per_role * 7)
            self.assertTrue(result['fits_format'])
            self.assertEqual(result['pages'], -(-raw_per_role // est.PAGE_BYTES))
            self.assertLess(result['bytes'], result['raw_bytes'] * high)
            self.assertGreater(result['bytes'], result['raw_bytes'] * low)
            self.assertLess(result['low_bytes'], result['bytes'])
            self.assertGreater(result['high_bytes'], result['bytes'])
            self.assertFalse(result['learned'])
            for role in est.ROLES:
                self.assertEqual(result['roles'][role] % est.FILESYSTEM_CLUSTER, 0)

    def test_44k1_is_the_default_and_keeps_every_source_frame(self):
        frames = est.frames_from_seconds(180)
        self.assertEqual(est.DEFAULT_RATE, 44100)
        self.assertEqual(est.role_frames(frames), frames)
        self.assertEqual(est.role_frames(frames, 96000), -(-frames * 320 // 147))
        default, slow = est.estimate_track(frames), est.estimate_track(frames, rate=96000)
        self.assertEqual(default['sample_rate'], 44100)
        self.assertEqual((default['raw_bytes'], slow['raw_bytes']), (7 * 4 * frames, 7 * 4 * -(-frames * 320 // 147)))
        # Roughly 71 percent of the 96 kHz bundle from the priors, about 70 percent measured.
        self.assertTrue(0.6 < default['bytes'] / slow['bytes'] < 0.8, default['bytes'] / slow['bytes'])
        for role in est.ROLES:
            raw = frames * 4
            pages = -(-raw // est.PAGE_BYTES)
            expected = est.PAGE_HEADER + est.PAGE_TABLE_ENTRY * pages + int(raw * est.PRIORS[44100][role])
            self.assertEqual(default['roles'][role], -(-expected // est.FILESYSTEM_CLUSTER) * est.FILESYSTEM_CLUSTER)
        self.assertEqual(est.waveform_bytes(frames), est.waveform_bytes(est.role_frames(frames, 96000), 96000))
        with self.assertRaises(ValueError):
            est.estimate_track(frames, est.SizeModel(rate=96000), rate=44100)
        with self.assertRaises(ValueError):
            est.role_frames(frames, 48000)

    def test_models_learn_each_rate_separately(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for rate, ratio in ((96000, 0.37), (44100, 0.6)):
                folder = root / str(rate)
                folder.mkdir()
                frames = est.role_frames(est.frames_from_seconds(30), rate)
                raw = frames * 4
                pages = -(-raw // est.PAGE_BYTES)
                for role in est.ROLES:
                    (folder / f'stems-sidecar-{role}.s16le.pgz').write_bytes(
                        b'\0' * (est.PAGE_HEADER + est.PAGE_TABLE_ENTRY * pages + int(raw * ratio)))
                self.assertTrue(est.observe_bundle(folder, frames, root, rate))
            self.assertAlmostEqual(est.SizeModel.load(est.model_path(root), 96000).ratio('vocal'), 0.37, places=2)
            self.assertAlmostEqual(est.SizeModel.load(est.model_path(root)).ratio('vocal'), 0.6, places=2)
            data = json.loads(est.model_path(root).read_text())
            self.assertEqual(sorted(data), ['samples', 'samples_44100', 'version'])

    def test_sparse_roles_estimate_smaller_than_the_full_mix(self):
        result = est.estimate_track(est.frames_from_seconds(240))
        self.assertLess(result['roles']['vocal'], result['roles']['full-mix'])
        self.assertLess(result['roles']['drums'], result['roles']['instrumental'])

    def test_format_limit_is_about_23_minutes(self):
        self.assertTrue(est.estimate_track(est.frames_from_seconds(23 * 60))['fits_format'])
        self.assertFalse(est.estimate_track(est.frames_from_seconds(24 * 60))['fits_format'])

    def test_waveform_extra_bytes_are_added_per_second(self):
        frames = est.frames_from_seconds(60)
        plain = est.estimate_track(frames)
        with_extra = est.estimate_track(frames, extra_bytes_per_second=1000)
        self.assertGreaterEqual(with_extra['bytes'] - plain['bytes'], 60 * 1000)
        self.assertEqual(with_extra['extra_bytes'] % est.FILESYSTEM_CLUSTER, 0)
        self.assertEqual(plain['extra_bytes'], est.waveform_bytes(plain['role_frames']))

    def test_waveform_bytes_match_the_writer_before_cluster_slack(self):
        try:
            from builder import stem_waveforms
        except ImportError:
            self.skipTest('stem_waveforms needs numpy and scipy')
        for rate in est.RATES:
            for seconds in (1, 30, 60, 200, 23 * 60):
                frames = est.role_frames(est.frames_from_seconds(seconds), rate)
                exact = stem_waveforms.bytes_for_frames(frames, rate)
                rounded = est.waveform_bytes(frames, rate)
                self.assertGreaterEqual(rounded, exact)
                self.assertLess(rounded - exact, 6 * est.FILESYSTEM_CLUSTER)
                columns = -(-frames // (rate // est.WAVEFORM_COLUMNS_PER_SECOND))
                self.assertEqual(exact, 3 * (2 * est.WAVEFORM_HEADER + 4 * columns))

    def test_observations_replace_priors_with_medians(self):
        model = est.SizeModel()
        frames_96k = est.role_frames(est.frames_from_seconds(120))
        raw = frames_96k * 4
        pages = -(-raw // est.PAGE_BYTES)
        overhead = est.PAGE_HEADER + est.PAGE_TABLE_ENTRY * pages
        for ratio in (0.30, 0.34, 0.50):
            model.observe({role: overhead + int(raw * ratio) for role in est.ROLES}, frames_96k)
        for role in est.ROLES:
            self.assertAlmostEqual(model.ratio(role), 0.34, places=3)
        self.assertEqual(model.observations(), 3)
        self.assertTrue(est.estimate_track(est.frames_from_seconds(60), model)['learned'])

    def test_implausible_observation_is_rejected(self):
        model = est.SizeModel()
        frames_96k = est.role_frames(est.frames_from_seconds(10))
        with self.assertRaises(ValueError):
            model.observe({role: 10 for role in est.ROLES}, frames_96k)
        with self.assertRaises(ValueError):
            model.observe({role: frames_96k * 4 * 3 for role in est.ROLES}, frames_96k)

    def test_model_round_trips_and_ignores_junk(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'model.json'
            model = est.SizeModel({'vocal': [0.2, 0.25]})
            model.save(path)
            loaded = est.SizeModel.load(path)
            self.assertEqual(loaded.samples['vocal'], [0.2, 0.25])
            self.assertEqual(loaded.ratio('drums'), est.PRIORS[44100]['drums'])
            self.assertEqual(est.SizeModel.load(path, 96000).ratio('drums'), est.PRIORS[96000]['drums'])
            path.write_text('{"samples": {"vocal": ["x", -1, 9, 0.3]}}')
            self.assertEqual(est.SizeModel.load(path, 96000).samples['vocal'], [0.3])
            self.assertEqual(est.SizeModel.load(path).samples['vocal'], [])
            path.write_text('not json')
            self.assertEqual(est.SizeModel.load(path).observations(), 0)
            self.assertEqual(est.SizeModel.load(Path(temp) / 'missing.json').observations(), 0)

    def test_sample_history_is_capped(self):
        model = est.SizeModel()
        frames_96k = est.role_frames(est.frames_from_seconds(10))
        raw = frames_96k * 4
        pages = -(-raw // est.PAGE_BYTES)
        overhead = est.PAGE_HEADER + est.PAGE_TABLE_ENTRY * pages
        for _ in range(est.MAX_SAMPLES + 5):
            model.observe({role: overhead + int(raw * 0.4) for role in est.ROLES}, frames_96k)
        self.assertEqual(len(model.samples['vocal']), est.MAX_SAMPLES)

    def test_selection_against_free_space_and_index_room(self):
        tracks = [est.estimate_track(est.frames_from_seconds(200)) for _ in range(3)]
        total = sum(t['bytes'] for t in tracks)
        high = sum(t['high_bytes'] for t in tracks)
        roomy = est.estimate_selection(tracks, free_bytes=high + 128 * 1024 * 1024)
        self.assertTrue(roomy['fits'])
        self.assertFalse(roomy['tight'])
        self.assertEqual(roomy['tracks'], 3)
        self.assertEqual(roomy['remaining_bytes'], roomy['free_bytes'] - total)
        tight = est.estimate_selection(tracks, free_bytes=total + 70 * 1024 * 1024)
        self.assertFalse(tight['fits'])
        self.assertTrue(tight['tight'])
        full = est.estimate_selection(tracks, free_bytes=total // 2)
        self.assertFalse(full['fits'])
        self.assertFalse(full['tight'])
        self.assertLess(full['remaining_bytes'], 0)
        nearly_full_index = est.estimate_selection(tracks, free_bytes=high * 2, index_bytes=est.INDEX_LIMIT - 1000)
        self.assertFalse(nearly_full_index['index_fits'])
        self.assertEqual(nearly_full_index['index_room_tracks'], 0)
        large_index = est.estimate_selection(tracks, free_bytes=high * 2, index_bytes=est.INDEX_LIMIT + 1)
        self.assertTrue(large_index['index_fits'])
        self.assertGreater(large_index['index_room_tracks'], 0)
        full_index = est.estimate_selection(tracks, free_bytes=high * 2, index_bytes=est.MAX_INDEX_LIMIT)
        self.assertFalse(full_index['index_fits'])
        self.assertEqual(full_index['index_room_tracks'], 0)

    def test_required_free_space_exceeds_the_estimate(self):
        frames = est.frames_from_seconds(300)
        self.assertGreater(est.required_free_bytes(frames), est.estimate_track(frames)['high_bytes'])

    def test_observe_bundle_learns_from_written_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / 'bundle'
            folder.mkdir()
            frames_96k = est.role_frames(est.frames_from_seconds(30))
            raw = frames_96k * 4
            pages = -(-raw // est.PAGE_BYTES)
            for role in est.ROLES:
                (folder / f'stems-sidecar-{role}.s16le.pgz').write_bytes(b'\0' * (est.PAGE_HEADER + est.PAGE_TABLE_ENTRY * pages + int(raw * 0.37)))
            self.assertTrue(est.observe_bundle(folder, frames_96k, root))
            model = est.SizeModel.load(est.model_path(root))
            self.assertEqual(model.observations(), 1)
            self.assertAlmostEqual(model.ratio('full-mix'), 0.37, places=2)
            (folder / 'stems-sidecar-vocal.s16le.pgz').unlink()
            self.assertFalse(est.observe_bundle(folder, frames_96k, root))
            self.assertEqual(json.loads(est.model_path(root).read_text())['version'], 1)


if __name__ == '__main__':
    unittest.main()
