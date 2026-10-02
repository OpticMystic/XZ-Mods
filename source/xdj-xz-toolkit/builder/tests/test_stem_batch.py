import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from builder import managed_usb, rekordbox_pdb, stem_batch, stem_library, service
from builder.jobs import Cancelled, Job
from builder.tests.pdb_fixture import write_export


class RecordingJob:
    def __init__(self):
        self.events = []
        self.cancelled = False
        self.commands = []
        self.verified = {'format': 'overcue-stems/4', 'verified_mixes': 7,
                         'all_pages_verified': True, 'source_identity_verified': True}

    def check(self):
        if self.cancelled:
            raise Cancelled()

    def progress(self, stage, message, **details):
        self.check()
        self.events.append(copy.deepcopy({'event': 'progress', 'stage': stage, 'message': message, **details}))

    def run(self, argv, **kwargs):
        self.commands.append(argv)
        return json.dumps(self.verified)


class BatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.mapping = {i: f'/Contents/Artist/Track {i}.wav' for i in range(1, 4)}
        write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB), self.mapping)
        for path in self.mapping.values():
            file = self.root / path.lstrip('/')
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b'original audio fixture')
        self.identity = managed_usb.identity(self.root, media=True)
        self.request = {'volume': str(self.root), 'expected_identity': self.identity,
                        'track_ids': ['1', '2', '3'], 'preset': 'umxhq'}
        self.job = RecordingJob()
        self.resources = self.root / 'resources'

    def generate(self, effect):
        with patch.object(stem_batch.engines, 'separate', side_effect=effect):
            return stem_batch.generate(self.request, self.job, self.resources)

    def test_continues_after_track_failure_and_preserves_sources(self):
        originals = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        calls = []
        def prepare(request, job, resources):
            calls.append(request['source'])
            job.progress('separate', 'Preparing audio')
            if request['source'].endswith('Track 2.wav'):
                raise RuntimeError('Inference failed')
            return {'reused': False}
        result = self.generate(prepare)
        self.assertEqual((result['prepared'], result['failed'], result['completed']), (2, 1, 3))
        self.assertEqual([i['state'] for i in result['items']], ['prepared', 'failed', 'prepared'])
        self.assertEqual(len(calls), 3)
        self.assertEqual(result['items'][1]['error'], 'Inference failed')
        self.assertEqual(originals, {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()})
        self.assertTrue(any(e['stage'] == 'separate' and e['batch']['items'][0]['state'] == 'running' for e in self.job.events))

    def test_cancel_mid_track_retains_prepared_and_unstarted_rows(self):
        def prepare(request, job, resources):
            if request['source'].endswith('Track 2.wav'):
                self.job.cancelled = True
                job.check()
            return {'reused': False}
        result = self.generate(prepare)
        self.assertTrue(result['cancelled'])
        self.assertEqual(result['prepared'], 1)
        self.assertEqual([i['state'] for i in result['items']], ['prepared', 'cancelled', 'queued'])

    def test_cancel_after_publication_keeps_completed_status(self):
        def prepare(request, job, resources):
            self.job.cancelled = True
            return {'reused': False}
        result = self.generate(prepare)
        self.assertTrue(result['cancelled'])
        self.assertEqual([i['state'] for i in result['items']], ['prepared', 'queued', 'queued'])

    def test_cancel_before_start_preserves_entire_queue(self):
        self.job.cancelled = True
        result = self.generate(lambda *args: self.fail('Preparation started after cancellation'))
        self.assertTrue(result['cancelled'])
        self.assertTrue(all(i['state'] == 'queued' for i in result['items']))

    def test_usb_identity_change_aborts_before_next_track(self):
        def prepare(request, job, resources):
            self.identity['directory'] = 'changed'
            return {'reused': False}
        result = self.generate(prepare)
        self.assertTrue(result['aborted'])
        self.assertEqual(result['prepared'], 1)
        self.assertEqual([i['state'] for i in result['items']], ['prepared', 'queued', 'queued'])
        self.assertIn('USB changed', result['error'])

    def test_identity_guard_is_used_at_publication_check(self):
        def prepare(request, job, resources):
            self.identity['directory'] = 'changed'
            job.check()
            self.fail('Publication continued on changed media')
        result = self.generate(prepare)
        self.assertTrue(result['aborted'])
        self.assertEqual([i['state'] for i in result['items']], ['failed', 'queued', 'queued'])

    def test_existing_stems_are_verified_without_inference(self):
        present = {'stem_status': 'present', 'can_generate': True, 'can_verify': True}
        with patch.object(stem_library, 'stem_status', return_value=present):
            result = self.generate(lambda *args: self.fail('Prepared track regenerated'))
        self.assertEqual(result['reused'], 3)
        self.assertEqual(result['prepared'], 0)
        self.assertEqual(len(self.job.commands), 3)
        self.assertEqual(self.job.commands[0][1], self.root / self.mapping[1].lstrip('/'))

    def test_failed_verification_leaves_prepared_stems_alone(self):
        self.job.verified['all_pages_verified'] = False
        with patch.object(stem_library, 'stem_status', return_value={'stem_status': 'present'}):
            result = self.generate(lambda *args: self.fail('Corrupt existing track overwritten'))
        self.assertEqual(result['failed'], 3)
        self.assertEqual(result['reused'], 0)

    def test_request_validation_before_engine_execution(self):
        for ids in ([], ['1', '1'], [True], ['../1'], ['4294967296'], ['0'], [1] * 101, ['999']):
            with self.subTest(ids=str(ids)[:80]):
                self.request['track_ids'] = ids
                with self.assertRaises(ValueError):
                    self.generate(lambda *args: self.fail('Invalid request ran'))
        self.request['track_ids'] = ['1']
        self.request['preset'] = 'unknown'
        with self.assertRaisesRegex(ValueError, 'preset'):
            self.generate(lambda *args: self.fail('Invalid preset ran'))

    def test_path_rebinding_in_export_is_refused_then_queue_continues(self):
        def prepare(request, job, resources):
            write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB), {**self.mapping, 2: '/Contents/Other.wav'})
            return {'reused': False}
        result = self.generate(prepare)
        self.assertEqual([i['state'] for i in result['items']], ['prepared', 'failed', 'prepared'])
        self.assertIn('identity changed', result['items'][1]['error'])

    def test_error_and_unicode_reports_fit_native_json_line_limit(self):
        items = [{'id': str(n), 'title': stem_batch._short('\U0001f3b5' * 500, 80),
                  'artist': stem_batch._short('\U0001f3b5' * 500, 40), 'state': 'failed',
                  'stage': stem_batch._short('\U0001f3b5' * 500, 32),
                  'error': stem_batch._short('\U0001f3b5' * 500, 160),
                  'message': stem_batch._short('\U0001f3b5' * 500, 120)} for n in range(100)]
        event = {'event': 'progress', 'stage': 'batch', 'message': 'Preparing audio', 'batch': stem_batch._report(items)}
        self.assertLess(len(json.dumps(event).encode('ascii')), 64000)

    def test_service_dispatch_exposes_batch(self):
        with patch.object(service, 'resources', return_value=self.resources), patch.object(stem_batch, 'generate', return_value={'format': stem_batch.FORMAT}) as generate:
            self.assertEqual(service.dispatch({**self.request, 'method': 'generate_library_stem_batch'}, self.job)['format'], stem_batch.FORMAT)
            generate.assert_called_once()


if __name__ == '__main__':
    unittest.main()
