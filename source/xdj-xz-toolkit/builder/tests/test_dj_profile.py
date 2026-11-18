"""DJ profiles on the USB (DJ Profile 1.0), the move from the old store, and the silent profile track.

fixtures/dj-profile is a copy of VJToolsLibrary's packages/creative-graph/fixtures/dj-profile: a stick in the
store used before 1.0 (DJPROFILE/VJTOOLS). Every stick a test writes must pass the reference checker
spec/djprofile/tools/check_djprofile.py with no problems.
"""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

from builder import dj_profile, service
from builder.jobs import Job
from builder.tests import pdb_fixture

FIXTURES = Path(__file__).parent / 'fixtures' / 'dj-profile'
ONELIBRARY = Path(__file__).parent / 'fixtures' / 'onelibrary'
CHECKER = Path(os.environ.get('DJPROFILE_CHECKER') or Path(__file__).resolve().parents[4] / 'spec/djprofile/tools/check_djprofile.py')


def _load_checker():
    if not CHECKER.is_file():
        raise RuntimeError(f'check_djprofile.py not found at {CHECKER}; set DJPROFILE_CHECKER')
    spec = importlib.util.spec_from_file_location('check_djprofile', CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


checker = _load_checker()


def call(method, **request):
    return service._dispatch({'method': method, **request}, Job())


def png(width, height, colour=(255, 40, 120, 200)):
    stream = io.BytesIO()
    Image.new('RGBA', (width, height), colour).save(stream, 'PNG')
    return stream.getvalue()


def jpeg(width, height, colour=(20, 200, 90)):
    stream = io.BytesIO()
    Image.new('RGB', (width, height), colour).save(stream, 'JPEG')
    return stream.getvalue()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def stick_files(volume):
    return {str(path.relative_to(volume)).replace('\\', '/'): sha(path.read_bytes())
            for path in Path(volume).rglob('*') if path.is_file() and path.name != dj_profile.LOCK}


def pdb_with_colour(colour_id, stale=()):
    """The smallest export.pdb whose live djdbProperty row carries COLORID at byte +9 (spec section 11).
    stale lists COLORIDs of superseded rows written first, present bit cleared, as rekordbox leaves them."""
    page_size, row_size = 4096, 40
    head = bytearray(page_size)
    struct.pack_into('<III', head, 0, 0, page_size, 1)
    struct.pack_into('<IIII', head, 0x1c, 19, 0, 1, 1)
    page = bytearray(page_size)
    struct.pack_into('<III', page, 4, 1, 19, 0)
    rows = [*stale, colour_id]
    for index, colour in enumerate(rows):
        start = 0x28 + index * row_size
        page[start:start + 2] = b'\x80\x02'
        struct.pack_into('<H', page, start + 2, index * 0x20)
        page[start + 9] = colour
        struct.pack_into('<H', page, page_size - 6 - 2 * index, index * row_size)
    struct.pack_into('<I', page, 0x18, len(rows))
    struct.pack_into('<H', page, page_size - 4, 1 << len(rows) - 1)
    return bytes(head + page)


class Stick(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.volume = self.root / 'usb'
        self.volume.mkdir()
        self.pc = self.root / 'pc'
        self.pc.mkdir()
        self.data = self.root / 'appdata'
        environment = patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.data)})
        environment.start()
        self.addCleanup(environment.stop)
        self.wide = self.pc / 'wide logo.png'
        self.wide.write_bytes(png(300, 120))
        self.square = self.pc / 'square.jpg'
        self.square.write_bytes(jpeg(64, 64))

    def save(self, expected_revision=None, make_active=False, logos=None, **fields):
        profile = {'dj_name': 'DJ Nova', 'tagline': 'Warehouse techno', 'accent': '#FF2A6D', 'aliases': ['NOVA'],
                   'links': [{'kind': 'website', 'url': 'https://nova.example/'},
                             {'kind': 'Media-Kit', 'url': 'https://nova.example/press.zip'},
                             {'kind': 'instagram', 'url': 'https://instagram.com/nova'}],
                   'page_url': None, 'note': 'Booking via agent'}
        profile.update(fields)
        if logos is None:
            logos = [{'path': str(self.wide), 'label': 'Main', 'note': ''}, {'path': str(self.square), 'label': '', 'note': 'square'}]
        return call('save_dj_profile', volume=str(self.volume), expected_revision=expected_revision, make_active=make_active,
                    profile=profile, logos=logos)

    def resave(self, result, view, **fields):
        """Saves a profile exactly as the editor would send it back from the read view, with fields changed."""
        profile = {key: view[key] for key in ('dj_id', 'dj_name', 'tagline', 'accent', 'aliases', 'links', 'page_url', 'note')}
        profile.update(fields)
        logos = [{'path': logo['path'], 'label': logo['label'], 'note': logo['note']} for logo in view['logos']]
        return call('save_dj_profile', volume=str(self.volume), expected_revision=result['revision'], make_active=False,
                    profile=profile, logos=logos)

    def assert_conforms(self):
        report = checker.check(self.volume)
        self.assertEqual(report.problems, [], 'check_djprofile.py reports no problems')
        return report

    def document(self, folder):
        return json.loads((self.volume / 'DJPROFILE' / folder / 'profile.json').read_text(encoding='utf-8'))

    def stick_file(self):
        return json.loads((self.volume / 'DJPROFILE/DJPROFILE.json').read_text(encoding='utf-8'))

    def listing(self, result):
        """What the migration must not change: names, ids, active, primary logo SHA-256, in order."""
        return [(p['dj_id'], p['dj_name'], p['active'], p['logos'][0]['sha256']) for p in result['profiles']]

    def old_stick(self):
        shutil.copytree(FIXTURES / 'stick-two-profiles', self.volume, dirs_exist_ok=True)


class OldStoreReadTests(Stick):
    def test_reads_the_shared_old_store_stick(self):
        self.old_stick()
        result = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual(result['format'], 'xz-dj-profiles/1')
        self.assertEqual(result['public_folder'], 'DJPROFILE')
        self.assertEqual(result['revision'], '2026-10-08T15-00-00Z-0001')
        self.assertEqual(result['active'], 'djp_lena')
        self.assertTrue(result['old_store'])
        self.assertEqual(result['problems'], [])
        self.assertEqual(result['summary'], {'count': 2, 'active_name': 'Lena B'})
        kai, lena = result['profiles']
        self.assertEqual((kai['dj_id'], kai['dj_name'], lena['dj_id'], lena['dj_name']), ('djp_kai', 'DJ Kai', 'djp_lena', 'Lena B'))
        self.assertEqual([p['active'] for p in result['profiles']], [False, True])
        self.assertEqual((kai['source'], kai['editable'], kai['folder']), ('old', True, None))
        self.assertEqual(kai['tagline'], 'Deep tech, Berlin')
        self.assertEqual(kai['accent'], '#ff2a6d')
        self.assertEqual(kai['aliases'], ['KAI', 'Kai Moreno'])
        self.assertEqual(kai['links'], [{'kind': 'page', 'url': 'https://vj.tools/dj#p1.fixture'},
                                        {'kind': 'website', 'url': 'https://example.com/kai'},
                                        {'kind': 'media-kit', 'url': 'https://example.com/kai/press.zip'}])
        self.assertIsNone(lena['accent'])
        logo = kai['logos'][0]
        self.assertEqual((logo['bytes'], logo['format'], logo['label']), (75, 'png', 'Preferred logo'))
        self.assertTrue(logo['preview'].startswith('data:image/png;base64,'))
        self.assertTrue(kai['tile']['preview'].startswith('data:image/png;base64,'))
        self.assertIsNone(kai['track'])

    def test_old_validators_agree_with_every_shared_case(self):
        cases = json.loads((FIXTURES / 'cases.json').read_text(encoding='utf-8'))
        for kind, validate in (('index', dj_profile.validate_old_index), ('profile', dj_profile.validate_old_document)):
            for case in cases[kind]:
                with self.subTest(kind=kind, case=case['name']):
                    if case['valid']:
                        validate(case['document'])
                    else:
                        with self.assertRaises(dj_profile.ProfileError):
                            validate(case['document'])

    def test_summary_counts_unreadable_old_profiles(self):
        self.assertEqual(dj_profile.summary(self.volume), {'count': 0, 'active_name': None})
        self.old_stick()
        shutil.rmtree(self.volume / 'DJPROFILE/VJTOOLS/profiles')
        self.assertEqual(dj_profile.summary(self.volume), {'count': 2, 'active_name': 'Lena B'})

    def test_reading_writes_nothing(self):
        self.old_stick()
        before = stick_files(self.volume)
        call('read_dj_profiles', volume=str(self.volume))
        call('dj_profile_track_status', volume=str(self.volume))
        self.assertEqual(stick_files(self.volume), before)


