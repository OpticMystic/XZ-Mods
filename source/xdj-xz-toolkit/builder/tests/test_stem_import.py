import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile

from builder import engines, overcue_writer, rekordbox_pdb, stem_import
from builder.tests.pdb_fixture import write_export
from builder.tests.test_overcue_writer import QuietJob

try:
    import numpy as np
    import soundfile as sf
    from builder.tests.test_stem_align import song, delayed
except ImportError as exc:
    np = None
    AUDIO_MISSING = f'numpy, scipy and soundfile are needed for real-audio import tests ({exc})'
else:
    AUDIO_MISSING = ''

BUILDER = Path(__file__).resolve().parents[1]
SCRIPTS = ('grouped_stems.py', 'overcue_roles.py', 'stem_align.py', 'overcue_flac_pack.py')


class ProcessJob(QuietJob):
    """Runs builder child scripts with this interpreter; -I is dropped so test site-packages stay visible."""

    def run(self, argv, *, cwd=None, env=None):
        result = subprocess.run([str(arg) for arg in argv if arg != '-I'], cwd=cwd, env=env, capture_output=True,
                                text=True, encoding='utf-8', errors='replace')
        if result.returncode:
            raise RuntimeError(f'Process failed ({result.returncode}): {result.stdout}{result.stderr}')
        return result.stdout


class Environment(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.data = self.root / 'data'
        self.resources = self.root / 'resources'
        (self.resources / 'inference').mkdir(parents=True)
        for name in SCRIPTS:
            shutil.copyfile(BUILDER / name, self.resources / 'inference' / name)
        environ = patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.data), 'XZ_BUILDER_RESOURCES': str(self.resources)})
        environ.start()
        self.addCleanup(environ.stop)

    def tearDown(self):
        self.temp.cleanup()

    def write_zip(self, name, members):
        path = self.root / name
        with zipfile.ZipFile(path, 'w') as archive:
            for member, data in members.items():
                archive.writestr(member, data)
        return path


class ZipTests(Environment):
    def test_extracts_audio_members_once(self):
        path = self.write_zip('Midnight Drive Stems.zip', {'Midnight Drive (Vocals).wav': b'v', 'stems/Drums.mp3': b'd',
                                                           'cover.jpg': b'x', '__MACOSX/._Drums.mp3': b'x', '.hidden.wav': b'x'})
        first = stem_import.extract_zip(path, QuietJob())
        self.assertEqual(sorted(member.name for member in first), ['Drums.mp3', 'Midnight Drive (Vocals).wav'])
        self.assertTrue(all(member.read_bytes() in (b'v', b'd') for member in first))
        with patch.object(zipfile, 'ZipFile', side_effect=AssertionError('extracted twice')):
            self.assertEqual(stem_import.extract_zip(path, QuietJob()), first)
        self.assertTrue(path.is_file())

    def test_rejects_zip_slip_and_links(self):
        link = zipfile.ZipInfo('link.wav')
        link.external_attr = 0o120777 << 16
        for members in ({'../evil.wav': b'x'}, {'/abs.wav': b'x'}, {'C:/drive.wav': b'x'}, {'a/../../b.wav': b'x'},
                        {'ok\\..\\..\\b.wav': b'x'}, {link: b'target.wav'}):
            path = self.write_zip('bad.zip', members)
            with self.assertRaisesRegex(ValueError, 'unsafe file paths'):
                stem_import.extract_zip(path, QuietJob())
            path.unlink()
        self.assertEqual([p.name for p in stem_import.extraction_root().iterdir()], [])

    def test_size_cap_and_empty_archives(self):
        path = self.write_zip('big.zip', {'vocals.wav': b'x' * 600, 'drums.wav': b'x' * 600})
        with patch.object(stem_import, 'ZIP_LIMIT', 1000):
            with self.assertRaisesRegex(ValueError, 'more than 4 GB'):
                stem_import.extract_zip(path, QuietJob())
        with self.assertRaisesRegex(ValueError, 'no WAV'):
            stem_import.extract_zip(self.write_zip('empty.zip', {'notes.txt': b'x'}), QuietJob())
        broken = self.root / 'broken.zip'
        broken.write_bytes(b'not a zip')
        with self.assertRaisesRegex(ValueError, 'could not be read as a ZIP'):
            stem_import.extract_zip(broken, QuietJob())

    def test_sweep_removes_old_extractions(self):
        root = stem_import.extraction_root()
        (root / 'old').mkdir()
        (root / '.extract-crash').mkdir()
        (root / 'fresh').mkdir()
        stale = time.time() - stem_import.EXTRACT_TTL - 10
        os.utime(root / 'old', (stale, stale))
        stem_import.sweep_extractions()
        self.assertEqual(sorted(p.name for p in root.iterdir()), ['fresh'])

    def test_stem_files_expand_zips_and_keep_overrides(self):
        loose = self.root / 'mystery.wav'
        loose.write_bytes(b'x')
        archive = self.write_zip('Midnight Drive suno.zip', {'Vocals.wav': b'v', 'Midnight Drive.wav': b'm'})
        entries, mapping = stem_import.stem_files({'files': [str(archive), {'path': str(loose), 'group': 'drums'}]},
                                                  QuietJob(), titles=['Midnight Drive'])
        self.assertEqual([entry['group'] for entry in mapping['files']], ['vocals', 'mix', 'drums'])
        self.assertEqual(mapping['source'], 'suno')
        self.assertEqual(entries[0]['archive'], 'Midnight Drive suno.zip')
        self.assertEqual(stem_import.assigned(entries, mapping),
                         [{'path': str(entries[0]['path']), 'group': 'vocals'}, {'path': str(loose), 'group': 'drums'}])
        with self.assertRaisesRegex(ValueError, 'added twice'):
            stem_import.stem_files({'files': [str(loose), str(loose)]}, QuietJob())

    def test_alignment_requests_are_bounded(self):
        self.assertIsNone(stem_import.parse_alignment(None))
        self.assertEqual(stem_import.parse_alignment({'offset_samples': -1105, 'gain': 1}), {'offset_samples': -1105, 'gain': 1.0})
        for bad in ({'offset_samples': 1.5, 'gain': 1}, {'offset_samples': 50000, 'gain': 1}, {'offset_samples': 0, 'gain': 0},
                    {'offset_samples': 0, 'gain': float('nan')}, {'offset_samples': True, 'gain': 1}, 'x'):
            with self.assertRaises(ValueError):
                stem_import.parse_alignment(bad)


