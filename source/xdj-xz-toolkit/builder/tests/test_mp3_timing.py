"""The LAME-tag probe agrees with the helper's dr_mp3 parse, and the writer records the player timeline."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import wave

from builder import mp3_timing, overcue_writer as writer

HELPER = os.environ.get('XZ_AUDIO_HELPER')
FFMPEG = shutil.which('ffmpeg')
RATE = 44100
CLICK = RATE * 2


def click_wav(path, seconds=6):
    frames = RATE * seconds
    pcm = bytearray(frames * 4)
    for i in range(frames):
        value = int(6000 * ((i * 7) % 200 - 100) / 100)
        if i == CLICK:
            value = 31000
        pcm[i * 4:i * 4 + 4] = value.to_bytes(2, 'little', signed=True) * 2
    with wave.open(str(path), 'wb') as out:
        out.setparams((2, 2, RATE, frames, 'NONE', 'NONE'))
        out.writeframes(bytes(pcm))
    return path


def loudest_frame(pcm_bytes):
    best, at = 0, 0
    for i in range(0, len(pcm_bytes), 4):
        value = abs(int.from_bytes(pcm_bytes[i:i + 2], 'little', signed=True))
        if value > best:
            best, at = value, i // 4
    return at


class ProbeShapeTests(unittest.TestCase):
    def test_not_an_mp3_is_none(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'x.mp3'
            path.write_bytes(b'ID3' + bytes(200))
            self.assertIsNone(mp3_timing.probe(path))

    def test_latency_pad_scales_to_96k(self):
        self.assertEqual(mp3_timing.latency_pad_frames({'lame_tag': True, 'delay_frames': 1105}, 44100), 2405)
        self.assertEqual(mp3_timing.latency_pad_frames({'lame_tag': True, 'delay_frames': 1105}, 48000), 2210)
        self.assertEqual(mp3_timing.latency_pad_frames({'lame_tag': False, 'delay_frames': 1105}, 44100), 0)
        self.assertEqual(mp3_timing.latency_pad_frames(None, 44100), 0)

    def test_source_record_keeps_mp3_timing_only_for_mp3(self):
        mp3 = {'lame_tag': True, 'delay_frames': 1105, 'padding_frames': 407}
        record = writer.source_record('/Contents/a.mp3', 'ab' * 32, 'mp3', {'sample_rate': 48000, 'mp3': mp3})
        self.assertEqual(record['sample_rate'], 48000)
        self.assertEqual(record['mp3'], {**mp3, 'timeline': 'player'})
        self.assertNotIn('mp3', writer.source_record('/Contents/a.flac', 'ab' * 32, 'flac', {'sample_rate': 44100, 'mp3': mp3}))
        self.assertEqual(writer.source_record('/Contents/a.wav', 'ab' * 32, 'wav')['sample_rate'], 44100)

    def test_timeline_classification(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'a.mp3'
            source.write_bytes(b'ID3' + bytes(200))
            own = {'writer': {'name': writer.WRITER}, 'source': {'codec': 'mp3'}}
            self.assertEqual(writer.mp3_timeline({**own, 'source': {'codec': 'mp3', 'mp3': {'lame_tag': True}}}, source), 'player')
            self.assertEqual(writer.mp3_timeline(own, source), 'player')
            self.assertIsNone(writer.mp3_timeline({'writer': {'name': 'OverCue'}}, source))
            self.assertIsNone(writer.mp3_timeline(own, Path(temp) / 'a.wav'))


@unittest.skipUnless(HELPER and FFMPEG, 'Set XZ_AUDIO_HELPER and provide FFmpeg to encode fixtures')
class HelperTimelineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.wav = click_wav(self.root / 'click.wav')

    def encode(self, name, *args):
        target = self.root / name
        subprocess.run([FFMPEG, '-v', 'error', '-i', str(self.wav), '-c:a', 'libmp3lame', *args, str(target)],
                       check=True, capture_output=True)
        return target

    def helper(self, *args):
        result = subprocess.run([HELPER, *args], capture_output=True, text=True, check=True)
        return json.loads(result.stdout)

    def test_tagged_mp3_keeps_the_encoder_delay_and_reports_it(self):
        mp3 = self.encode('tagged.mp3', '-b:a', '192k')
        info = self.helper('inspect', str(mp3))
        probe = mp3_timing.probe(mp3)
        self.assertTrue(info['mp3']['lame_tag'])
        self.assertEqual(info['mp3']['delay_frames'], probe['delay_frames'])
        self.assertEqual(info['mp3']['padding_frames'], probe['padding_frames'])
        self.assertEqual(probe['encoder_delay'] + mp3_timing.DECODER_DELAY, probe['delay_frames'])
        self.assertGreater(probe['encoder_delay'], 0)
        converted = self.root / 'tagged.wav'
        self.helper('convert', str(mp3), str(converted))
        with wave.open(str(converted)) as audio:
            pcm = audio.readframes(audio.getnframes())
        gapless = subprocess.run([FFMPEG, '-v', 'error', '-i', str(mp3), '-f', 's16le', '-ac', '2', '-'],
                                 capture_output=True, check=True).stdout
        self.assertEqual(loudest_frame(pcm) - loudest_frame(gapless), probe['delay_frames'])
        self.assertEqual(len(pcm) // 4 - len(gapless) // 4, probe['delay_frames'] + probe['padding_frames'])
        self.assertEqual(writer.mp3_timeline({'writer': {'name': writer.WRITER}, 'source': {'codec': 'mp3'}}, mp3), 'gapless')

    def test_untagged_mp3_matches_every_decoder(self):
        mp3 = self.encode('plain.mp3', '-b:a', '192k', '-write_xing', '0')
        info = self.helper('inspect', str(mp3))
        self.assertEqual(info['mp3'], {'lame_tag': False, 'delay_frames': 0, 'padding_frames': 0})
        self.assertFalse(mp3_timing.probe(mp3)['lame_tag'])
        converted = self.root / 'plain.wav'
        self.helper('convert', str(mp3), str(converted))
        with wave.open(str(converted)) as audio:
            pcm = audio.readframes(audio.getnframes())
        raw = subprocess.run([FFMPEG, '-v', 'error', '-i', str(mp3), '-f', 's16le', '-ac', '2', '-'],
                             capture_output=True, check=True).stdout
        self.assertEqual(loudest_frame(pcm), loudest_frame(raw))
        self.assertEqual(len(pcm), len(raw))
        self.assertEqual(writer.mp3_timeline({'writer': {'name': writer.WRITER}, 'source': {'codec': 'mp3'}}, mp3), 'player')


if __name__ == '__main__':
    unittest.main()
