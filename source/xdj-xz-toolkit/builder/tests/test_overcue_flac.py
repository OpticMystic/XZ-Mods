"""Real FLAC publishing, native verification and read-only library browsing."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from builder import overcue_writer as writer, stem_library
from builder.tests.test_overcue_writer import QuietJob, render_roles


class ProcessJob(QuietJob):
    def run(self, argv):
        return subprocess.run(list(map(str, argv)), check=True, capture_output=True, text=True).stdout


class FlacTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('XZ_BUILDER_RESOURCES') and os.environ.get('XZ_FLAC_PYTHON'),
                         'Set the packaged resources and managed audio interpreter for real FLAC acceptance')
    def test_publish_reuse_migrate_browse_and_corruption(self):
        resources = Path(os.environ['XZ_BUILDER_RESOURCES']).resolve()
        checker = resources / 'xz-overcue-check.exe'
        python = os.environ['XZ_FLAC_PYTHON']
        with tempfile.TemporaryDirectory(prefix='xz-flac-writer-') as temporary:
            root = Path(temporary)
            source = root / 'USB/Contents/Artist/track.wav'
            source.parent.mkdir(parents=True)
            original = b'original track bytes must remain unchanged'
            source.write_bytes(original)
            roles = root / 'roles'
            records = render_roles(roles, 40000)
            args = (source.parents[2], '/Contents/Artist/track.wav', 1, hashlib.sha256(original).hexdigest(),
                    'wav', roles, 'test-model', 'test-version', ProcessJob())
            legacy = writer.publish(*args)
            before = {p.name: p.read_bytes() for p in (source.parents[2] / 'CDJMODS/stems' / legacy['bundle']).iterdir()}
            receipt = writer.publish(*args, page_python=python)
            self.assertEqual(receipt['page_codec'], 'flac-96k')
            self.assertNotEqual(receipt['bundle'], legacy['bundle'])
            self.assertTrue(receipt['replaced_previous'])
            folder = source.parents[2] / 'CDJMODS/stems' / receipt['bundle']
            self.assertEqual(source.read_bytes(), original)
            # The verified FLAC bundle replaced the zlib one, so the zlib folder is removed.
            self.assertTrue(before)
            self.assertEqual(receipt['removed_previous'], [legacy['bundle']])
            self.assertFalse((folder.parent / legacy['bundle']).exists())
            for role in writer.ROLES:
                self.assertEqual((folder / writer.role_file(role)).read_bytes()[:8], b'OVPGZ003')
            checked = subprocess.run([str(checker), str(source)], capture_output=True, text=True, check=True)
            data = json.loads(checked.stdout)
            self.assertEqual((data['page_codec'], data['verified_mixes'], data['flac_mixes'], data['layout']),
                             ('flac-96k', 4, 4, 'seven-file'))
            self.assertTrue(data['source_identity_verified'])
            oracle = os.environ.get('XZ_OVERCUE_ORACLE')
            if oracle:
                subprocess.run([sys.executable, oracle, str(source.parents[2]), args[1]], check=True)
            self.assertTrue(writer.publish(*args, page_python=python)['reused'])
            index = json.loads((source.parents[2] / 'CDJMODS/index.json').read_text())
            track = {'id': 1, 'file_path': args[1], 'sample_rate': 44100}
            state = stem_library.stem_status(source.parents[2], track, index, None)
            self.assertEqual(state['page_codec'], 'flac-96k')
            self.assertIn('FLAC', state['stem_label'])
            (folder / 'overcue-manifest.json').unlink()
            state = stem_library.stem_status(source.parents[2], track, index, None)
            self.assertTrue(state['can_verify'])
            checked = subprocess.run([str(checker), str(source)], capture_output=True, text=True, check=True)
            self.assertTrue(json.loads(checked.stdout)['all_pages_verified'])
            role = writer.ROLES[0]
            with self.assertRaises(ValueError):
                writer.verify_role(folder / writer.role_file(role), '0' * 64, 40000)
            role_file = folder / writer.role_file(role)
            valid = role_file.read_bytes()
            role_file.write_bytes(valid[:-1] + bytes([valid[-1] ^ 1]))
            with self.assertRaises(ValueError):
                writer.verify_role(role_file, records[role]['sha256'], 40000)
            self.assertEqual(source.read_bytes(), original)


BUILDER = Path(__file__).resolve().parents[1]


def write_wav(path, samples, rate=44100):
    import wave
    with wave.open(str(path), 'wb') as target:
        target.setnchannels(2)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(samples.astype('<i2').tobytes())
    return path


@unittest.skipUnless(os.environ.get('XZ_BUILDER_RESOURCES') and os.environ.get('XZ_FLAC_PYTHON'),
                     'Set the packaged resources and managed audio interpreter for real FLAC acceptance')
class FortyFourOneTests(unittest.TestCase):
    """The default 44.1 kHz profile end to end: overcue_roles, FLAC pages, the unit's reader, the spec validator."""

    def setUp(self):
        import numpy as np
        import shutil
        self.np = np
        self.temp = tempfile.TemporaryDirectory(prefix='xz-flac-44k1-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.python = os.environ['XZ_FLAC_PYTHON']
        # Builder scripts from this tree beside the packaged native checker.
        self.resources = self.root / 'resources'
        (self.resources / 'inference').mkdir(parents=True)
        packaged = Path(os.environ['XZ_BUILDER_RESOURCES']).resolve()
        self.checker = self.resources / 'xz-overcue-check.exe'
        shutil.copyfile(packaged / 'xz-overcue-check.exe', self.checker)
        for name in ('overcue_flac_pack.py', 'stem_waveforms.py', 'overcue_roles.py'):
            shutil.copyfile(BUILDER / name, self.resources / 'inference' / name)
        from unittest.mock import patch
        environ = patch.dict(os.environ, {'XZ_BUILDER_RESOURCES': str(self.resources)})
        environ.start()
        self.addCleanup(environ.stop)
        self.usb = self.root / 'USB'
        (self.usb / 'CDJMODS/index-v2').mkdir(parents=True)  # an OverCue-upgraded USB

    def separated(self, name, frames, seed):
        """A mix and its three separated parts, quiet enough that no headroom gain applies."""
        rng = self.np.random.default_rng(seed)
        parts = {part: self.np.clip(self.np.rint(rng.normal(0, 2500, (frames, 2))), -8000, 8000).astype('<i2')
                 for part in ('vocals', 'drums', 'harmonics')}
        folder = self.root / name
        folder.mkdir()
        paths = {part: write_wav(folder / f'{part}.wav', pcm) for part, pcm in parts.items()}
        paths['mix'] = write_wav(folder / 'mix.wav', sum(pcm.astype(self.np.int32) for pcm in parts.values()))
        return parts, paths

    def render(self, paths, output, rate=None):
        subprocess.run([self.python, '-I', str(self.resources / 'inference/overcue_roles.py'), '--mix', str(paths['mix']),
                        '--vocals', str(paths['vocals']), '--vocals-gain', '1', '--harmonics', str(paths['harmonics']),
                        '--harmonics-gain', '1', '--output-dir', str(output),
                        *(['--rate', str(rate)] if rate else [])], check=True, capture_output=True)
        return output

    def track(self, file_path, data):
        source = self.usb / file_path.lstrip('/')
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(data)
        return source, hashlib.sha256(data).hexdigest()

    def check(self, source):
        return json.loads(subprocess.run([str(self.checker), str(source)], capture_output=True, text=True, check=True).stdout)

    def oracle(self, file_path):
        oracle = os.environ.get('XZ_OVERCUE_ORACLE')
        if oracle:
            subprocess.run([sys.executable, oracle, str(self.usb), file_path], check=True, capture_output=True)

    def test_default_bundle_is_44k1_bit_exact_and_accepted(self):
        np = self.np
        frames = 70000  # three full FLAC pages and a short one
        old_path, new_path = '/Contents/Artist/old.wav', '/Contents/Artist/new.mp3'
        old_source, old_sha = self.track(old_path, b'an older track prepared at 96 kHz')
        new_source, new_sha = self.track(new_path, b'mp3 bytes; the reader only checks their hash')

        # A 96 kHz bundle published before the default changed stays readable beside new ones.
        _, old_paths = self.separated('old-parts', 30000, 1)
        old = writer.publish(self.usb, old_path, 1, old_sha, 'wav', self.render(old_paths, self.root / 'old-roles', 96000),
                             'model', 'v', ProcessJob(), page_python=self.python)
        self.assertEqual((old['page_codec'], old['sample_rate'], old['frames']), ('flac-96k', 96000, -(-30000 * 320 // 147)))

        parts, paths = self.separated('new-parts', frames, 2)
        roles = self.render(paths, self.root / 'new-roles')
        mp3 = {'lame_tag': True, 'delay_frames': 1105, 'padding_frames': 300}
        receipt = writer.publish(self.usb, new_path, 2, new_sha, 'mp3', roles, 'model', 'v', ProcessJob(),
                                 page_python=self.python, source={'sample_rate': 44100, 'mp3': mp3})
        self.assertEqual((receipt['page_codec'], receipt['sample_rate'], receipt['frames']), ('flac-44k1', 44100, frames))
        self.assertTrue(receipt['waveforms'])
        self.assertNotIn('waveform_error', receipt)
        folder = self.usb / 'CDJMODS/stems' / receipt['bundle']

        # Stored pages decode to the separation output sample for sample.
        import soundfile as sf
        import io
        import struct
        decoded = {}
        for role in writer.ROLES:
            data = (folder / writer.role_file(role)).read_bytes()
            self.assertEqual(data[:8], b'OVPGZ004')
            page_bytes, count, total = struct.unpack('>IIQ', data[8:24])
            self.assertEqual((page_bytes, count, total), (131072, 3, frames * 4))
            pcm = bytearray()
            for number in range(count):
                offset, length, expanded = struct.unpack_from('>QII', data, 24 + 48 * number)
                page, rate = sf.read(io.BytesIO(data[offset:offset + length]), dtype='int16', always_2d=True)
                self.assertEqual((rate, len(page) * 4), (44100, expanded))
                pcm += page.astype('<i2').tobytes()
            decoded[role] = np.frombuffer(bytes(pcm), '<i2').reshape(-1, 2)
        for role, part in (('vocal', 'vocals'), ('harmonics', 'harmonics')):
            np.testing.assert_array_equal(decoded[role], parts[part])
        # Two-part separation derives drums as mix - vocals - harmonics (mix PCM/32768, parts PCM/32767), in place.
        mix = sum(pcm.astype(np.int32) for pcm in parts.values()).astype(np.float32) * np.float32(1 / 32768)
        unit = np.float32(1 / 32767)
        drums = mix - parts['vocals'].astype(np.float32) * unit - parts['harmonics'].astype(np.float32) * unit
        np.testing.assert_array_equal(decoded['drums'], np.rint(drums * np.float32(32767)).astype('<i2'))
        self.assertLessEqual(int(np.abs(decoded['drums'].astype(np.int32) - parts['drums']).max()), 1)

        # Manifest and index entry carry the cdj2000-44k1/1 profile and agree with the files.
        manifest = json.loads((folder / 'overcue-manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['schema'], 'overcue-stems/5')
        self.assertEqual(manifest['runtime'], {'sample_rate': 44100, 'channels': 2, 'format': 's16le', 'frames': frames,
                                               'latency_pad_frames': 0, 'headroom_gain': 1.0, 'page_codec': 'flac-44k1',
                                               'source_timing_policy': 'decoded-timeline/1'})
        self.assertEqual(manifest['source']['mp3'], {**mp3, 'timeline': 'player'})
        self.assertEqual(writer.mp3_timeline(manifest, new_source), 'player')
        index = json.loads((self.usb / 'CDJMODS/index.json').read_text(encoding='utf-8'))
        entry = index['tracks']['2']
        self.assertEqual({key: entry[key] for key in ('frames', 'three_part', 'page_bytes', 'page_codec', 'sample_rate',
                                                       'preparation_profile', 'source_timing_policy', 'source_sha256')},
                         {'frames': frames, 'three_part': 1, 'page_bytes': 131072, 'page_codec': 'flac-44k1',
                          'sample_rate': 44100, 'preparation_profile': 'cdj2000-44k1/1',
                          'source_timing_policy': 'decoded-timeline/1', 'source_sha256': new_sha})
        for role in writer.ROLES:
            key = role.replace('-', '_')
            data = (folder / writer.role_file(role)).read_bytes()
            self.assertEqual(entry[key + '_sha256'], hashlib.sha256(decoded[role].tobytes()).hexdigest())
            self.assertEqual(entry[key + '_page_table_sha256'], hashlib.sha256(data[:24 + 48 * 3]).hexdigest())
            self.assertEqual(manifest['roles'][role]['page_table_sha256'], entry[key + '_page_table_sha256'])
            self.assertEqual(manifest['roles'][role]['bytes'], frames * 4)
        self.assertNotIn('sample_rate', index['tracks']['1'])

        # index-v2 records are the spec's binding fields only, for both profiles.
        records = writer.overcue_index_v2.read(self.usb / 'CDJMODS/index-v2')
        for key in ('1', '2'):
            self.assertEqual(records[1, int(key)], writer.overcue_index_v2.record(1, key, index['tracks'][key]))
        self.assertNotIn(b'sample_rate', records[1, 2])

        # The unit's reader (linked into the checker) and the published validator accept both bundles.
        checked = self.check(new_source)
        self.assertEqual((checked['sample_rate'], checked['page_codec'], checked['frames'], checked['verified_mixes'],
                          checked['layout'], checked['source_identity_verified']),
                         (44100, 'flac-44k1', frames, 4, 'seven-file', True))
        self.assertEqual((self.check(old_source)['sample_rate'], self.check(old_source)['page_codec']), (96000, 'flac-96k'))
        self.oracle(new_path)
        self.oracle(old_path)

        # Waveforms are 150 columns per second at 294 frames; the library reports the profile.
        for name in writer.WAVEFORM_FILES:
            self.assertEqual(writer.check_waveform(folder / name, frames, 44100)['columns'], -(-frames // 294))
        state = stem_library.stem_status(self.usb, {'id': 2, 'file_path': new_path}, index, None)
        self.assertEqual((state['page_codec'], state['stem_sample_rate'], state['stem_waveforms']), ('flac-44k1', 44100, True))
        state = stem_library.stem_status(self.usb, {'id': 1, 'file_path': old_path}, index, None)
        self.assertEqual((state['page_codec'], state['stem_sample_rate']), ('flac-96k', 96000))

        # Waveforms can be drawn again from the OVPGZ004 pages alone.
        drawn = {name: (folder / name).read_bytes() for name in writer.WAVEFORM_FILES}
        for name in writer.WAVEFORM_FILES:
            (folder / name).unlink()
        self.assertTrue(writer.add_waveforms(self.usb, new_path, self.python, ProcessJob())['added'])
        self.assertEqual({name: (folder / name).read_bytes() for name in writer.WAVEFORM_FILES}, drawn)

        # The same roles reuse the bundle; preparing the old track again at 44.1 kHz replaces its 96 kHz entry.
        again = writer.publish(self.usb, new_path, 2, new_sha, 'mp3', roles, 'model', 'v', ProcessJob(),
                               page_python=self.python, source={'sample_rate': 44100, 'mp3': mp3})
        self.assertTrue(again['reused'])
        renewed = writer.publish(self.usb, old_path, 1, old_sha, 'wav', self.render(old_paths, self.root / 'old-44k1'),
                                 'model', 'v', ProcessJob(), page_python=self.python)
        self.assertTrue(renewed['replaced_previous'])
        self.assertEqual((renewed['sample_rate'], renewed['frames']), (44100, 30000))
        self.assertEqual(self.check(old_source)['sample_rate'], 44100)
        self.oracle(old_path)

        # A 44.1 kHz bundle under an entry without sample_rate is refused by the unit's reader.
        index = json.loads((self.usb / 'CDJMODS/index.json').read_text(encoding='utf-8'))
        del index['tracks']['2']['sample_rate']
        (self.usb / 'CDJMODS/index.json').write_text(json.dumps(index), encoding='utf-8')
        refused = subprocess.run([str(self.checker), str(new_source)], capture_output=True, text=True)
        self.assertNotEqual(refused.returncode, 0)


if __name__ == '__main__':
    unittest.main()