class SpecExampleTests(Stick):
    """The known-good sticks in spec/djprofile/examples, read here and edited without losing what another writer put there."""

    def example(self, name):
        shutil.copytree(CHECKER.parents[1] / 'examples' / name, self.volume, dirs_exist_ok=True)

    def test_every_example_reads_as_the_checker_reads_it(self):
        for name in ('level0', 'level1', 'level1-track'):
            with self.subTest(example=name):
                shutil.rmtree(self.volume)
                self.volume.mkdir()
                self.example(name)
                report = self.assert_conforms()
                result = call('read_dj_profiles', volume=str(self.volume))
                self.assertEqual([p['folder'] for p in result['profiles']], [p['folder'] for p in report.profiles])
                self.assertEqual(next(p['folder'] for p in result['profiles'] if p['active']), report.active)
                self.assertEqual([p['source'] for p in result['profiles']], ['1.0' if p['level'] else 'folder' for p in report.profiles])
                self.assertEqual(result['problems'], [])

    def test_editing_another_writers_profile_keeps_its_logos_and_extension(self):
        self.example('level1')
        before = call('read_dj_profiles', volume=str(self.volume))
        kai = before['profiles'][0]
        self.assertEqual([logo['background'] for logo in kai['logos']], ['dark', 'light'])
        self.resave(before, kai, tagline='Leeds and Sheffield')
        document = self.document('DJ Kai')
        self.assertEqual(document['ext'], {'example.org': {'note': 'readers ignore this, writers keep it'}})
        self.assertEqual([(e['file'], e['background']) for e in document['logos']], [('logo.png', 'dark'), ('logo-light.png', 'light')])
        self.assertTrue((self.volume / 'DJPROFILE/DJ Kai/logo-light.png').exists())
        self.assertEqual(self.stick_file()['profiles'], [{'folder': 'DJ Kai', 'id': kai['dj_id']}, {'folder': 'Lena B'}])
        self.assert_conforms()


class MigrationTests(Stick):
    def test_first_write_moves_the_whole_stick_behind_a_checked_backup(self):
        self.old_stick()
        home = self.volume / 'DJPROFILE/VJTOOLS'
        old_files = {str(p.relative_to(home)).replace('\\', '/'): sha(p.read_bytes()) for p in home.rglob('*') if p.is_file()}
        before = call('read_dj_profiles', volume=str(self.volume))
        checked = []
        real_delete = dj_profile._delete_tree

        def delete(path):
            backup, = (self.data / dj_profile.BACKUPS).iterdir()
            copied = {str(p.relative_to(backup / 'VJTOOLS')).replace('\\', '/'): sha(p.read_bytes())
                      for p in (backup / 'VJTOOLS').rglob('*') if p.is_file()}
            self.assertEqual(copied, old_files, 'the backup matches the stick before the old folder is deleted')
            self.assertEqual(Path(path), self.volume / 'DJPROFILE' / (dj_profile.REMOVING_PREFIX + backup.name),
                             'the old folder was first renamed to a dot name readers skip')
            self.assertFalse(home.exists())
            checked.append(path)
            return real_delete(path)

        with patch.object(dj_profile, '_delete_tree', delete):
            after = call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_lena', expected_revision=before['revision'])
        self.assertEqual(len(checked), 1)
        self.assertEqual(self.listing(after), self.listing(before), 'the DJ sees the same profiles after the move')
        self.assertEqual([p['source'] for p in after['profiles']], ['1.0', '1.0'])
        self.assertFalse(home.exists())
        self.assertFalse(after['old_store'])
        self.assertTrue(all('VJTOOLS' not in path for path in stick_files(self.volume)), 'the stick holds only the 1.0 layout')
        backup = Path(after['moved']['backup'])
        self.assertEqual(backup.parent, self.data / dj_profile.BACKUPS)
        record = json.loads((backup / 'backup.json').read_text(encoding='utf-8'))
        self.assertEqual({path: entry['sha256'] for path, entry in record['files'].items()}, old_files)
        self.assertEqual(after['moved']['profiles'], 2)
        self.assertFalse(after['moved']['old_kept'])
        stick = self.stick_file()
        self.assertEqual(stick['profiles'], [{'folder': 'DJ Kai', 'id': 'djp_kai'}, {'folder': 'Lena B', 'id': 'djp_lena'}])
        self.assertEqual(stick['active'], 'djp_lena')
        kai = self.document('DJ Kai')
        self.assertEqual(kai['revision'], '0b7c1f2e-0000-4000-8000-00000000c0de')
        self.assertEqual(kai['links'][0], {'kind': 'page', 'url': 'https://vj.tools/dj#p1.fixture'})
        self.assertEqual([e['file'] for e in kai['logos']], ['logo.png'])
        self.assertEqual(kai['renditions'][0]['file'], 'tile-160.rgb565')
        self.assertEqual(kai['renditions'][0]['for'], 'xdj-xz')
        self.assertEqual(sorted(p.name for p in (self.volume / 'DJPROFILE/DJ Kai').iterdir()), ['logo.png', 'profile.json', 'tile-160.rgb565'])
        report = self.assert_conforms()
        self.assertEqual([(p['folder'], p['level']) for p in report.profiles], [('DJ Kai', 1), ('Lena B', 1)])
        self.assertEqual(report.active, 'Lena B')
        self.assertFalse((self.volume / 'PIONEER/rekordbox/export.pdb.tmp').exists())

    def test_a_second_write_changes_nothing(self):
        self.old_stick()
        first = call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_lena',
                     expected_revision=call('read_dj_profiles', volume=str(self.volume))['revision'])
        layout = stick_files(self.volume)
        again = call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_lena', expected_revision=first['revision'])
        self.assertIsNone(again['moved'])
        kai = next(p for p in again['profiles'] if p['dj_id'] == 'djp_kai')
        saved = self.resave(again, kai)
        self.assertEqual(stick_files(self.volume), layout, 'the same profile saved again leaves every file as it was')
        self.assertEqual(saved['revision'], first['revision'])
        self.assertEqual(self.listing(saved), self.listing(first))

    def test_an_old_profile_whose_logo_does_not_check_stays_in_the_old_store(self):
        self.old_stick()
        logo = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_kai/bundles/0b7c1f2e-0000-4000-8000-00000000c0de/assets/001-logo.png'
        data = bytearray(logo.read_bytes())
        data[-20] ^= 0xFF
        logo.write_bytes(bytes(data))
        before = call('read_dj_profiles', volume=str(self.volume))
        result = self.save(expected_revision=before['revision'])
        self.assertTrue((self.volume / 'DJPROFILE/VJTOOLS/index.json').exists(), 'the old folder is kept')
        self.assertFalse((self.volume / 'DJPROFILE/DJ Kai').exists())
        self.assertTrue((self.volume / 'DJPROFILE/Lena B/profile.json').exists())
        self.assertTrue(result['moved']['old_kept'])
        self.assertIsNone(result['moved']['backup'])
        self.assertEqual([f['dj_id'] for f in result['moved']['failed']], ['djp_kai'])
        self.assertFalse((self.data / dj_profile.BACKUPS).exists() and any((self.data / dj_profile.BACKUPS).iterdir()))
        self.assertEqual([p['dj_name'] for p in result['profiles']], ['Lena B', 'DJ Nova'])
        self.assertEqual([p['dj_id'] for p in result['problems']], ['djp_kai'])
        self.assertEqual(self.stick_file()['active'], 'djp_lena')
        self.assert_conforms()

    def test_switching_active_while_the_old_store_remains_updates_its_index_too(self):
        self.old_stick()
        logo = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_lena/bundles/5e1a9d40-0000-4000-8000-0000000b2b00/assets/001-logo.png'
        logo.write_bytes(b'\x89PNG' + bytes(71))
        before = call('read_dj_profiles', volume=str(self.volume))
        result = call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_kai', expected_revision=before['revision'])
        self.assertTrue(result['moved']['old_kept'])
        self.assertEqual(self.stick_file()['active'], 'djp_kai')
        index = json.loads((self.volume / dj_profile.OLD_INDEX_PATH).read_text(encoding='utf-8'))
        self.assertEqual(index['active'], 'djp_kai', 'an XZ on an older runtime agrees')
        self.assertEqual(result['active'], 'djp_kai')

    def test_the_djs_hand_made_folder_is_adopted_and_keeps_their_logo(self):
        self.old_stick()
        hand = self.volume / 'DJPROFILE/DJ Kai'
        hand.mkdir()
        own = png(50, 50, (10, 10, 250, 255))
        (hand / 'logo.png').write_bytes(own)
        (hand / 'b.png').write_bytes(png(20, 20))
        before = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual([p['dj_name'] for p in before['profiles']], ['DJ Kai', 'Lena B'], 'listed once, from the old store')
        after = call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_lena', expected_revision=before['revision'])
        self.assertEqual(self.listing(after), self.listing(before))
        kai = self.document('DJ Kai')
        self.assertEqual([(e['file'], e['role']) for e in kai['logos']], [('logo.png', 'primary'), ('logo-original.png', 'alternate')])
        self.assertEqual((hand / 'logo-original.png').read_bytes(), own)
        self.assertEqual(sha((hand / 'logo.png').read_bytes()), before['profiles'][0]['logos'][0]['sha256'])
        self.assertTrue((hand / 'b.png').exists())
        self.assertFalse((self.volume / 'DJPROFILE/DJ Kai 2').exists())
        self.assert_conforms()

    def test_the_branding_card_is_moved_and_never_deleted(self):
        source = FIXTURES / 'stick-two-profiles/DJPROFILE/VJTOOLS/profiles/djp_kai'
        bundle = '0b7c1f2e-0000-4000-8000-00000000c0de'
        target = self.volume / 'VJ.Tools/Branding'
        shutil.copytree(source / 'bundles', target / 'bundles')
        document = json.loads((source / 'profile.json').read_text(encoding='utf-8'))
        text = json.dumps(document).replace(f'DJPROFILE/VJTOOLS/profiles/djp_kai/bundles/{bundle}', f'VJ.Tools/Branding/bundles/{bundle}')
        (target / 'profile.json').write_text(text, encoding='utf-8')
        card = stick_files(self.volume)
        before = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual([(p['dj_id'], p['source']) for p in before['profiles']], [('djp_kai', 'old')])
        after = self.save(expected_revision=before['revision'])
        self.assertTrue((self.volume / 'DJPROFILE/DJ Kai/profile.json').exists())
        self.assertTrue(all(stick_files(self.volume).get(path) == digest for path, digest in card.items()), 'the card is untouched')
        self.assertEqual([p['dj_name'] for p in after['profiles']], ['DJ Kai', 'DJ Nova'])
        layout = stick_files(self.volume)
        call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_kai', expected_revision=after['revision'])
        self.assertEqual(set(stick_files(self.volume)) - {'DJPROFILE/DJPROFILE.json'}, set(layout) - {'DJPROFILE/DJPROFILE.json'})
        self.assert_conforms()

    def test_unknown_old_members_go_to_ext(self):
        self.old_stick()
        path = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_kai/profile.json'
        document = json.loads(path.read_text(encoding='utf-8'))
        document['booking_region'] = {'country': 'DE'}
        path.write_text(json.dumps(document), encoding='utf-8')
        call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_lena',
             expected_revision=call('read_dj_profiles', volume=str(self.volume))['revision'])
        self.assertEqual(self.document('DJ Kai')['ext'], {'vj.tools': {'booking_region': {'country': 'DE'}}})
        self.assert_conforms()


    def old_track(self, comment):
        old = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_kai'
        track = dj_profile.id3_tag([('TIT2', b'\x03DJ Kai'), ('COMM', b'\x03eng\0' + comment.encode())]) + dj_profile.SILENT_FRAME * 40
        (old / 'track').mkdir()
        (old / 'track/DJ PROFILE - DJ Kai.mp3').write_bytes(track)
        document = json.loads((old / 'profile.json').read_text(encoding='utf-8'))
        document['profile_track'] = {'path': 'DJPROFILE/VJTOOLS/profiles/djp_kai/track/DJ PROFILE - DJ Kai.mp3',
                                     'bytes': len(track), 'sha256': sha(track)}
        (old / 'profile.json').write_text(json.dumps(document, indent=2), encoding='utf-8')
        return document['profile_track']

    def test_a_moved_track_names_only_the_marker_its_old_file_carries(self):
        self.old_stick()
        marker = 'djprofile:1:djp_kai:abcdef012345'
        self.old_track('8A ' + marker + ' https://vj.tools/dj#p1.fixture')
        before = call('read_dj_profiles', volume=str(self.volume))
        call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_kai', expected_revision=before['revision'])
        self.assertEqual(self.document('DJ Kai')['track'], {'marker': marker}, 'the marker the track carries, never a recomputed one')
        self.assertEqual([p.name for p in self.volume.rglob('*.mp3')], [], 'the old MP3 stays in the backup only')
        backup, = (self.data / dj_profile.BACKUPS).iterdir()
        self.assertTrue((backup / 'VJTOOLS/profiles/djp_kai/track/DJ PROFILE - DJ Kai.mp3').is_file())
        report = self.assert_conforms()
        self.assertTrue(any('made before the last edit' in w['problem'] for w in report.warnings))

    def test_a_track_whose_file_carries_no_marker_is_kept_in_ext(self):
        self.old_stick()
        record = self.old_track('no marker in here')
        call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_kai',
             expected_revision=call('read_dj_profiles', volume=str(self.volume))['revision'])
        document = self.document('DJ Kai')
        self.assertNotIn('track', document)
        self.assertEqual(json.loads(document['ext']['vj.tools']['profile_track']), record)
        self.assert_conforms()


