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
        with self.assertRaisesRegex(ValueError, r'disguised\.mp3 holds WAV audio, although its name ends in \.mp3'):
            cache.inspect_audio(disguised)
        m4a = self.encode('track.m4a', '-c:a', 'aac')
        with self.assertRaisesRegex(ValueError, r'XZ Mods cannot use track\.m4a\. It prepares and imports stems for WAV, AIFF, FLAC and MP3 tracks, not M4A files'):
            cache.inspect_audio(m4a)

    def converted(self, source):
        target = self.root / (source.name + '.converted.wav')
        subprocess.run([HELPER, 'convert', str(source), str(target)], check=True, capture_output=True)
        return target.read_bytes()

    def test_aiff_and_aifc_read_like_the_same_wav(self):
        """Issue #1: AIFF is a common DAW and rekordbox format; it must decode to the same PCM as WAV."""
        cases = [('pcm16.aiff', ('-c:a', 'pcm_s16be'), 'pcm_s16le', 16),
                 ('pcm24-48k.aif', ('-c:a', 'pcm_s24be', '-ar', '48000'), 'pcm_s24le', 24),
                 ('sowt-aifc.aif', ('-c:a', 'pcm_s16le'), 'pcm_s16le', 16)]
        for name, args, wav_codec, bits in cases:
            with self.subTest(name):
                aiff = self.encode(name, *args)
                wav = self.encode(name + '.wav', '-c:a', wav_codec, *args[2:])
                info = cache.inspect_audio(aiff)
                self.assertEqual((info['format'], info['source_bits']), ('aiff', bits))
                self.assertEqual(info['frames'], cache.inspect_audio(wav)['frames'])
                self.assertEqual(self.converted(aiff), self.converted(wav))
        float_aifc = self.encode('float-aifc.aif', '-c:a', 'pcm_f32be')
        self.assertEqual(cache.inspect_audio(float_aifc)['frames'], 20000)

    def test_rf64_wav_and_id3_tagged_flac_are_read(self):
        rf64 = self.encode('rf64.wav', '-c:a', 'pcm_s24le', '-rf64', 'always')
        self.assertEqual(cache.inspect_audio(rf64)['frames'], 20000)
        flac = self.encode('plain.flac', '-c:a', 'flac')
        frame = b'\0\0\0Song'
        tag = b'ID3\x03\x00\x00' + bytes([0, 0, 0, 10 + len(frame)]) + b'TIT2' + len(frame).to_bytes(4, 'big') + b'\0\0' + frame
        tagged = self.root / 'tagged.flac'
        tagged.write_bytes(tag + flac.read_bytes())
        self.assertEqual(cache.inspect_audio(tagged)['frames'], 20000)
        self.assertEqual(self.converted(tagged), self.converted(flac))

    def test_each_refusal_names_its_own_reason(self):
        """Issue #1: one message used to cover every failure. Each cause now has its own plain reason."""
        old = 'Invalid, truncated, unsupported or oversized audio'
        surround = self.encode('surround.wav', '-ac', '6', '-c:a', 'pcm_s16le')
        alaw = self.encode('alaw.wav', '-c:a', 'pcm_alaw')
        empty = self.root / 'empty.wav'
        data = bytearray(self.wav.read_bytes())
        at = data.find(b'data')
        data[4:8] = data[at + 4:at + 8] = bytes(4)
        empty.write_bytes(bytes(data[:at + 8]))
        truncated = self.encode('cut.flac', '-c:a', 'flac')
        truncated.write_bytes(truncated.read_bytes()[:-2000])
        junk = self.root / 'junk.mp3'
        junk.write_bytes(b'\0' * 64 + self.wav.read_bytes())
        cases = [(surround, r'surround\.wav has 6 audio channels\. XZ Mods reads mono and stereo'),
                 (alaw, r'alaw\.wav holds compressed audio \(compressed WAV audio, format 0x6\)'),
                 (empty, r'empty\.wav holds no audio'),
                 (truncated, r'cut\.flac ends early or is damaged \(\d+ of 20000 frames\)'),
                 (junk, r'junk\.mp3 is not WAV, AIFF, FLAC or MP3 audio, although its name ends in \.mp3')]
        for path, reason in cases:
            with self.subTest(path.name):
                with self.assertRaisesRegex(ValueError, reason) as caught:
                    cache.inspect_audio(path)
                self.assertNotIn(old, str(caught.exception))

    def test_tracks_longer_than_the_stem_format_say_so(self):
        long = self.root / 'mix.flac'
        subprocess.run([FFMPEG, '-v', 'error', '-f', 'lavfi', '-i', 'anullsrc=r=44100:cl=stereo', '-t', '1440',
                        '-c:a', 'flac', str(long)], check=True, capture_output=True)
        with self.assertRaisesRegex(ValueError, r'mix\.flac is longer than stems allow \(about 23 minutes\)'):
            cache.inspect_audio(long)

    def test_existing_convert_output_is_kept_and_named(self):
        target = self.root / 'out.wav'
        subprocess.run([HELPER, 'convert', str(self.wav), str(target)], check=True, capture_output=True)
        again = subprocess.run([HELPER, 'convert', str(self.wav), str(target)], capture_output=True, text=True)
        self.assertEqual(again.returncode, 1)
        self.assertTrue(again.stderr.startswith('output: '), again.stderr)
        self.assertEqual(target.read_bytes(), self.wav.read_bytes())

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
            with self.assertRaisesRegex(ValueError, 'XZ_AUDIO_HELPER'):
                cache.inspect_audio(root / 'song.aiff')
            (root / 'song.m4a').write_bytes(b'\0\0\0\x20ftypM4A ' + b'\0' * 100)
            with self.assertRaisesRegex(ValueError, 'XZ Mods cannot use song.m4a. It prepares and imports stems for WAV, AIFF, FLAC and MP3 tracks, not M4A files'):
                cache.inspect_audio(root / 'song.m4a')

    def test_unknown_helper_output_is_still_shown(self):
        self.assertEqual(cache.helper_problem(Path('a.wav'), 'something new\n'), 'XZ Mods could not read a.wav: something new')
        self.assertEqual(cache.helper_problem(Path('a.wav'), 'rate: 4000 Hz\n'),
                         'a.wav has a sample rate of 4000 Hz. XZ Mods reads 8 kHz to 192 kHz. Export it at 44.1 kHz or 48 kHz.')


class LibraryFormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mapping = {1: '/Contents/Artist/Song.mp3', 2: '/Contents/Artist/Song.m4a', 3: '/Contents/Artist/Song.flac', 4: '/Contents/Artist/Song.wav',
                        5: '/Contents/Artist/Song.aiff', 6: '/Contents/Artist/Other.AIF'}
        write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB), self.mapping)
        for path in self.mapping.values():
            target = self.root / path.lstrip('/')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b'audio fixture')

    def rows(self):
        with patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.root / 'data')}):
            return {row['id']: row for row in stem_library.browse({'volume': str(self.root)}, QuietJob())['tracks']}

    def test_mp3_flac_wav_and_aiff_are_preparable_and_aac_is_labelled(self):
        """Issue #1: AIFF tracks were refused ("Not yet"), so their stems could be neither prepared nor imported."""
        rows = self.rows()
        for track_id in (1, 3, 4, 5, 6):
            self.assertTrue(rows[track_id]['can_generate'], rows[track_id])
            self.assertEqual(rows[track_id]['stem_status'], 'missing')
            self.assertEqual(rows[track_id]['stem_label'], 'Not prepared')
        self.assertFalse(rows[2]['can_generate'])
        self.assertEqual(rows[2]['stem_status'], 'unsupported')
        self.assertEqual(rows[2]['stem_label'], 'Format not supported yet')
        self.assertIn('AAC (M4A)', rows[2]['detail'])
        self.assertIn('Export the track as WAV, AIFF, FLAC or MP3', rows[2]['detail'])
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
