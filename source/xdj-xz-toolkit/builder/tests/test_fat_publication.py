"""Prove exclusive publication and failure preservation without a native rename flag."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from builder import cache


class FatPublication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def test_two_writers_cannot_replace_each_other(self):
        sources = [self.root / 'a.bin', self.root / 'b.bin']
        for index, source in enumerate(sources):
            source.write_bytes(bytes([index + 1]) * 200000)
        target = self.root / 'new.bin'
        def publish(source):
            try:
                cache._publish_fat_new(source, target)
                return source
            except FileExistsError:
                return None
        with ThreadPoolExecutor(2) as workers:
            winners = [item for item in workers.map(publish, sources) if item is not None]
        self.assertEqual(len(winners), 1)
        winner = sources.index(winners[0])
        self.assertEqual(target.read_bytes(), bytes([winner + 1]) * 200000)
        self.assertTrue(sources[1 - winner].exists())

    def test_failed_copy_removes_only_its_new_file(self):
        source = self.root / 'staged.bin'
        source.write_bytes(b'verified input')
        target = self.root / 'new.bin'
        sentinel = self.root / 'music.wav'
        sentinel.write_bytes(b'keep music')
        def fail(input, output, size):
            output.write(b'partial')
            raise OSError('Injected disk error')
        with patch.object(cache.shutil, 'copyfileobj', fail):
            with self.assertRaisesRegex(OSError, 'Injected'):
                cache._publish_fat_new(source, target)
        self.assertFalse(target.exists())
        self.assertEqual(source.read_bytes(), b'verified input')
        self.assertEqual(sentinel.read_bytes(), b'keep music')

    def test_existing_directory_is_kept_and_new_bundle_is_complete(self):
        source = self.root / 'staging'
        source.mkdir()
        (source / 'roles').mkdir()
        (source / 'roles/audio.bin').write_bytes(b'audio')
        target = self.root / 'bundle'
        target.mkdir()
        with self.assertRaises(FileExistsError):
            cache._publish_fat_new(source, target)
        target.rmdir()
        cache._publish_fat_new(source, target)
        self.assertEqual((target / 'roles/audio.bin').read_bytes(), b'audio')
        self.assertFalse(source.exists())