@unittest.skipIf(np is None, AUDIO_MISSING)
class ImportTests(Environment):
    """Pages are zlib and verified in Python unless XZ_OVERCUE_CHECK names the native checker for FLAC pages."""
    TRACK = '/Contents/Artist/Midnight Drive.wav'

    def setUp(self):
        super().setUp()
        self.usb = self.root / 'usb'
        write_export(self.usb.joinpath(*rekordbox_pdb.EXPORT_PDB), {7: self.TRACK})
        self.mix, self.parts = song(1, seconds=6.0)
        self.source = self.usb / self.TRACK.lstrip('/')
        self.source.parent.mkdir(parents=True)
        sf.write(self.source, self.mix, 44100, subtype='PCM_16')
        patchers = [patch.object(stem_import, 'import_python', lambda job, resources: Path(sys.executable)),
                    patch('builder.managed_usb.check_target')]
        checker = os.environ.get('XZ_OVERCUE_CHECK')
        if checker:
            shutil.copyfile(checker, self.resources / Path(checker).name)
        else:
            publish = overcue_writer.publish
            patchers.append(patch.object(overcue_writer, 'publish', lambda *args, page_python=None, source=None: publish(*args, source=source)))
        for patcher in patchers:
            patcher.start()
            self.addCleanup(patcher.stop)

    def request(self, files, **extra):
        return {'volume': str(self.usb), 'expected_identity': {}, 'file_path': self.TRACK, 'files': files, **extra}

    def suno_zip(self, delay=1105, mp3=True):
        v, d, b = (self.parts[name] for name in ('vocals', 'drums', 'bass'))
        harmonic = ('Guitar', 'Keys', 'Strings', 'Brass', 'Woodwinds', 'Synth', 'FX')
        stems = {'Vocals': v * .8, 'Backing Vocals': v * .2, 'Drums': d * .7, 'Percussion': d * .3, 'Bass': b * .4,
                 **{name: b * (.6 / len(harmonic)) for name in harmonic}}
        folder = self.root / 'suno'
        folder.mkdir()
        suffix = '.mp3' if mp3 and 'MP3' in sf.available_formats() else '.wav'
        with zipfile.ZipFile(self.root / 'Midnight Drive.zip', 'w') as archive:
            for name, audio in stems.items():
                path = folder / f'Midnight Drive ({name}){suffix}'
                sf.write(path, delayed(audio.astype(np.float32), delay), 44100,
                         **({'format': 'MP3', 'subtype': 'MPEG_LAYER_III', 'compression_level': 0.2} if suffix == '.mp3' else {'subtype': 'PCM_16'}))
                archive.write(path, path.name)
            archive.write(self.source, 'Midnight Drive.wav')
        return self.root / 'Midnight Drive.zip'

    def manifest(self, receipt):
        return json.loads((self.usb / 'CDJMODS/stems' / receipt['bundle'] / 'overcue-manifest.json').read_text(encoding='utf-8'))

    def test_suno_zip_plans_then_imports_with_a_receipt(self):
        plan = stem_import.plan(self.request([str(self.suno_zip())]), ProcessJob(), self.resources)
        self.assertFalse((self.usb / 'CDJMODS').exists())
        self.assertEqual(plan['mapping']['source'], 'suno')
        self.assertEqual(plan['verdict'], 'ready', plan['warnings'])
        self.assertEqual([item['group'] for item in plan['files']].count('mix'), 1)
        self.assertAlmostEqual(plan['alignment']['offset_samples'], 1105, delta=2)
        self.assertLessEqual(plan['alignment']['residual_db'], -20)
        self.assertTrue(plan['fit_length'])
        self.assertEqual(len(plan['import_request']['files']), 12)
        receipt = stem_import.import_groups(self.request(**plan['import_request']), ProcessJob(), self.resources)
        self.assertTrue(receipt['verified'])
        separation = self.manifest(receipt)['separation']
        self.assertEqual({key: separation[key] for key in ('method', 'source', 'layout')},
                         {'method': 'import', 'source': 'suno', 'layout': 'groups'})
        self.assertEqual(separation['alignment']['verdict'], 'aligned')
        self.assertLessEqual(separation['alignment']['residual_db'], -20)
        self.assertEqual(separation['alignment']['offset_samples'], plan['alignment']['offset_samples'])
        self.assertEqual(self.manifest(receipt)['schema'], 'overcue-stems/4')
        self.assertEqual(list(stem_import.extraction_root().iterdir()), [])

    def test_two_stem_set_splits_the_instrumental(self):
        folder = self.root / 'two'
        folder.mkdir()
        vocals, instrumental = folder / 'Midnight Drive (Vocals).wav', folder / 'Midnight Drive (Instrumental).wav'
        sf.write(vocals, delayed(self.parts['vocals'], 300), 44100, subtype='PCM_16')
        sf.write(instrumental, delayed(self.parts['drums'] + self.parts['bass'], 300), 44100, subtype='PCM_16')
        plan = stem_import.plan(self.request([str(vocals), str(instrumental)]), ProcessJob(), self.resources)
        self.assertTrue(plan['needs_drum_split'])
        self.assertEqual(plan['alignment']['offset_samples'], 300)
        with self.assertRaisesRegex(ValueError, 'Set it up first'):
            stem_import.import_groups(self.request(**plan['import_request']), ProcessJob(), self.resources)
        calls = []

        def separate_drums(source, output_dir, job, resources):
            calls.append(Path(source).name)
            stored, _ = sf.read(source, dtype='float32')
            Path(output_dir).mkdir()
            parts = {}
            for name, share in (('drums', .5), ('harmonics', .5)):
                path = Path(output_dir) / (name + '.wav')
                sf.write(path, stored * share, 44100, subtype='PCM_16')
                parts[name] = {'path': str(path), 'gain': 32768 / 32767}
            return {'model_id': 'test-drums', **parts}

        with patch.object(engines, 'separate_drums', separate_drums, create=True):
            receipt = stem_import.import_groups(self.request(**plan['import_request']), ProcessJob(), self.resources)
        self.assertEqual(calls, ['instrumental.wav'])
        separation = self.manifest(receipt)['separation']
        self.assertEqual((separation['layout'], separation['drum_split']), ('vocals_instrumental', 'test-drums'))
        self.assertEqual(separation['alignment']['verdict'], 'aligned')

    def test_wrong_track_is_refused(self):
        _, other = song(7, seconds=6.0)
        path = self.root / 'drums.wav'
        sf.write(path, sum(other.values()), 44100, subtype='PCM_16')
        plan = stem_import.plan(self.request([str(path)]), ProcessJob(), self.resources)
        self.assertEqual(plan['verdict'], 'mismatch')
        self.assertTrue(any('do not match this track' in warning for warning in plan['warnings']))
        with self.assertRaisesRegex(RuntimeError, 'do not match this track'):
            stem_import.import_groups(self.request(**plan['import_request'], accept_weak_alignment=True), ProcessJob(), self.resources)
        self.assertFalse((self.usb / 'CDJMODS').exists())

    def test_hand_assigned_import_still_works(self):
        files = []
        for name, group in (('vocals', 'vocals'), ('drums', 'drums'), ('bass', 'harmonics')):
            path = self.root / f'part {name}.wav'
            sf.write(path, self.parts[name], 44100, subtype='PCM_16')
            files.append({'path': str(path), 'group': group})
        receipt = stem_import.import_groups(self.request(files, fit_length=False), ProcessJob(), self.resources)
        separation = self.manifest(receipt)['separation']
        self.assertEqual((separation['method'], separation['source']), ('import', 'generic'))
        self.assertEqual(separation['alignment']['offset_samples'], 0)
        self.assertEqual(separation['alignment']['verdict'], 'aligned')
        self.assertEqual([item['origin'] for item in receipt['files']], ['override'] * 3)


if __name__ == '__main__':
    unittest.main()
