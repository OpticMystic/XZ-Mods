"""Verified downloads recover from transient failures without publishing bad bytes."""
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError
from http.client import IncompleteRead

from builder import engines
from builder.jobs import Job, Cancelled


class QuietJob(Job):
    def progress(self, *args, **kwargs):
        self.check()


class Response(io.BytesIO):
    def geturl(self):
        return 'https://example.test/artifact.zip'


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.content = b'verified publisher bytes'
        self.record = {'filename': 'artifact.zip', 'url': 'https://example.test/artifact.zip',
                       'bytes': len(self.content), 'sha256': hashlib.sha256(self.content).hexdigest()}
        self.target = self.folder / self.record['filename']
        self.job = QuietJob()
        sleeper = patch.object(engines.time, 'sleep')
        sleeper.start()
        self.addCleanup(sleeper.stop)

    def download(self, responses):
        with patch.object(engines.urllib.request, 'urlopen', side_effect=responses) as opened:
            result = engines.download(self.record, self.folder, self.job)
        self.assertEqual(result, self.target)
        self.assertEqual(result.read_bytes(), self.content)
        self.assertEqual(list(self.folder.iterdir()), [self.target])
        return opened

    def test_short_transfer_is_retried_from_zero(self):
        opened = self.download([Response(self.content[:5]), Response(self.content)])
        self.assertEqual(opened.call_count, 2)

    def test_wrong_digest_is_retried_with_cache_revalidation(self):
        opened = self.download([Response(b'x' * len(self.content)), Response(self.content)])
        retry = opened.call_args_list[1].args[0]
        self.assertEqual(retry.get_header('Cache-control'), 'no-cache')

    def test_transient_connection_failures_recover(self):
        for failure in (URLError('temporary outage'), TimeoutError('timed out'), ConnectionResetError('reset')):
            with self.subTest(failure=type(failure).__name__):
                self.target.unlink(missing_ok=True)
                self.download([failure, Response(self.content)])

    def test_interrupted_read_restarts_without_reusing_partial_bytes(self):
        response = Response(self.content)
        with patch.object(response, 'read', side_effect=[b'wrong prefix', IncompleteRead(b'partial', 20)]):
            self.download([response, Response(self.content)])

    def test_transient_http_error_recovers(self):
        error = HTTPError(self.record['url'], 503, 'Unavailable', {}, None)
        self.download([error, Response(self.content)])

    def test_persistent_truncation_is_bounded_and_keeps_existing_file(self):
        self.target.write_bytes(b'old cached file')
        with patch.object(engines.urllib.request, 'urlopen', side_effect=lambda *a, **k: Response(b'short')) as opened:
            with self.assertRaisesRegex(ValueError, 'incomplete.*3 attempts'):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 3)
        self.assertEqual(self.target.read_bytes(), b'old cached file')
        self.assertEqual(list(self.folder.iterdir()), [self.target])

    def test_persistent_digest_mismatch_is_never_published(self):
        with patch.object(engines.urllib.request, 'urlopen', side_effect=lambda *a, **k: Response(b'x' * len(self.content))) as opened:
            with self.assertRaisesRegex(ValueError, 'checksum.*3 attempts'):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 3)
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_persistent_network_failure_has_a_bounded_retry(self):
        with patch.object(engines.urllib.request, 'urlopen', side_effect=URLError('offline')) as opened:
            with self.assertRaises(URLError):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 3)
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_oversized_response_is_rejected_without_retry(self):
        with patch.object(engines.urllib.request, 'urlopen', return_value=Response(self.content + b'!')) as opened:
            with self.assertRaisesRegex(ValueError, 'exceeds'):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_permanent_http_error_is_not_retried(self):
        error = HTTPError(self.record['url'], 404, 'Not found', {}, None)
        with patch.object(engines.urllib.request, 'urlopen', side_effect=error) as opened:
            with self.assertRaises(HTTPError):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_http_redirect_is_refused(self):
        response = Response(self.content)
        with patch.object(response, 'geturl', return_value='http://example.test/artifact.zip'), \
             patch.object(engines.urllib.request, 'urlopen', return_value=response) as opened:
            with self.assertRaisesRegex(ValueError, 'HTTPS'):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_verified_cache_avoids_the_network(self):
        self.target.write_bytes(self.content)
        with patch.object(engines.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            self.assertEqual(engines.download(self.record, self.folder, self.job), self.target)

    def test_cancelled_job_never_starts_a_download(self):
        with patch.object(self.job, 'check', side_effect=Cancelled('Cancelled')), \
             patch.object(engines.urllib.request, 'urlopen') as opened:
            with self.assertRaises(Cancelled):
                engines.download(self.record, self.folder, self.job)
        opened.assert_not_called()
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_cancellation_after_response_prevents_publication(self):
        cancel_file = self.folder / 'cancel'
        self.job = QuietJob(cancel_file)
        response = Response(self.content)
        read = response.read
        def cancel_at_eof(size):
            block = read(size)
            if not block:
                cancel_file.touch()
            return block
        with patch.object(response, 'read', side_effect=cancel_at_eof), \
             patch.object(engines.urllib.request, 'urlopen', return_value=response):
            with self.assertRaises(Cancelled):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(list(self.folder.iterdir()), [cancel_file])

    def test_cancellation_during_retry_does_not_open_another_connection(self):
        def cancel(*args, **kwargs):
            raise Cancelled('Cancelled')
        with patch.object(self.job, 'progress', side_effect=cancel), \
             patch.object(engines.urllib.request, 'urlopen', side_effect=URLError('offline')) as opened:
            with self.assertRaises(Cancelled):
                engines.download(self.record, self.folder, self.job)
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(list(self.folder.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
