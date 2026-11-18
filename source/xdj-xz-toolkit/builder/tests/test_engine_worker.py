import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

from builder import engines, managed_usb, rekordbox_pdb, stem_batch
from builder.jobs import Cancelled, Job
from builder.tests import test_stem_batch
from builder.tests.pdb_fixture import write_export

CHILD = '''import json, sys, time
while True:
    line = sys.stdin.readline()
    if not line:
        break
    request = json.loads(line)
    if request.get('op') == 'quit':
        break
    mode = request.get('mode')
    print('loading weights', flush=True)
    print(json.dumps({'note': 'not an event'}), flush=True)
    if mode == 'crash':
        sys.stderr.write('engine exploded on purpose\\n')
        sys.stderr.flush()
        sys.exit(3)
    print(json.dumps({'event': 'progress', 'stage': 'separate-vocals', 'pct': 50, 'overall': 40, 'message': 'half'}), flush=True)
    if mode == 'hang':
        time.sleep(60)
    print(json.dumps({'event': 'progress', 'stage': 'postprocess', 'pct': 100, 'overall': 100, 'message': 'encoded'}), flush=True)
    print(json.dumps({'event': 'done', 'result': request['output'], 'device': 'cpu',
                      'timing': {'frames': 44100, 'separate_seconds': 1.0, 'postprocess_seconds': 0.5}}), flush=True)
'''

PROGRESS = [{'event': 'progress', 'stage': 'separate-vocals', 'pct': 50, 'overall': 40, 'message': 'half'},
            {'event': 'progress', 'stage': 'postprocess', 'pct': 100, 'overall': 100, 'message': 'encoded'}]


class WorkerTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.script = self.root / 'child.py'
        self.script.write_text(CHILD, encoding='utf-8')

    def spawn(self, job=None):
        worker = (job or Job()).worker([sys.executable, '-I', self.script])
        self.addCleanup(worker.close)
        return worker

    def test_request_returns_done(self):
        worker = self.spawn()
        final = worker.request({'op': 'separate', 'output': 'one/result.json'}, lambda event: None)
        self.assertEqual(final['event'], 'done')
        self.assertEqual(final['result'], 'one/result.json')
        self.assertEqual(final['device'], 'cpu')

    def test_only_progress_events_are_forwarded_in_order(self):
        worker = self.spawn()
        seen = []
        worker.request({'op': 'separate', 'output': 'one/result.json'}, seen.append)
        self.assertEqual(seen, PROGRESS)

    def test_sequential_requests_reuse_the_process(self):
        worker = self.spawn()
        pid = worker.process.pid
        first = worker.request({'op': 'separate', 'output': 'one/result.json'}, lambda event: None)
        self.assertIsNone(worker.process.poll())
        second = worker.request({'op': 'separate', 'output': 'two/result.json'}, lambda event: None)
        self.assertEqual((first['result'], second['result']), ('one/result.json', 'two/result.json'))
        self.assertEqual(worker.process.pid, pid)

    def test_close_ends_the_process(self):
        worker = self.spawn()
        worker.request({'op': 'separate', 'output': 'one/result.json'}, lambda event: None)
        worker.close()
        self.assertEqual(worker.process.poll(), 0)

    def test_child_exit_without_terminal_event_reports_stderr(self):
        worker = self.spawn()
        with self.assertRaises(RuntimeError) as raised:
            worker.request({'op': 'separate', 'mode': 'crash'}, lambda event: None)
        self.assertIn('(3)', str(raised.exception))
        self.assertIn('engine exploded on purpose', str(raised.exception))

    def test_cancel_terminates_the_child(self):
        cancel = self.root / 'cancel'
        worker = self.spawn(Job(cancel_file=cancel))
        seen = []

        def on_event(event):
            seen.append(event)
            if event.get('event') == 'progress':
                cancel.write_text('cancel')

        with self.assertRaises(Cancelled):
            worker.request({'op': 'separate', 'mode': 'hang', 'output': 'one/result.json'}, on_event)
        self.assertIsNotNone(worker.process.poll())
        self.assertIn(PROGRESS[0], seen)
        self.assertNotIn(PROGRESS[1], seen)


class RecordingJob:
    def __init__(self):
        self.events = []

    def check(self):
        pass

    def progress(self, stage, message, **details):
        self.events.append({'stage': stage, 'message': message, **details})


class ReplayWorker:
    def __init__(self, events, final):
        self.events, self.final, self.payloads = events, final, []

    def request(self, payload, on_event):
        self.payloads.append(payload)
        for event in self.events:
            on_event(event)
        return self.final


class EngineSeparateTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.output = self.root / 'separated'
        self.output.mkdir()
        stem = self.output / 'vocals.wav'
        stem.write_bytes(b'vocals pcm')
        self.result = {'model_id': 'xz-balanced-deadbeef', 'identity': {'device': 'cuda'},
                       'stems': [{'name': 'vocals', 'file': 'vocals.wav', 'gain': 1.0,
                                  'sha256': hashlib.sha256(b'vocals pcm').hexdigest()}]}
        self.timing = {'frames': 44100 * 60, 'load_seconds': 4.0, 'separate_seconds': 10.0, 'postprocess_seconds': 2.0}
        self.job = RecordingJob()

    def engine(self, events, final):
        (self.output / 'result.json').write_text(json.dumps(self.result), encoding='utf-8')
        engine = engines.Engine.__new__(engines.Engine)
        engine.preset, engine.job, engine.resources = 'balanced', self.job, self.root / 'resources'
        engine.worker = ReplayWorker(events, final)
        return engine

    def done(self):
        return {'event': 'done', 'result': str(self.output / 'result.json'), 'device': 'cuda', 'timing': self.timing}

    def test_forwards_progress_and_records_timing(self):
        events = [{'event': 'ready', 'device': 'cuda'},
                  {'event': 'progress', 'stage': 'separate-vocals', 'pct': 50, 'overall': 40, 'message': 'half',
                   'device': 'cuda', 'model': 'melband-roformer-kj-vocals'},
                  {'event': 'progress', 'stage': 'device', 'pct': 0, 'message': 'The GPU failed', 'fallback': True},
                  {'event': 'progress'}]
        engine = self.engine(events, self.done())
        with mock.patch('builder.engines.record_timing') as record:
            result = engine.separate(self.root / 'source.wav', self.output, ('vocals', 'harmonics'))
        self.assertEqual(result, self.result)
        self.assertEqual(engine.worker.payloads, [{'op': 'separate', 'input': str(self.root / 'source.wav'),
                                                   'output_dir': str(self.output), 'parts': ['vocals', 'harmonics']}])
        self.assertEqual(self.job.events, [
            {'stage': 'separate-vocals', 'message': 'half', 'pct': 50, 'overall': 40, 'device': 'cuda'},
            {'stage': 'device', 'message': 'The GPU failed', 'pct': 0, 'fallback': True},
            {'stage': 'separate', 'message': ''}])
        record.assert_called_once_with('balanced', 'cuda', self.timing, ('vocals', 'harmonics'))

    def test_stem_hash_mismatch_is_refused(self):
        self.result['stems'][0]['sha256'] = '0' * 64
        engine = self.engine([], self.done())
        with mock.patch('builder.engines.record_timing') as record:
            with self.assertRaisesRegex(ValueError, 'integrity'):
                engine.separate(self.root / 'source.wav', self.output, ('vocals', 'harmonics'))
        record.assert_not_called()

    def test_failed_event_raises_its_error(self):
        engine = self.engine([], {'event': 'failed', 'error': 'RuntimeError: CUDA out of memory', 'diagnostics': {}})
        with mock.patch('builder.engines.record_timing') as record:
            with self.assertRaisesRegex(RuntimeError, 'CUDA out of memory'):
                engine.separate(self.root / 'source.wav', self.output, ('drums',))
        record.assert_not_called()


class BatchEngineTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        mapping = {i: f'/Contents/Artist/Track {i}.wav' for i in range(1, 4)}
        write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB), mapping)
        for path in mapping.values():
            file = self.root / path.lstrip('/')
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b'original audio fixture')
        self.request = {'volume': str(self.root), 'expected_identity': managed_usb.identity(self.root, media=True),
                        'track_ids': ['1', '2', '3'], 'preset': 'balanced'}
        self.job = test_stem_batch.RecordingJob()

    def test_one_engine_serves_the_whole_batch(self):
        engines_used = []

        def separate(request, job, resources, engine=None):
            engines_used.append(engine)
            job.progress('separate-vocals', 'half', pct=50, overall=40)
            return {'reused': False}

        with mock.patch.object(test_stem_batch.FakeEngine, 'opened', []), \
                mock.patch.object(stem_batch.engines, 'Engine', test_stem_batch.FakeEngine), \
                mock.patch.object(stem_batch.engines, 'separate', side_effect=separate):
            result = stem_batch.generate(self.request, self.job, self.root / 'resources')
            opened = list(test_stem_batch.FakeEngine.opened)
        self.assertEqual(len(opened), 1)
        self.assertEqual(engines_used, [opened[0]] * 3)
        self.assertTrue(opened[0].closed)
        self.assertEqual(opened[0].preset, 'balanced')
        self.assertEqual(result['prepared'], 3)
        self.assertTrue(all('pct' not in item for item in result['items']))

    def test_track_progress_forwards_pct(self):
        def separate(request, job, resources, engine=None):
            job.progress('separate-vocals', 'half', pct=50, overall=40)
            return {'reused': False}

        with mock.patch.object(test_stem_batch.FakeEngine, 'opened', []), \
                mock.patch.object(stem_batch.engines, 'Engine', test_stem_batch.FakeEngine), \
                mock.patch.object(stem_batch.engines, 'separate', side_effect=separate):
            stem_batch.generate(self.request, self.job, self.root / 'resources')
        forwarded = [event for event in self.job.events if event['stage'] == 'separate-vocals']
        self.assertEqual(len(forwarded), 3)
        for index, event in enumerate(forwarded):
            self.assertEqual((event['pct'], event['overall']), (50, 40))
            item = event['batch']['items'][index]
            self.assertEqual((item['state'], item['stage'], item['pct'], item['message']),
                             ('running', 'separate-vocals', 50, 'half'))


if __name__ == '__main__':
    unittest.main()
