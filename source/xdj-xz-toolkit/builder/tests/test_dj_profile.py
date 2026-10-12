"""DJ profiles on the USB and the silent profile track.

fixtures/dj-profile is a copy of VJToolsLibrary's packages/creative-graph/fixtures/dj-profile,
which the Rust, TypeScript and XZ readers are tested on too.
"""
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

from builder import dj_profile, service
from builder.jobs import Job

FIXTURES = Path(__file__).parent / 'fixtures' / 'dj-profile'


def call(method, **request):
    return service._dispatch({'method': method, **request}, Job())


def png(width, height, colour=(255, 40, 120, 200)):
    stream = io.BytesIO()
    Image.new('RGBA', (width, height), colour).save(stream, 'PNG')
    return stream.getvalue()


def jpeg(width, height):
    stream = io.BytesIO()
    Image.new('RGB', (width, height), (20, 200, 90)).save(stream, 'JPEG')
    return stream.getvalue()


def stick_files(volume):
    return {str(path.relative_to(volume)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in Path(volume).rglob('*') if path.is_file() and path.name != dj_profile.LOCK}


def pdb_with_colour(colour_id, stale=()):
    """The smallest export.pdb whose live djdbProperty row carries COLORID at byte +9 (standard §1.2).
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
        self.wide = self.pc / 'wide logo.png'
        self.wide.write_bytes(png(300, 120))
        self.square = self.pc / 'square.jpg'
        self.square.write_bytes(jpeg(64, 64))

    def save(self, expected_revision=None, make_active=False, **fields):
        profile = {'dj_name': 'DJ Nova', 'tagline': 'Warehouse techno', 'accent': '#FF2A6D', 'aliases': ['NOVA'],
                   'links': [{'kind': 'website', 'url': 'https://nova.example/'},
                             {'kind': 'Media-Kit', 'url': 'https://nova.example/press.zip'},
                             {'kind': 'instagram', 'url': 'https://instagram.com/nova'}],
                   'page_url': None, 'note': 'Booking via agent'}
        profile.update(fields)
        return call('save_dj_profile', volume=str(self.volume), expected_revision=expected_revision, make_active=make_active,
                    profile=profile, logos=[{'path': str(self.wide), 'label': 'Main', 'note': ''},
                                            {'path': str(self.square), 'label': '', 'note': 'square'}])


class FixtureStickTests(Stick):
    def test_reads_the_shared_two_profile_stick(self):
        shutil.copytree(FIXTURES / 'stick-two-profiles', self.volume, dirs_exist_ok=True)
        result = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual(result['format'], 'xz-dj-profiles/1')
        self.assertEqual(result['public_folder'], 'DJPROFILE')
        self.assertEqual(result['revision'], '2026-10-08T15-00-00Z-0001')
        self.assertEqual(result['active'], 'djp_lena')
        self.assertFalse(result['legacy'])
        self.assertEqual(result['problems'], [])
        kai, lena = result['profiles']
        self.assertEqual((kai['dj_id'], kai['dj_name'], lena['dj_id'], lena['dj_name']), ('djp_kai', 'DJ Kai', 'djp_lena', 'Lena B'))
        self.assertEqual([p['active'] for p in result['profiles']], [False, True])
        self.assertEqual((kai['source'], kai['editable']), ('vjtools', True))
        self.assertEqual(kai['tagline'], 'Deep tech, Berlin')
        self.assertEqual(kai['accent'], '#ff2a6d')
        self.assertEqual(kai['aliases'], ['KAI', 'Kai Moreno'])
        self.assertEqual(kai['links'][1], {'kind': 'media-kit', 'url': 'https://example.com/kai/press.zip'})
        self.assertEqual(kai['page_url'], 'https://vj.tools/dj#p1.fixture')
        self.assertIsNone(lena['accent'])
        logo = kai['logos'][0]
        self.assertEqual((logo['bytes'], logo['format'], logo['label']), (75, 'png', 'Preferred logo'))
        self.assertTrue(logo['preview'].startswith('data:image/png;base64,'))
        self.assertTrue(kai['tile']['preview'].startswith('data:image/png;base64,'))
        self.assertIsNone(kai['track'])

    def test_validators_agree_with_every_shared_case(self):
        cases = json.loads((FIXTURES / 'cases.json').read_text(encoding='utf-8'))
        for kind, validate in (('index', dj_profile.validate_index), ('profile', dj_profile.validate_document)):
            for case in cases[kind]:
                with self.subTest(kind=kind, case=case['name']):
                    if case['valid']:
                        validate(case['document'])
                    else:
                        with self.assertRaises(dj_profile.ProfileError):
                            validate(case['document'])

    def test_summary_reads_only_the_index(self):
        self.assertEqual(dj_profile.summary(self.volume), {'count': 0, 'active_name': None})
        shutil.copytree(FIXTURES / 'stick-two-profiles', self.volume, dirs_exist_ok=True)
        shutil.rmtree(self.volume / 'DJPROFILE/VJTOOLS/profiles')
        self.assertEqual(dj_profile.summary(self.volume), {'count': 2, 'active_name': 'Lena B'})


class SaveTests(Stick):
    def test_first_save_writes_the_standard_layout_index_last(self):
        published = []
        original = dj_profile._publish

        def record(root, relative, data):
            published.append((relative, (Path(root) / dj_profile.HOME / 'profiles').exists()))
            return original(root, relative, data)

        with patch.object(dj_profile, '_publish', record):
            result = self.save()
        dj_id = result['saved']
        self.assertRegex(dj_id, r'^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$')
        self.assertEqual([relative for relative, _ in published], [f'DJPROFILE/VJTOOLS/profiles/{dj_id}/profile.json', dj_profile.INDEX_PATH])
        home = self.volume / 'DJPROFILE/VJTOOLS'
        index = json.loads((home / 'index.json').read_text(encoding='utf-8'))
        self.assertEqual((index['active'], index['profiles']), (dj_id, [{'dj_id': dj_id, 'dj_name': 'DJ Nova'}]))
        document = json.loads((home / f'profiles/{dj_id}/profile.json').read_text(encoding='utf-8'))
        dj_profile.validate_document(document)
        dj_profile.validate_index(index)
        bundle = self.volume / document['bundle']
        self.assertEqual(sorted(p.name for p in (bundle / 'assets').iterdir()), ['001-Main.png', '002-Alternate_1.jpg'])
        self.assertEqual((bundle / 'assets/001-Main.png').read_bytes(), self.wide.read_bytes())
        self.assertEqual((bundle / 'assets/002-Alternate_1.jpg').read_bytes(), self.square.read_bytes())
        tile = (bundle / 'renditions/xz-tile-160.rgb565').read_bytes()
        self.assertEqual(len(tile), 51200)
        self.assertEqual(document['xz_tile']['sha256'], hashlib.sha256(tile).hexdigest())
        self.assertEqual(tile[:2], b'\0\0', 'letterbox rows are black')
        manifest = json.loads((bundle / 'manifest.json').read_text(encoding='utf-8'))
        for entry in manifest['files']:
            self.assertEqual(hashlib.sha256((bundle / entry['path']).read_bytes()).hexdigest(), entry['sha256'])
        self.assertEqual(document['accent'], '#ff2a6d')
        self.assertEqual(document['secondary_logos'][0]['note'], 'square')
        self.assertFalse((home / f'profiles/{dj_id}/profile.previous.json').exists())
        nova = result['profiles'][0]
        self.assertEqual((nova['dj_name'], nova['active'], len(nova['logos'])), ('DJ Nova', True, 2))
        self.assertEqual(result['revision'], index['revision'])

    def test_edit_keeps_old_bundle_and_previous_pointer_and_refuses_stale_revision(self):
        first = self.save()
        dj_id = first['saved']
        home = self.volume / f'DJPROFILE/VJTOOLS/profiles/{dj_id}'
        first_pointer = (home / 'profile.json').read_bytes()
        before = stick_files(self.volume)
        second = self.save(expected_revision=first['revision'], dj_id=dj_id, tagline='Now with modular')
        after = stick_files(self.volume)
        self.assertTrue(all(after[path] == digest for path, digest in before.items() if '/bundles/' in path), 'bundles are immutable')
        self.assertEqual(len(list((home / 'bundles').iterdir())), 2)
        self.assertEqual((home / 'profile.previous.json').read_bytes(), first_pointer)
        self.assertEqual(second['profiles'][0]['tagline'], 'Now with modular')
        self.assertEqual(second['revision'], first['revision'], 'index content did not change, so it was not rewritten')
        with self.assertRaisesRegex(ValueError, 'changed since they were loaded'):
            self.save(expected_revision='2026-01-01T00-00-00Z-dead', dj_id=dj_id)
        with self.assertRaisesRegex(ValueError, 'changed since they were loaded'):
            self.save(expected_revision=None, dj_name='Someone else')

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
        self.assertEqual(json.loads((self.volume / dj_profile.INDEX_PATH).read_text(encoding='utf-8'))['active'], third['saved'])

    def test_set_active_keeps_unknown_index_fields(self):
        first = self.save()
        second = self.save(expected_revision=first['revision'], dj_name='Lena B')
        path = self.volume / dj_profile.INDEX_PATH
        index = json.loads(path.read_text(encoding='utf-8'))
        index['written_by'] = 'xz'
        path.write_text(json.dumps(index), encoding='utf-8')
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

    def test_folder_profile_is_listed_read_only_in_the_stick_colour(self):
        folder = self.volume / 'DJPROFILE' / 'DJ Folder'
        folder.mkdir(parents=True)
        (folder / 'b.png').write_bytes(png(20, 20))
        (folder / 'LOGO.PNG').write_bytes(png(40, 20))
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
        saved = self.save(dj_id=profile['dj_id'], dj_name='DJ Folder', accent=profile['accent'])
        names = [(p['dj_name'], p['source']) for p in saved['profiles']]
        self.assertEqual(names, [('DJ Folder', 'vjtools')], 'the saved VJTOOLS profile wins over its folder')
        self.assertTrue((folder / 'LOGO.PNG').exists())

    def test_limits_are_refused_in_plain_words(self):
        with self.assertRaisesRegex(ValueError, 'at most 8'):
            call('save_dj_profile', volume=str(self.volume), expected_revision=None, profile={'dj_name': 'X'},
                 logos=[{'path': str(self.wide)}] * 9)
        with self.assertRaisesRegex(ValueError, 'https://'):
            self.save(links=[{'kind': 'website', 'url': 'http://nova.example/'}])
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
        Image.frombytes('RGB', (600, 600), __import__('os').urandom(600 * 600 * 3)).save(noisy)
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

    def test_usb_track_carries_the_profile_and_is_silent(self):
        saved = self.save(page_url='https://vj.tools/dj#p1.nova')
        dj_id = saved['saved']
        result = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='usb')
        path = Path(result['path'])
        self.assertEqual(path.parent, self.volume / f'DJPROFILE/VJTOOLS/profiles/{dj_id}/track')
        self.assertEqual(path.name, 'DJ PROFILE - DJ Nova.mp3')
        self.assertEqual((result['logos_embedded'], result['logos_skipped'], result['verified_silent']), (2, [], True))
        self.assertAlmostEqual(result['duration_seconds'], 20, delta=0.1)
        self.assertFalse((self.volume / 'PIONEER').exists())
        frames, data, offset = self.frames(path)
        by_id = {}
        for frame_id, value in frames:
            by_id.setdefault(frame_id, []).append(value)
        for frame_id in ('TIT2', 'TPE1', 'TPE2'):
            self.assertEqual(by_id[frame_id], ['DJ Nova'])
        self.assertEqual((by_id['TALB'], by_id['TCON']), (['DJ PROFILE'], ['DJ Profile']))
        document = json.loads((self.volume / f'DJPROFILE/VJTOOLS/profiles/{dj_id}/profile.json').read_text(encoding='utf-8'))
        payload, = [t['value'] for t in by_id['TXXX'] if t['description'] == 'DJPROFILE']
        self.assertEqual(json.loads(payload), {k: v for k, v in document.items() if k != 'profile_track'})
        comment, = by_id['COMM']
        digest = hashlib.sha256(payload.encode()).hexdigest()[:12]
        self.assertEqual(comment, {'language': 'eng', 'description': '', 'text': f'djprofile:1:{dj_id}:{digest} https://vj.tools/dj#p1.nova'})
        self.assertEqual(by_id['WOAR'], ['https://vj.tools/dj#p1.nova'])
        self.assertEqual(by_id['WXXX'], [{'description': 'EPK', 'url': 'https://nova.example/press.zip'},
                                         {'description': 'website', 'url': 'https://nova.example/'},
                                         {'description': 'instagram', 'url': 'https://instagram.com/nova'}])
        cover, logotype = by_id['APIC']
        self.assertEqual((cover['type'], cover['mime'], cover['data']), (3, 'image/png', self.wide.read_bytes()))
        self.assertEqual((logotype['type'], logotype['mime'], logotype['data']), (0x13, 'image/jpeg', self.square.read_bytes()))
        self.assertEqual(logotype['description'], 'Alternate 1')
        self.assertEqual(document['profile_track'], {'path': f'DJPROFILE/VJTOOLS/profiles/{dj_id}/track/DJ PROFILE - DJ Nova.mp3',
                                                     'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        dj_profile.validate_document(document)
        reread = call('read_dj_profiles', volume=str(self.volume))
        self.assertEqual(reread['profiles'][0]['track']['bytes'], len(data))
        dj_profile.check_silent_audio(data, offset)
        decoded = dj_profile.ffmpeg_silence(path)
        if decoded is None:
            self.skipTest('ffmpeg is not on PATH; the decode proof did not run')
        silent, seconds = decoded
        self.assertTrue(silent, 'ffmpeg decoded only zero samples')
        self.assertAlmostEqual(seconds, 20, delta=0.5)

    def test_folder_track_uses_website_when_no_page_and_survives_resave(self):
        saved = self.save()
        dj_id = saved['saved']
        result = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='folder', folder=str(self.pc / 'out'))
        self.assertEqual(Path(result['path']).parent, self.pc / 'out')
        frames, _, _ = self.frames(result['path'])
        values = dict((frame_id, value) for frame_id, value in frames if frame_id in ('WOAR', 'COMM'))
        self.assertEqual(values['WOAR'], 'https://nova.example/')
        self.assertTrue(values['COMM']['text'].endswith(' https://nova.example/press.zip'), 'EPK link follows the marker')
        self.assertFalse((self.volume / f'DJPROFILE/VJTOOLS/profiles/{dj_id}/track').exists())
        usb = call('write_profile_track', volume=str(self.volume), dj_id=dj_id, destination='usb')
        again = self.save(expected_revision=saved['revision'], dj_id=dj_id, make_active=True)
        self.assertEqual(again['profiles'][0]['track']['path'], usb['path'], 'unchanged content keeps its track')

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
        saved = self.save()
        document = json.loads((self.volume / f'DJPROFILE/VJTOOLS/profiles/{saved["saved"]}/profile.json').read_text(encoding='utf-8'))
        with patch.object(dj_profile, 'MAX_TAG_BYTES', 20000):
            frames, embedded, skipped = dj_profile.track_frames(document, [
                {'data': b'x' * 1000, 'mime': 'image/png', 'label': 'small'},
                {'data': b'x' * 30000, 'mime': 'image/png', 'label': 'big'}])
        self.assertEqual((embedded, skipped), (1, ['big']))


if __name__ == '__main__':
    unittest.main()