class ReviewTests(Stick):
    """Data-loss cases from the review of the first 1.0 commit; each reproduces one probe."""

    def write(self, dj_id='djp_lena'):
        return call('set_active_dj_profile', volume=str(self.volume), dj_id=dj_id,
                    expected_revision=call('read_dj_profiles', volume=str(self.volume))['revision'])

    def add_old(self, dj_id, name):
        source = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_kai'
        target = self.volume / f'DJPROFILE/VJTOOLS/profiles/{dj_id}'
        shutil.copytree(source, target)
        document = json.loads((target / 'profile.json').read_text(encoding='utf-8').replace('djp_kai', dj_id))
        document['dj_name'] = name
        (target / 'profile.json').write_text(json.dumps(document), encoding='utf-8')
        path = self.volume / dj_profile.OLD_INDEX_PATH
        index = json.loads(path.read_text(encoding='utf-8'))
        index['profiles'].append({'dj_id': dj_id, 'dj_name': name})
        path.write_text(json.dumps(index), encoding='utf-8')

    def edit_old(self, dj_id, change):
        path = self.volume / f'DJPROFILE/VJTOOLS/profiles/{dj_id}/profile.json'
        document = json.loads(path.read_text(encoding='utf-8'))
        change(document)
        path.write_text(json.dumps(document, ensure_ascii=False), encoding='utf-8')
        return path

    def old_index(self):
        return json.loads((self.volume / dj_profile.OLD_INDEX_PATH).read_text(encoding='utf-8'))

    def names(self):
        return [p['dj_name'] for p in call('read_dj_profiles', volume=str(self.volume))['profiles']]

    def test_p10_an_older_xz_runtime_keeps_the_old_folder_and_its_active_in_step(self):
        self.old_stick()
        (self.volume / 'autoexec.bin').write_bytes(b'a loader this computer did not write')
        self.assertEqual(dj_profile.stick_runtime_capabilities(self.volume), set(), 'an unknown runtime counts as old')
        moved = self.write('djp_kai')['moved']
        self.assertEqual((moved['old_kept'], moved['reason'], moved['backup']), (True, 'runtime', None))
        self.assertTrue((self.volume / dj_profile.OLD_INDEX_PATH).exists())
        self.assertEqual(self.old_index()['active'], 'djp_kai', 'the older runtime shows the same DJ')
        result = call('read_dj_profiles', volume=str(self.volume))
        self.assertTrue(result['runtime_update_needed'])
        self.assertEqual([p['source'] for p in result['profiles']], ['1.0', '1.0'])
        self.write('djp_lena')
        self.assertEqual(self.old_index()['active'], 'djp_lena')
        result = call('read_dj_profiles', volume=str(self.volume))
        kai = next(p for p in result['profiles'] if p['dj_id'] == 'djp_kai')
        call('save_dj_profile', volume=str(self.volume), expected_revision=result['revision'], make_active=True,
             profile={key: kai[key] for key in ('dj_id', 'dj_name', 'tagline', 'accent', 'aliases', 'links', 'page_url', 'note')},
             logos=[{'path': logo['path'], 'label': logo['label'], 'note': logo['note']} for logo in kai['logos']])
        self.assertEqual(self.old_index()['active'], 'djp_kai', 'a save that makes a profile active syncs the old index too')
        self.assert_conforms()
        with patch.object(dj_profile, 'stick_runtime_capabilities', return_value={'djprofile/1'}):
            moved = self.write('djp_lena')['moved']
        self.assertFalse(moved['old_kept'])
        self.assertFalse((self.volume / 'DJPROFILE/VJTOOLS').exists(), 'once the runtime reads 1.0 the old folder goes')
        self.assertFalse(call('read_dj_profiles', volume=str(self.volume))['runtime_update_needed'])

    def test_runtime_capabilities_come_from_the_recorded_mod_version(self):
        self.assertIsNone(dj_profile.stick_runtime_capabilities(self.volume), 'no autoexec.bin: no XZ runtime')
        loader = b'a loader written by this computer'
        (self.volume / 'autoexec.bin').write_bytes(loader)
        version = 'a' * 64
        records = self.data / 'mod-versions'
        (records / 'loaders').mkdir(parents=True)
        (records / 'loaders' / (sha(loader) + '.json')).write_text(json.dumps({'loader_sha256': sha(loader), 'mod_version': {'id': version}}))
        manifest = records / version / 'runtime' / 'manifest.json'
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps({'firmware': 'XDJ-XZ 1.26'}))
        self.assertEqual(dj_profile.stick_runtime_capabilities(self.volume), set(), 'a runtime that lists no capabilities is old')
        manifest.write_text(json.dumps({'capabilities': ['djprofile/1', 'stems/4']}))
        self.assertEqual(dj_profile.stick_runtime_capabilities(self.volume), {'djprofile/1', 'stems/4'})

    def test_p3_names_that_differ_only_in_non_ascii_case_never_share_a_folder(self):
        first = self.save(dj_name='\u00c9mile')
        before = stick_files(self.volume)
        second = self.save(expected_revision=first['revision'], dj_name='\u00e9mile', logos=[{'path': str(self.square), 'label': '', 'note': ''}])
        self.assertNotEqual(first['saved'], second['saved'])
        self.assertTrue(all(stick_files(self.volume).get(path) == digest for path, digest in before.items()
                            if path.startswith('DJPROFILE/\u00c9mile/')), "the first DJ's folder is untouched")
        self.assertEqual([p['dj_name'] for p in second['profiles']], ['\u00c9mile', '\u00e9mile'])
        self.assertEqual(self.stick_file()['profiles'][1]['folder'], '\u00e9mile 2')
        self.assert_conforms()

    def test_p2_old_members_the_spec_json_rules_reject_go_to_ext_as_strings(self):
        self.old_stick()
        deep = {'a': {'b': {'c': {'d': {'e': {'f': {'g': 1}}}}}}}
        self.edit_old('djp_kai', lambda d: d.update(crop={'x': 0.5}, deep=deep, **{'\u043a\u043b\u044e\u0447': 1}))
        for _ in range(3):
            self.write()
        self.assertEqual(sorted(p.name for p in (self.volume / 'DJPROFILE').iterdir() if p.is_dir()), ['DJ Kai', 'Lena B'],
                         'the move happens once, not again on every write')
        vendor = self.document('DJ Kai')['ext']['vj.tools']
        self.assertEqual(json.loads(vendor['crop']), {'x': 0.5})
        self.assertEqual(json.loads(vendor['deep']), deep)
        self.assertEqual(vendor['\\u043a\\u043b\\u044e\\u0447'], 1)
        self.assert_conforms()
        with self.assertRaisesRegex(ValueError, 'whole number'):
            dj_profile.validate_profile({**self.document('DJ Kai'), 'crop': 0.5})

    def test_p4_a_read_only_file_in_the_old_folder_is_still_removed_and_later_writes_work(self):
        self.old_stick()
        manifest = next((self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_lena').rglob('manifest.json'))
        os.chmod(manifest, stat.S_IREAD)
        first = self.write('djp_kai')['moved']
        self.assertEqual((first['old_kept'], first['cleanup_pending']), (False, False))
        self.assertEqual(sorted(p.name for p in (self.volume / 'DJPROFILE').iterdir()), ['.xzmods-write.lock', 'DJ Kai', 'DJPROFILE.json', 'Lena B'])
        self.write('djp_lena')
        self.assertEqual(self.names(), ['DJ Kai', 'Lena B'])
        self.assert_conforms()

    def test_an_interrupted_delete_leaves_a_hidden_folder_the_next_write_finishes(self):
        self.old_stick()
        real = dj_profile._remove_path
        with patch.object(dj_profile, '_remove_path', lambda action, path: False if path.endswith('index.json') else real(action, path)):
            moved = self.write('djp_kai')['moved']
        self.assertTrue(moved['cleanup_pending'])
        left, = [p for p in (self.volume / 'DJPROFILE').iterdir() if p.name.startswith(dj_profile.REMOVING_PREFIX)]
        self.assertEqual(self.names(), ['DJ Kai', 'Lena B'], 'readers skip the dot-named leftover')
        self.assertFalse(call('read_dj_profiles', volume=str(self.volume))['old_store'])
        self.write('djp_lena')
        self.assertFalse(left.exists())
        self.assert_conforms()

    @unittest.skipUnless(os.name == 'nt', 'directory junctions are a Windows feature')
    def test_p12_a_junction_in_the_old_folder_keeps_it_and_what_it_points_to(self):
        self.old_stick()
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'precious.txt').write_text('mine', encoding='utf-8')
        junction = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_kai/linked'
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(outside)], check=True, capture_output=True)
        moved = self.write('djp_kai')['moved']
        self.assertEqual((moved['old_kept'], moved['reason']), (True, 'backup'))
        self.assertTrue((outside / 'precious.txt').exists())
        self.assertTrue((self.volume / dj_profile.OLD_INDEX_PATH).exists())

    def test_p6_old_text_over_the_byte_limits_is_cut_and_kept_in_ext(self):
        self.old_stick()
        long_name = '\u6771\u4eac' * 40
        kind = '\u0421\u0441\u044b\u043b\u043a\u0430 \u043d\u0430 \u043c\u043e\u0439 \u0441\u0430\u0439\u0442 \u0438 \u0432\u0441\u0435'

        def kai(document):
            document['dj_name'] = long_name
            document['links'] = [{'kind': 'website', 'url': f'https://e.example/{i}'} for i in range(12)]
            document['tagline'] = '\U0001f3a7' * 61
        self.edit_old('djp_kai', kai)
        index = self.old_index()
        index['profiles'][0]['dj_name'] = long_name
        (self.volume / dj_profile.OLD_INDEX_PATH).write_text(json.dumps(index, ensure_ascii=False), encoding='utf-8')
        self.edit_old('djp_lena', lambda d: d.update(links=[{'kind': kind, 'url': 'https://e.example/'}]))
        before = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual((len(before['profiles']), before['problems']), (2, []), 'no profile vanishes for a byte limit')
        self.write('djp_kai')
        folder = next(entry['folder'] for entry in self.stick_file()['profiles'] if entry['id'] == 'djp_kai')
        document = self.document(folder)
        vendor = document['ext']['vj.tools']
        self.assertEqual((vendor['dj_name'], vendor['tagline']), (long_name, '\U0001f3a7' * 61))
        self.assertTrue(long_name.startswith(document['name']) and len(document['name'].encode()) <= 160)
        self.assertEqual(json.loads(vendor['dropped_links']), [{'kind': 'website', 'url': 'https://e.example/11'}])
        self.assertEqual(self.document('Lena B')['ext']['vj.tools']['links[0].kind'], kind)
        self.assert_conforms()

    def test_p1_an_interrupted_move_keeps_the_order_the_stick_showed(self):
        self.old_stick()
        self.add_old('djp_zed', 'Zed')
        before = self.names()
        real, calls = dj_profile._migrate_one, []

        def flaky(*args):
            calls.append(1)
            if len(calls) == 2:
                raise KeyboardInterrupt('pulled')
            return real(*args)
        with patch.object(dj_profile, '_migrate_one', flaky), self.assertRaises(KeyboardInterrupt):
            self.write()
        self.assertEqual(self.names(), before)
        self.write()
        self.assertEqual(self.names(), before)
        self.assertEqual([entry['folder'] for entry in self.stick_file()['profiles']], before)
        self.assert_conforms()

    def test_p8_hand_made_folders_keep_their_place_and_nothing_visible_drops_out(self):
        self.old_stick()
        (self.volume / 'DJPROFILE/Aaron').mkdir()
        (self.volume / 'DJPROFILE/Aaron/logo.png').write_bytes(png(20, 20))
        before = self.names()
        self.assertEqual(before, ['Aaron', 'DJ Kai', 'Lena B'])
        self.write()
        self.assertEqual(self.names(), before)
        self.assert_conforms()
        shutil.rmtree(self.volume)
        self.volume.mkdir()
        self.old_stick()
        self.add_old('djp_zed', 'Zed')
        for i in range(14):
            (self.volume / f'DJPROFILE/Hand {i:02}').mkdir()
            (self.volume / f'DJPROFILE/Hand {i:02}/logo.png').write_bytes(png(20, 20, (i * 10, 0, 0, 255)))
        before = self.names()
        self.assertEqual(len(before), 16)
        self.write()
        self.assertEqual(self.names(), before)
        self.assert_conforms()

    def test_p11_a_newer_old_store_edit_after_the_move_is_moved_again_not_deleted(self):
        self.old_stick()
        with patch.object(dj_profile, '_remove_old_store', side_effect=KeyboardInterrupt), self.assertRaises(KeyboardInterrupt):
            self.write('djp_kai')
        moved_at = (self.volume / 'DJPROFILE/DJ Kai/profile.json').stat().st_mtime
        path = self.edit_old('djp_kai', lambda d: d.update(tagline='Edited in an older Library', revision='rev-newer'))
        os.utime(path, (moved_at + 10, moved_at + 10))
        self.write('djp_lena')
        self.assertEqual(self.document('DJ Kai')['tagline'], 'Edited in an older Library')
        self.assertEqual(self.document('DJ Kai')['revision'], 'rev-newer')
        self.assertTrue((self.volume / 'DJPROFILE/DJ Kai/profile.previous.json').exists())
        self.assertFalse((self.volume / 'DJPROFILE/VJTOOLS').exists())
        self.assert_conforms()

    def test_an_older_old_store_copy_does_not_undo_an_edit_made_after_the_move(self):
        self.old_stick()
        with patch.object(dj_profile, '_remove_old_store', side_effect=KeyboardInterrupt), self.assertRaises(KeyboardInterrupt):
            self.write('djp_kai')
        result = call('read_dj_profiles', volume=str(self.volume))
        kai = next(p for p in result['profiles'] if p['dj_id'] == 'djp_kai')
        old = self.volume / 'DJPROFILE/VJTOOLS/profiles/djp_kai/profile.json'
        os.utime(old, (1_000_000, 1_000_000))
        self.resave(result, kai, tagline='Edited in XZ Mods')
        self.assertEqual(self.document('DJ Kai')['tagline'], 'Edited in XZ Mods')
        self.assertFalse((self.volume / 'DJPROFILE/VJTOOLS').exists())

    def test_p13_a_move_stopped_after_the_logo_rename_still_lists_the_djs_logo(self):
        self.old_stick()
        hand = self.volume / 'DJPROFILE/DJ Kai'
        hand.mkdir()
        own = png(30, 30, (1, 2, 3, 255))
        (hand / 'logo.png').write_bytes(own)
        with patch.object(dj_profile, '_publish_document', side_effect=KeyboardInterrupt), self.assertRaises(KeyboardInterrupt):
            self.write('djp_kai')
        self.assertEqual((hand / 'logo-original.png').read_bytes(), own)
        self.write('djp_kai')
        logos = self.document('DJ Kai')['logos']
        self.assertIn(('logo-original.png', sha(own)), [(e['file'], e['sha256']) for e in logos])
        self.assert_conforms()

    def test_a_top_folder_rename_stopped_half_way_is_read_and_finished(self):
        self.save()
        hidden = self.volume / (dj_profile.TOP_RENAME_PREFIX + '0a1b2c3d')
        os.rename(self.volume / 'DJPROFILE', hidden)
        result = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual([p['dj_name'] for p in result['profiles']], ['DJ Nova'], 'the profiles are still found')
        call('set_active_dj_profile', volume=str(self.volume), dj_id=result['profiles'][0]['dj_id'], expected_revision=result['revision'])
        self.assertTrue((self.volume / 'DJPROFILE/DJ Nova/profile.json').exists())
        self.assertFalse(hidden.exists())
        self.assert_conforms()


