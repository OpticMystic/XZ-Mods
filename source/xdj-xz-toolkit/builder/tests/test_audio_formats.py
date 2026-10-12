"""Any-format source decoding through the native audio helper and the library rows that depend on it."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from builder import cache, stem_library, rekordbox_pdb
from builder.tests.pdb_fixture import write_export
from builder.tests.test_overcue_writer import QuietJob
from builder.tests.test_cache import fixture

HELPER = os.environ.get('XZ_AUDIO_HELPER')
FFMPEG = shutil.which('ffmpeg')


@unittest.skipUnless(HELPER and FFMPEG, 'Set XZ_AUDIO_HELPER and provide FFmpeg to encode fixtures')
class HelperFormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.wav = fixture(self.root / 'source.wav')

    def encode(self, name, *args):
        target = self.root / name
        subprocess.run([FFMPEG, '-v', 'error', '-i', str(self.wav), *args, str(target)], check=True, capture_output=True)
        return target

    def test_mp3_is_inspected_and_converted_at_its_own_rate(self):
        mp3 = self.encode('track.mp3', '-c:a', 'libmp3lame', '-b:a', '192k')
        info = cache.inspect_audio(mp3)
        self.assertEqual((info['format'], info['sample_rate'], info['channels'], info['sample_width']), ('mp3', 44100, 2, 2))
        self.assertEqual(info['frames'], info['source_frames'])
        self.assertGreaterEqual(info['frames'], 20000)
        converted = self.root / 'track.wav'
        result = json.loads(subprocess.run([HELPER, 'convert', str(mp3), str(converted)], capture_output=True, text=True, check=True).stdout)
        self.assertEqual(result['frames'], info['frames'])
        self.assertEqual(cache.inspect_audio(converted)['frames'], info['frames'])

    def test_mono_mp3_becomes_stereo(self):
        mp3 = self.encode('mono.mp3', '-ac', '1', '-c:a', 'libmp3lame')
        info = cache.inspect_audio(mp3)
        self.assertEqual((info['channels'], info['source_channels']), (2, 1))

    def test_high_rate_24_bit_wav_reports_44100_frames(self):
        wav = self.encode('hires.wav', '-c:a', 'pcm_s24le', '-ar', '96000')
        info = cache.inspect_audio(wav)
        self.assertEqual((info['sample_rate'], info['source_bits']), (96000, 24))
        self.assertEqual(info['source_frames'], 20000 * 96000 // 44100 + (1 if (20000 * 96000) % 44100 else 0))
        self.assertEqual(info['frames'], cache.frames_at_44100(info['source_frames'], 96000))

    def test_float_wav_is_accepted(self):
        wav = self.encode('float.wav', '-c:a', 'pcm_f32le')
        self.assertEqual(cache.inspect_audio(wav)['frames'], 20000)

    def test_disguised_and_unsupported_files_are_refused(self):
        disguised = self.root / 'disguised.mp3'
        disguised.write_bytes(self.wav.read_bytes())
        with self.assertRaises(ValueError):
            cache.inspect_audio(disguised)
        aiff = self.encode('track.aiff', '-c:a', 'pcm_s16be')
        with self.assertRaises(ValueError):
            cache.inspect_audio(aiff)

    def test_compatible_wav_converts_byte_for_byte(self):
        converted = self.root / 'copy.wav'
        subprocess.run([HELPER, 'convert', str(self.wav), str(converted)], check=True, capture_output=True)
        self.assertEqual(converted.read_bytes(), self.wav.read_bytes())


class FramesAt44100Tests(unittest.TestCase):
    def test_ceiling_rule_matches_polyphase_output_length(self):
        self.assertEqual(cache.frames_at_44100(20000, 44100), 20000)
        self.assertEqual(cache.frames_at_44100(48000, 48000), 44100)
        self.assertEqual(cache.frames_at_44100(48001, 48000), 44101)
        self.assertEqual(cache.frames_at_44100(96000 * 3 + 1, 96000), 44100 * 3 + 1)

    def test_helperless_inspection_only_accepts_plain_wav(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {'XZ_AUDIO_HELPER': ''}):
            root = Path(temp)
            info = cache.inspect_audio(fixture(root / 'plain.wav'))
            self.assertEqual((info['frames'], info['source_frames'], info['source_bits']), (20000, 20000, 16))
            (root / 'song.mp3').write_bytes(b'ID3' + b'\0' * 100)
            with self.assertRaisesRegex(ValueError, 'XZ_AUDIO_HELPER'):
                cache.inspect_audio(root / 'song.mp3')
            (root / 'song.aiff').write_bytes(b'FORM' + b'\0' * 100)
            with self.assertRaisesRegex(ValueError, 'Only .wav, .flac and .mp3'):
                cache.inspect_audio(root / 'song.aiff')


class LibraryFormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mapping = {1: '/Contents/Artist/Song.mp3', 2: '/Contents/Artist/Song.aiff', 3: '/Contents/Artist/Song.flac', 4: '/Contents/Artist/Song.wav'}
        write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB), self.mapping)
        for path in self.mapping.values():
            target = self.root / path.lstrip('/')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b'audio fixture')

    def rows(self):
        with patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.root / 'data')}):
            return {row['id']: row for row in stem_library.browse({'volume': str(self.root)}, QuietJob())['tracks']}

    def test_mp3_flac_and_wav_are_preparable_and_aiff_is_labelled(self):
        rows = self.rows()
        for track_id in (1, 3, 4):
            self.assertTrue(rows[track_id]['can_generate'], rows[track_id])
            self.assertEqual(rows[track_id]['stem_label'], 'Not prepared')
        self.assertFalse(rows[2]['can_generate'])
        self.assertEqual(rows[2]['stem_status'], 'unsupported')
        self.assertEqual(rows[2]['stem_label'], 'Format not supported yet')
        self.assertNotIn('OverCue', rows[2]['detail'])

    def test_rows_carry_a_size_estimate(self):
        with patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.root / 'data')}):
            result = stem_library.browse({'volume': str(self.root)}, QuietJob())
        self.assertIn('index_bytes', result)
        self.assertEqual(result['size_model']['observations'], 0)
        row = result['tracks'][0]
        self.assertEqual(row['estimated_bytes'], row['estimate']['bytes'])
        self.assertIn('full-mix', row['estimate']['roles'])
        self.assertTrue(row['estimate']['fits_format'])


if __name__ == '__main__':
    unittest.main()