class SaveTests(Stick):
    def test_first_save_writes_a_profile_folder_and_the_stick_file_last(self):
        written = []
        original = dj_profile._replace

        def record(destination, data):
            written.append(destination.name)
            return original(destination, data)

        with patch.object(dj_profile, '_replace', record):
            result = self.save()
        dj_id = result['saved']
        self.assertRegex(dj_id, r'^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$')
        self.assertEqual(written[-2:], ['profile.json', 'DJPROFILE.json'], 'assets first, the stick file last')
        self.assertLess(written.index('logo.png'), written.index('profile.json'))
        folder = self.volume / 'DJPROFILE/DJ Nova'
        document = self.document('DJ Nova')
        dj_profile.validate_profile(document)
        alternate = f'logo-{sha(self.square.read_bytes())[:8]}.jpg'
        self.assertEqual(sorted(p.name for p in folder.iterdir()), sorted([alternate, 'logo.png', 'profile.json', 'tile-160.rgb565']))
        self.assertEqual((folder / 'logo.png').read_bytes(), self.wide.read_bytes())
        self.assertEqual((folder / alternate).read_bytes(), self.square.read_bytes())
        self.assertEqual(document['logos'], [
            {'file': 'logo.png', 'bytes': self.wide.stat().st_size, 'sha256': sha(self.wide.read_bytes()), 'role': 'primary', 'background': 'any', 'label': 'Main'},
            {'file': alternate, 'bytes': self.square.stat().st_size, 'sha256': sha(self.square.read_bytes()), 'role': 'alternate', 'background': 'any', 'note': 'square'}])
        tile = (folder / 'tile-160.rgb565').read_bytes()
        self.assertEqual(len(tile), 51200)
        self.assertEqual(tile[:2], b'\0\0', 'letterbox rows are black')
        self.assertEqual(document['renditions'][0], {'file': 'tile-160.rgb565', 'bytes': 51200, 'sha256': sha(tile), 'width': 160,
                                                     'height': 160, 'format': 'rgb565le', 'for': 'xdj-xz'})
        self.assertEqual((document['name'], document['accent'], document['format']), ('DJ Nova', '#ff2a6d', 'djprofile/1'))
        self.assertRegex(document['revision'], r'^[0-9a-f]{64}$')
        self.assertFalse((folder / 'profile.previous.json').exists())
        stick = self.stick_file()
        self.assertEqual((stick['format'], stick['active'], stick['profiles']), ('djprofile-stick/1', dj_id, [{'folder': 'DJ Nova', 'id': dj_id}]))
        self.assertEqual(result['revision'], stick['revision'])
        nova = result['profiles'][0]
        self.assertEqual((nova['dj_name'], nova['active'], nova['source'], nova['folder'], len(nova['logos'])), ('DJ Nova', True, '1.0', 'DJ Nova', 2))
        self.assertIsNone(result['moved'])
        self.assertFalse((self.volume / dj_profile.LOCK).exists(), 'nothing is written outside DJPROFILE')
        self.assert_conforms()

    def test_edit_keeps_the_previous_document_and_deletes_only_what_it_listed(self):
        first = self.save()
        folder = self.volume / 'DJPROFILE/DJ Nova'
        first_document = (folder / 'profile.json').read_bytes()
        alternate = next(e['file'] for e in self.document('DJ Nova')['logos'] if e['role'] == 'alternate')
        (folder / 'notes.txt').write_text('mine', encoding='utf-8')
        second = self.save(expected_revision=first['revision'], dj_id=first['saved'], tagline='Now with modular',
                           logos=[{'path': str(self.wide), 'label': 'Main', 'note': ''}])
        self.assertEqual((folder / 'profile.previous.json').read_bytes(), first_document)
        self.assertFalse((folder / alternate).exists(), 'the dropped alternate is deleted')
        self.assertTrue((folder / 'notes.txt').exists(), 'files no document listed are kept')
        self.assertEqual(second['profiles'][0]['tagline'], 'Now with modular')
        self.assertEqual(second['revision'], first['revision'], 'the stick file did not change, so it was not rewritten')
        self.assert_conforms()
        with self.assertRaisesRegex(ValueError, 'changed since they were loaded'):
            self.save(expected_revision='2026-01-01T00-00-00Z-dead', dj_id=first['saved'])
        with self.assertRaisesRegex(ValueError, 'changed since they were loaded'):
            self.save(expected_revision=None, dj_name='Someone else')

    def test_a_changed_main_logo_is_replaced_in_place_and_the_old_one_can_become_an_alternate(self):
        first = self.save()
        folder = self.volume / 'DJPROFILE/DJ Nova'
        swapped = self.save(expected_revision=first['revision'], dj_id=first['saved'],
                            logos=[{'path': str(self.square), 'label': '', 'note': ''}, {'path': str(self.wide), 'label': 'Main', 'note': ''}])
        document = self.document('DJ Nova')
        self.assertEqual([e['file'] for e in document['logos']], ['logo.jpg', f'logo-{sha(self.wide.read_bytes())[:8]}.png'])
        self.assertFalse((folder / 'logo.png').exists(), 'logo.png was listed before and not now')
        self.assertNotEqual(document['renditions'][0]['file'], 'tile-160.rgb565', 'a changed tile gets a new name')
        self.assertFalse((folder / 'tile-160.rgb565').exists())
        self.assertEqual(swapped['profiles'][0]['logos'][0]['format'], 'jpeg')
        self.assert_conforms()

    def test_renaming_the_dj_renames_the_folder(self):
        first = self.save()
        renamed = self.save(expected_revision=first['revision'], dj_id=first['saved'], dj_name='Nova: Live?')
        self.assertFalse((self.volume / 'DJPROFILE/DJ Nova').exists())
        self.assertEqual(self.document('Nova Live')['name'], 'Nova: Live?')
        self.assertEqual(self.stick_file()['profiles'], [{'folder': 'Nova Live', 'id': first['saved']}])
        self.assertEqual(renamed['profiles'][0]['folder'], 'Nova Live')
        self.assert_conforms()

    def test_unknown_members_survive_an_edit(self):
        first = self.save()
        path = self.volume / 'DJPROFILE/DJ Nova/profile.json'
        document = json.loads(path.read_text(encoding='utf-8'))
        document['ext'] = {'example.org': {'visual_pack': 'visuals/pack.json'}}
        document['future_member'] = [1, 2]
        document['logos'][0]['made_with'] = 'pen'
        document['links'][1]['verified'] = True
        path.write_text(json.dumps(document, indent=2), encoding='utf-8')
        stick = self.stick_file()
        stick['written_by'] = 'another app'
        stick['profiles'][0]['colour'] = 'red'
        (self.volume / 'DJPROFILE/DJPROFILE.json').write_text(json.dumps(stick), encoding='utf-8')
        second = self.save(expected_revision=first['revision'], dj_id=first['saved'], tagline='Edited')
        edited = self.document('DJ Nova')
        self.assertEqual(edited['tagline'], 'Edited')
        self.assertEqual(edited['ext'], {'example.org': {'visual_pack': 'visuals/pack.json'}})
        self.assertEqual(edited['future_member'], [1, 2])
        self.assertEqual(edited['logos'][0]['made_with'], 'pen')
        self.assertTrue(edited['links'][1]['verified'])
        self.save(expected_revision=second['revision'], dj_id=first['saved'], dj_name='DJ Nova B', make_active=True)
        stick = self.stick_file()
        self.assertEqual((stick['written_by'], stick['profiles'][0]['colour'], stick['profiles'][0]['folder']), ('another app', 'red', 'DJ Nova B'))
        self.assert_conforms()

    def test_a_pulled_stick_recovers_through_profile_previous(self):
        first = self.save()
        second = self.save(expected_revision=first['revision'], dj_id=first['saved'], tagline='Second')
        folder = self.volume / 'DJPROFILE/DJ Nova'
        live = (folder / 'profile.json').read_bytes()
        (folder / 'profile.json').write_bytes(live[:len(live) // 2])
        (folder / '.profile-0123456789abcdef').write_bytes(live)
        recovered = call('read_dj_profiles', volume=str(self.volume))
        nova, = recovered['profiles']
        self.assertEqual((nova['dj_id'], nova['source'], nova['tagline']), (first['saved'], '1.0', 'Warehouse techno'))
        good_previous = (folder / 'profile.previous.json').read_bytes()
        self.resave(recovered, nova, tagline='Third')
        self.assertEqual((folder / 'profile.previous.json').read_bytes(), good_previous, 'a broken profile.json never replaces the good previous copy')
        self.assertEqual(self.document('DJ Nova')['tagline'], 'Third')
        self.assertIsNotNone(second)
        (folder / '.profile-0123456789abcdef').unlink()
        self.assert_conforms()

    def test_second_profile_is_not_made_active_unless_asked_and_set_active_switches(self):
        first = self.save()
        second = self.save(expected_revision=first['revision'], dj_name='Lena B')
        self.assertEqual(second['active'], first['saved'])
        lena = second['saved']
        switched = call('set_active_dj_profile', volume=str(self.volume), dj_id=lena, expected_revision=second['revision'])
        self.assertEqual(switched['active'], lena)
        self.assertNotEqual(switched['revision'], second['revision'])
        with self.assertRaisesRegex(ValueError, 'changed since they were loaded'):
            call('set_active_dj_profile', volume=str(self.volume), dj_id=first['saved'], expected_revision=second['revision'])
        third = self.save(expected_revision=switched['revision'], make_active=True, dj_name='Third')
        self.assertEqual(third['active'], third['saved'])
        self.assertEqual(self.stick_file()['active'], third['saved'])
        self.assertEqual([p['folder'] for p in self.stick_file()['profiles']], ['DJ Nova', 'Lena B', 'Third'])
        self.assert_conforms()

    def test_set_active_keeps_unknown_stick_file_members(self):
        first = self.save()
        second = self.save(expected_revision=first['revision'], dj_name='Lena B')
        path = self.volume / 'DJPROFILE/DJPROFILE.json'
        stick = json.loads(path.read_text(encoding='utf-8'))
        stick['written_by'] = 'xz'
        path.write_text(json.dumps(stick), encoding='utf-8')
        dj_profile.set_active(self.volume, second['saved'], second['revision'])
        self.assertEqual(json.loads(path.read_text(encoding='utf-8'))['written_by'], 'xz')

    def test_stick_colour_reads_the_live_row_not_a_stale_one(self):
        # The owner's XZME stick (2026-10-12): rows 0-2 superseded, row 3 live with COLORID 3 (Orange) at +9.
        (self.volume / 'PIONEER/rekordbox').mkdir(parents=True)
        path = self.volume / 'PIONEER/rekordbox/export.pdb'
        path.write_bytes(pdb_with_colour(3, stale=(0, 0, 3)))
        self.assertEqual(dj_profile.stick_colour(self.volume), '#ffa064')
        path.write_bytes(pdb_with_colour(0, stale=(7,)))
        self.assertIsNone(dj_profile.stick_colour(self.volume))
        path.write_bytes(pdb_with_colour(9))
        self.assertIsNone(dj_profile.stick_colour(self.volume))

    def test_a_hand_made_folder_is_a_profile_and_saving_it_adopts_the_folder(self):
        folder = self.volume / 'DJPROFILE' / 'DJ Folder'
        folder.mkdir(parents=True)
        own = png(40, 20)
        (folder / 'b.png').write_bytes(png(20, 20))
        (folder / 'LOGO.PNG').write_bytes(own)
        (self.volume / 'PIONEER/rekordbox').mkdir(parents=True)
        (self.volume / 'PIONEER/rekordbox/export.pdb').write_bytes(pdb_with_colour(6))
        result = call('read_dj_profiles', volume=str(self.volume))
        profile, = result['profiles']
        self.assertEqual((profile['source'], profile['editable'], profile['dj_name']), ('folder', False, 'DJ Folder'))
        self.assertEqual(profile['accent'], '#10e4dc')
        self.assertEqual(profile['logos'][0]['stick_path'], 'DJPROFILE/DJ Folder/LOGO.PNG')
        self.assertTrue(profile['active'])
        self.assertEqual(result['active'], profile['dj_id'])
        self.assertIsNone(result['revision'])
        self.assertEqual(result['summary'], {'count': 1, 'active_name': 'DJ Folder'})
        self.assert_conforms()
        saved = self.save(dj_id=profile['dj_id'], dj_name='DJ Folder', accent=profile['accent'], make_active=True)
        self.assertEqual([(p['dj_name'], p['source']) for p in saved['profiles']], [('DJ Folder', '1.0')])
        document = self.document('DJ Folder')
        self.assertEqual([e['file'] for e in document['logos']][::2], ['logo.png', 'logo-original.png'])
        self.assertEqual((folder / 'logo-original.png').read_bytes(), own, "the DJ's own logo is kept")
        self.assertEqual((folder / 'logo.png').read_bytes(), self.wide.read_bytes())
        self.assertTrue((folder / 'b.png').exists())
        self.assertEqual(self.stick_file()['profiles'], [{'folder': 'DJ Folder', 'id': saved['saved']}])
        self.assert_conforms()

    def test_saving_a_hand_made_folder_with_its_own_logo_keeps_it_as_the_main_logo(self):
        folder = self.volume / 'DJPROFILE' / 'Mara'
        folder.mkdir(parents=True)
        (folder / 'logo.png').write_bytes(png(30, 30))
        profile, = call('read_dj_profiles', volume=str(self.volume))['profiles']
        self.save(dj_id=profile['dj_id'], dj_name='Mara', logos=[{'path': profile['logos'][0]['path'], 'label': '', 'note': ''}])
        self.assertEqual([e['file'] for e in self.document('Mara')['logos']], ['logo.png'])
        self.assertFalse(any(p.name.startswith('logo-original') for p in folder.iterdir()))
        self.assert_conforms()

    def test_folders_readers_skip_and_level_0_names(self):
        (self.volume / 'DJPROFILES/Lena').mkdir(parents=True)
        (self.volume / 'DJPROFILES/Lena/zz.jpg').write_bytes(jpeg(40, 40))
        (self.volume / 'DJPROFILE/.hidden').mkdir(parents=True)
        (self.volume / 'DJPROFILE/.hidden/logo.png').write_bytes(png(10, 10))
        (self.volume / 'DJPROFILE/Lena').mkdir()
        (self.volume / 'DJPROFILE/Lena/._logo.png').write_bytes(b'\0' * 40)
        (self.volume / 'DJPROFILE/Beyoncé').mkdir()
        (self.volume / 'DJPROFILE/Beyoncé/logo.png').write_bytes(png(10, 10))
        (self.volume / 'DJPROFILE/Big').mkdir()
        (self.volume / 'DJPROFILE/Big/logo.png').write_bytes(png(4097, 10))
        result = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual([p['dj_name'] for p in result['profiles']], ['Beyoncé'])
        self.assertEqual([p['dj_name'] for p in result['problems']], ['Big'])

    def test_a_large_logo_is_scaled_to_the_stick_limits(self):
        large = self.pc / 'large.png'
        large.write_bytes(png(5000, 1000))
        self.save(logos=[{'path': str(large), 'label': '', 'note': ''}])
        with Image.open(self.volume / 'DJPROFILE/DJ Nova/logo.png') as image:
            self.assertEqual(image.size, (4096, 819))
        self.assert_conforms()

    def test_limits_are_refused_in_plain_words(self):
        with self.assertRaisesRegex(ValueError, 'at most 8'):
            call('save_dj_profile', volume=str(self.volume), expected_revision=None, profile={'dj_name': 'X'},
                 logos=[{'path': str(self.wide)}] * 9)
        with self.assertRaisesRegex(ValueError, 'https://'):
            self.save(links=[{'kind': 'website', 'url': 'http://nova.example/'}])
        with self.assertRaisesRegex(ValueError, 'Add a logo'):
            self.save(logos=[])
        with self.assertRaisesRegex(ValueError, '160 bytes'):
            self.save(dj_name='é' * 81)
        huge = self.pc / 'huge.png'
        huge.write_bytes(dj_profile.PNG_MAGIC + bytes(9 * 1024 * 1024))
        with self.assertRaisesRegex(ValueError, 'larger than 8 MB'):
            call('read_logo', path=str(huge))
        with self.assertRaisesRegex(ValueError, 'larger than 8 MB'):
            call('save_dj_profile', volume=str(self.volume), expected_revision=None, profile={'dj_name': 'X'}, logos=[{'path': str(huge)}])
        text = self.pc / 'notes.png'
        text.write_bytes(b'not an image at all')
        with self.assertRaisesRegex(ValueError, 'PNG or JPEG'):
            call('read_logo', path=str(text))
        with self.assertRaisesRegex(ValueError, '#rrggbb'):
            self.save(accent='red')
        self.assertFalse((self.volume / 'DJPROFILE').exists(), 'refused saves write nothing')

    def test_read_logo_reports_size_format_and_preview(self):
        logo = call('read_logo', path=str(self.wide))
        self.assertEqual((logo['width'], logo['height'], logo['format'], logo['bytes']), (300, 120, 'png', self.wide.stat().st_size))
        preview = Image.open(io.BytesIO(__import__('base64').b64decode(logo['preview'].split(',', 1)[1])))
        self.assertEqual(preview.size, (256, 102))

    def test_previews_fit_the_host_result_line(self):
        noisy = self.pc / 'noise.png'
        Image.frombytes('RGB', (600, 600), os.urandom(600 * 600 * 3)).save(noisy)
        revision = None
        for n in range(6):
            result = call('save_dj_profile', volume=str(self.volume), expected_revision=revision, profile={'dj_name': f'DJ {n}'},
                          logos=[{'path': str(noisy)}, {'path': str(self.wide)}])
            revision = result['revision']
        line = json.dumps({'event': 'result', 'ok': True, 'result': call('read_dj_profiles', volume=str(self.volume))})
        self.assertLess(len(line), 65536)


class TrackTests(Stick):
    def frames(self, path):
        data = Path(path).read_bytes()
        frames, offset = dj_profile.read_id3(data)
        return [(frame_id, dj_profile.decode_frame(frame_id, payload)) for frame_id, payload in frames], data, offset

    def test_usb_track_follows_the_spec_and_is_silent(self):
        saved = self.save(page_url='https://vj.tools/dj#p1.nova')
        dj_id = saved['saved']
        result = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='usb')
        path = Path(result['path'])
        self.assertEqual(path, self.volume / 'DJPROFILE/DJ Nova/DJ PROFILE - DJ Nova.mp3')
        self.assertEqual((result['logos_embedded'], result['logos_skipped'], result['verified_silent']), (2, [], True))
        self.assertAlmostEqual(result['duration_seconds'], 10, delta=0.1)
        self.assertFalse((self.volume / 'PIONEER').exists())
        frames, data, offset = self.frames(path)
        by_id = {}
        for frame_id, value in frames:
            by_id.setdefault(frame_id, []).append(value)
        for frame_id in ('TIT2', 'TPE1', 'TPE2'):
            self.assertEqual(by_id[frame_id], ['DJ Nova'])
        self.assertEqual((by_id['TALB'], by_id['TCON'], by_id['TPUB']), (['DJ PROFILE'], ['DJ Profile'], ['Warehouse techno']))
        document = self.document('DJ Nova')
        payload, = [t['value'] for t in by_id['TXXX'] if t['description'] == 'DJPROFILE']
        without_track = {k: v for k, v in document.items() if k != 'track'}
        self.assertEqual(payload.encode(), json.dumps(without_track, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode())
        comment, = by_id['COMM']
        marker = f'djprofile:1:{dj_id}:{sha(payload.encode())[:12]}'
        self.assertEqual(comment, {'language': 'eng', 'description': '', 'text': marker + ' https://vj.tools/dj#p1.nova'})
        self.assertEqual(by_id['WOAR'], ['https://vj.tools/dj#p1.nova'])
        self.assertEqual(by_id['WXXX'], [{'description': 'EPK', 'url': 'https://nova.example/press.zip'},
                                         {'description': 'website', 'url': 'https://nova.example/'},
                                         {'description': 'instagram', 'url': 'https://instagram.com/nova'}])
        cover, main, alternate = by_id['APIC']
        self.assertEqual((cover['type'], cover['mime']), (3, 'image/jpeg'))
        info = dj_profile.image_info(cover['data'])
        self.assertEqual(info, ('jpeg', 300, 300, True), 'a square baseline JPEG, 240 to 800 px')
        with Image.open(io.BytesIO(cover['data'])) as image:
            self.assertLess(sum(image.convert('RGB').getpixel((2, 2))), 30, 'padded black for a dark or any background')
        self.assertEqual((main['type'], main['data'], main['description']), (0x13, self.wide.read_bytes(), 'Main'))
        self.assertEqual((alternate['type'], alternate['data'], alternate['description']), (0x13, self.square.read_bytes(), 'Alternate 1'))
        self.assertEqual(document['track'], {'file': 'DJ PROFILE - DJ Nova.mp3', 'bytes': len(data), 'sha256': sha(data), 'marker': marker})
        reread = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual(reread['profiles'][0]['track']['bytes'], len(data))
        self.assert_conforms()
        dj_profile.check_silent_audio(data, offset)
        decoded = dj_profile.ffmpeg_silence(path)
        if decoded is None:
            self.skipTest('ffmpeg is not on PATH; the decode proof did not run')
        silent, seconds = decoded
        self.assertTrue(silent, 'ffmpeg decoded only zero samples')
        self.assertAlmostEqual(seconds, 10, delta=0.5)

    def test_a_light_logo_cover_is_padded_white(self):
        cover = dj_profile.cover_jpeg(png(100, 40, (0, 0, 0, 0)), 'light')
        with Image.open(io.BytesIO(cover)) as image:
            self.assertEqual(image.size, (240, 240))
            self.assertGreater(sum(image.convert('RGB').getpixel((2, 2))), 720)
        big = dj_profile.cover_jpeg(png(1200, 300), 'dark')
        self.assertEqual(dj_profile.image_info(big)[1:], (800, 800, True))

    def test_folder_track_and_usb_copy_survive_a_resave_of_the_same_content(self):
        saved = self.save()
        dj_id = saved['saved']
        result = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='folder', folder=str(self.pc / 'out'))
        self.assertEqual(Path(result['path']).parent, self.pc / 'out')
        frames, _, _ = self.frames(result['path'])
        values = dict((frame_id, value) for frame_id, value in frames if frame_id in ('WOAR', 'COMM'))
        self.assertEqual(values['WOAR'], 'https://nova.example/')
        self.assertTrue(values['COMM']['text'].endswith(' https://nova.example/press.zip'), 'EPK link follows the marker')
        self.assertFalse((self.volume / 'DJPROFILE/DJ Nova/DJ PROFILE - DJ Nova.mp3').exists())
        usb = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='usb')
        reread = call('read_dj_profiles', volume=str(self.volume))
        again = self.resave(reread, reread['profiles'][0])
        self.assertEqual(again['profiles'][0]['track']['path'], usb['path'], 'unchanged content keeps its track')
        edited = self.resave(again, again['profiles'][0], tagline='New tagline')
        self.assertIsNone(edited['profiles'][0]['track'], 'an edit drops the track made before it')
        self.assertFalse(Path(usb['path']).exists())
        self.assert_conforms()

    def test_structural_check_catches_a_noisy_frame(self):
        saved = self.save()
        path = Path(call('write_profile_track', volume=str(self.volume), dj_id=saved['saved'], destination='folder',
                         folder=str(self.pc))['path'])
        data = bytearray(path.read_bytes())
        _, offset = dj_profile.read_id3(bytes(data))
        data[offset + 5 * dj_profile.FRAME_BYTES + 40] = 0x55
        with self.assertRaisesRegex(ValueError, 'frame 5 is not silent'):
            dj_profile.check_silent_audio(bytes(data), offset)
        header = bytearray(path.read_bytes())
        header[offset + 3 * dj_profile.FRAME_BYTES + 2] = 0xA0
        with self.assertRaisesRegex(ValueError, 'frame 3'):
            dj_profile.check_silent_audio(bytes(header), offset)

    def test_track_name_strips_characters_fat32_forbids(self):
        self.assertEqual(dj_profile.track_name('A/B: "C"?'), 'DJ PROFILE - AB C.mp3')
        self.assertEqual(dj_profile.track_name('???'), 'DJ PROFILE.mp3')

    def test_oversized_logos_are_skipped_and_reported(self):
        self.save()
        document = self.document('DJ Nova')
        with patch.object(dj_profile, 'MAX_TAG_BYTES', 30000):
            frames, embedded, skipped = dj_profile.track_frames(document, [
                {'data': b'x' * 1000, 'mime': 'image/png', 'label': 'small'},
                {'data': b'x' * 30000, 'mime': 'image/png', 'label': 'big'}], jpeg(240, 240))
        self.assertEqual((embedded, skipped), (1, ['big']))


class TrackStatusTests(Stick):
    """profile_track_status reads export.pdb and OneLibrary and never writes to the stick."""

    def two_profile_stick(self, onelibrary_fixture='wal-profile'):
        self.old_stick()
        if onelibrary_fixture:
            source = ONELIBRARY / onelibrary_fixture
            for name in ('exportLibrary.db', 'exportLibrary.db-wal'):
                if (source / name).exists():
                    shutil.copyfile(source / name, self.volume / 'PIONEER/rekordbox' / name)

    def export(self, comments, playlists):
        tracks = {i: f'/Contents/DJ/Track {i}.mp3' for i in range(1, 6)}
        pdb_fixture.write_export(self.volume / 'PIONEER/rekordbox/export.pdb', tracks, comments=comments, playlists=playlists)

    def status(self):
        before = stick_files(self.volume)
        result = call('dj_profile_track_status', volume=str(self.volume))
        self.assertEqual(stick_files(self.volume), before, 'the status check writes nothing')
        self.assertEqual(result['format'], 'xz-dj-profile-track-status/1')
        return result, {profile['dj_id']: profile for profile in result['profiles']}

    def marker(self, dj_id):
        return next(dj_profile.track_marker(p.document) for p in dj_profile._scan(self.volume).profiles if p.id == dj_id)

    def test_found_in_both_libraries_and_stale_elsewhere(self):
        self.two_profile_stick()
        kai = self.marker('djp_kai')
        self.assertTrue(kai.startswith('djprofile:1:djp_kai:'))
        self.export({1: kai + ' https://vj.tools/dj#p1.fixture', 2: 'djprofile:1:djp_lena:000000000000',
                     3: kai + 'x', 4: 'djprofile:1:djp_kai_2:' + kai[-12:]},
                    [(1, 0, 'Sets', True, []), (2, 1, 'dj profile ', False, [1, 2]), (3, 0, 'Friday', False, [1])])
        result, profiles = self.status()
        self.assertEqual(result['libraries'], {'device_library': {'state': 'present', 'error': None},
                                               'onelibrary': {'state': 'present', 'error': None}})
        self.assertEqual(profiles['djp_kai']['device_library'], {'library': 'present', 'track': 'found', 'in_profile_playlist': True,
                                                                 'playlists': ['Friday', 'dj profile ']})
        self.assertEqual(profiles['djp_kai']['onelibrary'], {'library': 'present', 'track': 'found', 'in_profile_playlist': True,
                                                             'playlists': ['DJ PROFILE']})
        self.assertEqual(profiles['djp_lena']['device_library']['track'], 'stale')
        self.assertEqual(profiles['djp_lena']['onelibrary'], {'library': 'present', 'track': 'stale', 'in_profile_playlist': False,
                                                              'playlists': ['Friday']})
        self.assertEqual((profiles['djp_kai']['dj_name'], profiles['djp_kai']['marker']), ('DJ Kai', kai))

    def test_the_marker_is_the_same_before_and_after_the_move(self):
        self.two_profile_stick(onelibrary_fixture=None)
        before = self.marker('djp_kai')
        call('set_active_dj_profile', volume=str(self.volume), dj_id='djp_lena',
             expected_revision=call('read_dj_profiles', volume=str(self.volume))['revision'])
        self.assertEqual(self.marker('djp_kai'), before)
        self.export({1: 'djprofile:1:djp_kai:21f157d13096'}, [(1, 0, 'DJ PROFILE', False, [1])])
        _, profiles = self.status()
        self.assertEqual(profiles['djp_kai']['device_library']['track'], 'stale', 'a track made before 1.0 shows as stale')

    def test_missing_track_absent_onelibrary_and_no_playlist(self):
        self.two_profile_stick(onelibrary_fixture=None)
        self.export({5: self.marker('djp_lena')}, [(1, 0, 'Friday', False, [5])])
        result, profiles = self.status()
        self.assertEqual(result['libraries']['onelibrary'], {'state': 'absent', 'error': None})
        self.assertEqual(profiles['djp_kai']['device_library'], {'library': 'present', 'track': 'missing', 'in_profile_playlist': False,
                                                                 'playlists': []})
        self.assertEqual(profiles['djp_kai']['onelibrary'], {'library': 'absent', 'track': None, 'in_profile_playlist': False,
                                                             'playlists': []})
        self.assertEqual(profiles['djp_lena']['device_library'], {'library': 'present', 'track': 'found', 'in_profile_playlist': False,
                                                                  'playlists': ['Friday']})

    def test_a_onelibrary_without_markers_reports_missing(self):
        self.two_profile_stick(onelibrary_fixture='rekordbox7-sample')
        self.export({}, [])
        _, profiles = self.status()
        self.assertEqual(profiles['djp_kai']['onelibrary']['track'], 'missing')

    def test_unreadable_libraries_are_reported_separately(self):
        self.two_profile_stick()
        database = self.volume / 'PIONEER/rekordbox/exportLibrary.db'
        damaged = bytearray(database.read_bytes())
        damaged[3 * 4096 + 50] ^= 0xFF
        database.write_bytes(damaged)
        result, profiles = self.status()
        self.assertEqual(result['libraries']['device_library']['state'], 'unreadable')
        self.assertIn('library exported to this USB is unreadable', result['libraries']['device_library']['error'])
        self.assertEqual(result['libraries']['onelibrary']['state'], 'unreadable')
        self.assertIn('page 4 failed its check', result['libraries']['onelibrary']['error'])
        self.assertEqual(profiles['djp_kai']['onelibrary']['library'], 'unreadable')
        self.assertIsNone(profiles['djp_kai']['onelibrary']['track'])

    def test_the_written_track_comment_matches_until_the_profile_is_edited(self):
        saved = self.save()
        dj_id = saved['saved']
        path = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='folder', folder=str(self.pc))['path']
        frames, _ = dj_profile.read_id3(Path(path).read_bytes())
        comment, = [dj_profile.decode_frame(frame_id, payload)['text'] for frame_id, payload in frames if frame_id == 'COMM']
        self.export({1: comment}, [(1, 0, 'DJ PROFILE', False, [1])])
        _, profiles = self.status()
        self.assertEqual(profiles[dj_id]['device_library']['track'], 'found')
        self.assertTrue(profiles[dj_id]['device_library']['in_profile_playlist'])
        self.save(expected_revision=saved['revision'], dj_id=dj_id, tagline='Now in Lisbon')
        _, profiles = self.status()
        self.assertEqual(profiles[dj_id]['device_library']['track'], 'stale')

    def test_marker_after_text_another_tool_prepended(self):
        self.two_profile_stick(onelibrary_fixture=None)
        self.export({1: '8A - Energy 7 ' + self.marker('djp_kai'), 2: 'xdjprofile:1:djp_lena:' + self.marker('djp_lena')[-12:]},
                    [(1, 0, 'DJ PROFILE', False, [1, 2])])
        _, profiles = self.status()
        self.assertEqual(profiles['djp_kai']['device_library']['track'], 'found')
        self.assertEqual(profiles['djp_lena']['device_library']['track'], 'missing')

    def test_a_stick_without_rekordbox(self):
        self.save()
        result, profiles = self.status()
        self.assertEqual({name: library['state'] for name, library in result['libraries'].items()},
                         {'device_library': 'absent', 'onelibrary': 'absent'})
        self.assertEqual(len(profiles), 1)


if __name__ == '__main__':
    unittest.main()
