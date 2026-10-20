# SPDX-License-Identifier: CC0-1.0
"""python -m unittest discover -s spec/djprofile/tools -v"""
from __future__ import annotations

import json
import shutil
import tempfile
import unicodedata
import unittest
from pathlib import Path

import check_djprofile as checker

EXAMPLES = Path(__file__).resolve().parent.parent / 'examples'
KAI_ID = '5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21'


def problems(report):
    return [p['problem'] for p in report.problems]


def warnings(report):
    return [p['problem'] for p in report.warnings]


class Examples(unittest.TestCase):
    def test_examples_conform(self):
        for name, level in (('level0', 0), ('level1', 1), ('level1-track', 1)):
            report = checker.check(EXAMPLES / name)
            self.assertEqual(problems(report), [], name)
            self.assertEqual(warnings(report), [], name)
            self.assertEqual(report.active, 'DJ Kai', name)
            self.assertEqual(report.profiles[0]['level'], level, name)

    def test_examples_match_the_schemas(self):
        try:
            import jsonschema
        except ImportError:
            self.skipTest('pip install jsonschema to check the schemas')
        schemas = EXAMPLES.parent / 'schema'
        for schema_name, file_name in (('profile.schema.json', 'profile.json'), ('stick.schema.json', 'DJPROFILE.json')):
            schema = json.loads((schemas / schema_name).read_text(encoding='utf-8'))
            jsonschema.Draft202012Validator.check_schema(schema)
            for path in EXAMPLES.rglob(file_name):
                jsonschema.validate(json.loads(path.read_text(encoding='utf-8')), schema,
                                    cls=jsonschema.Draft202012Validator)

    def test_level1_lists_document_then_folder_profile(self):
        report = checker.check(EXAMPLES / 'level1')
        self.assertEqual([(p['folder'], p['level']) for p in report.profiles], [('DJ Kai', 1), ('Lena B', 0)])


class Stick(unittest.TestCase):
    example = 'level1-track'

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        shutil.copytree(EXAMPLES / self.example, self.root, dirs_exist_ok=True)
        self.top = self.root / 'DJPROFILE'
        self.kai = self.top / 'DJ Kai'

    def tearDown(self):
        shutil.rmtree(self.root)

    def document(self):
        return json.loads((self.kai / 'profile.json').read_text(encoding='utf-8'))

    def edit(self, change):
        document = self.document()
        change(document)
        (self.kai / 'profile.json').write_text(json.dumps(document), encoding='utf-8')

    def folder(self, name, logo_from='DJ Kai/logo.png'):
        (self.top / name).mkdir()
        shutil.copy(self.top / logo_from, self.top / name / 'logo.png')


class Level2(Stick):
    def test_marker_in_export_pdb_is_level2(self):
        marker = self.document()['track']['marker']
        pdb = self.root / 'PIONEER' / 'rekordbox' / 'export.pdb'
        pdb.parent.mkdir(parents=True)
        pdb.write_bytes(b'\0' * 64 + marker.encode('utf-16-le') + b'\0' * 64)
        report = checker.check(self.root)
        self.assertEqual(report.profiles[0]['level'], 2)
        self.assertEqual(report.profiles[0]['track_library'], 'export.pdb')

    def test_marker_only_in_document_stays_level1(self):
        self.assertEqual(checker.check(self.root).profiles[0]['level'], 1)

    def test_stale_track_is_a_warning(self):
        self.edit(lambda d: d.update(tagline='New tagline'))
        report = checker.check(self.root)
        self.assertEqual(problems(report), [])
        self.assertIn('the profile track was made before the last edit; make a new one', warnings(report))

    def test_member_order_does_not_change_the_hash(self):
        document = self.document()
        (self.kai / 'profile.json').write_text(json.dumps(dict(reversed(list(document.items())))), encoding='utf-8')
        report = checker.check(self.root)
        self.assertEqual((problems(report), warnings(report)), ([], []))

    def test_marker_token_after_prepended_text(self):
        self.assertTrue(checker.MARKER.search('8A 124 djprofile:1:abc:0123456789ab https://x.example'))
        self.assertFalse(checker.MARKER.search('xdjprofile:1:abc:0123456789ab'))


class Documents(Stick):
    def test_tampered_logo(self):
        logo = self.kai / 'logo.png'
        original = logo.read_bytes()
        logo.write_bytes(original + b'x')
        self.assertTrue(any('the document says' in p for p in problems(checker.check(self.root))))
        logo.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        self.assertIn('logo.png does not match its sha256', problems(checker.check(self.root)))

    def test_path_escape(self):
        self.edit(lambda d: d['logos'][1].update(file='../Lena B/logo.jpg'))
        self.assertTrue(any('not a safe path' in p for p in problems(checker.check(self.root))))

    def test_two_primaries(self):
        self.edit(lambda d: d['logos'][1].update(role='primary'))
        self.assertIn('exactly one logo has role "primary"', problems(checker.check(self.root)))

    def test_primary_must_be_logo_file(self):
        self.edit(lambda d: (d['logos'][0].update(role='alternate'), d['logos'][1].update(role='primary')))
        self.assertIn('the primary logo file is logo.png or logo.jpg at the top of the folder',
                      problems(checker.check(self.root)))

    def test_http_link(self):
        self.edit(lambda d: d['links'].append({'kind': 'website', 'url': 'http://insecure.example'}))
        self.assertTrue(any('https://' in p for p in problems(checker.check(self.root))))

    def test_float_duplicate_and_non_ascii_member_names_rejected(self):
        for text in ('{"format":"djprofile/1","a":1.5}', '{"format":"djprofile/1","format":"x"}',
                     '{"format":"djprofile/1","naïve":1}'):
            (self.kai / 'profile.json').write_text(text, encoding='utf-8')
            self.assertTrue(any('not valid for DJ Profile' in p for p in problems(checker.check(self.root))), text)

    def test_limits_count_utf8_bytes(self):
        self.edit(lambda d: d.update(note='音' * 3000))
        self.assertIn('note must be text of at most 8192 bytes', problems(checker.check(self.root)))

    def test_broken_document_falls_back_to_previous(self):
        shutil.copy(self.kai / 'profile.json', self.kai / 'profile.previous.json')
        (self.kai / 'profile.json').write_text('{"format": "djprofile/1", ', encoding='utf-8')
        report = checker.check(self.root)
        self.assertEqual(report.profiles[0]['level'], 1)
        self.assertIn('profile.json is unusable; read profile.previous.json', warnings(report))

    def test_fixed_names_fold_ascii_case(self):
        (self.kai / 'profile.json').rename(self.kai / 'Profile.JSON')
        self.assertEqual(checker.check(self.root).profiles[0]['level'], 1)

    def test_rgb565_rendition_size(self):
        data = bytes(10 * 10 * 2 - 1)
        (self.kai / 'tile.rgb565').write_bytes(data)
        import hashlib
        self.edit(lambda d: d.update(renditions=[{'file': 'tile.rgb565', 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                                  'width': 10, 'height': 10, 'format': 'rgb565le', 'for': 'xdj-xz'}]))
        self.assertIn('rgb565le 10x10 must be 200 bytes', problems(checker.check(self.root)))


class Folders(Stick):
    def test_nfd_folder_matches_nfc_active(self):
        self.folder(unicodedata.normalize('NFD', 'Beyoncé'))
        (self.top / 'DJPROFILE.json').write_text(json.dumps({'format': 'djprofile-stick/1', 'active': 'Beyoncé'}),
                                                 encoding='utf-8')
        report = checker.check(self.root)
        self.assertEqual(problems(report), [])
        self.assertEqual(unicodedata.normalize('NFC', report.active), 'Beyoncé')

    def test_appledouble_files_are_skipped(self):
        (self.top / 'Lena B' / '._logo.jpg').write_bytes(b'\0\5\26\7' + bytes(100))
        report = checker.check(self.root)
        self.assertEqual(report.profiles[1]['logo'], 'logo.jpg')
        self.assertEqual(problems(report), [])

    def test_bad_image_falls_through_to_next(self):
        (self.top / 'Lena B' / 'logo.jpg').rename(self.top / 'Lena B' / 'zz.jpg')
        (self.top / 'Lena B' / 'logo.png').write_bytes(b'not a png at all')
        self.assertEqual(checker.check(self.root).profiles[1]['logo'], 'zz.jpg')

    def test_cap_counts_profiles_not_folders(self):
        for i in range(16):
            (self.top / f'Empty {i:02}').mkdir()
        self.folder('Zed')
        self.assertIn('Zed', [p['folder'] for p in checker.check(self.root).profiles])

    def test_active_by_id_survives_folder_rename(self):
        self.kai.rename(self.top / 'Kai Renamed')
        self.assertEqual(checker.check(self.root).active, 'Kai Renamed')

    def test_active_names_missing_profile(self):
        (self.top / 'DJPROFILE.json').write_text(json.dumps({'format': 'djprofile-stick/1', 'active': 'Nobody'}),
                                                 encoding='utf-8')
        report = checker.check(self.root)
        self.assertEqual(report.active, 'DJ Kai')
        self.assertTrue(any('names no profile here' in p for p in problems(report)))

    def test_duplicate_id_later_folder_skipped(self):
        shutil.copytree(self.kai, self.top / 'DJ Kai copy')
        report = checker.check(self.root)
        self.assertNotIn('DJ Kai copy', [p['folder'] for p in report.profiles])
        self.assertIn('same id as an earlier profile; skipped', warnings(report))

    def test_long_emoji_folder_name_breaks_fat_rule(self):
        self.folder('🎧' * 60)
        self.assertTrue(any('FAT32' in p for p in problems(checker.check(self.root))))

    def test_djprofiles_spelling_is_read(self):
        self.top.rename(self.root / 'DJPROFILES')
        report = checker.check(self.root)
        self.assertEqual(len(report.profiles), 2)
        self.assertIn('writers name the folder exactly DJPROFILE', problems(report))


class StickFile(Stick):
    def write(self, value):
        (self.top / 'DJPROFILE.json').write_text(json.dumps({'format': 'djprofile-stick/1', **value}), encoding='utf-8')

    def test_profiles_list_sets_order(self):
        self.write({'profiles': [{'folder': 'Lena B'}, {'folder': 'DJ Kai', 'id': KAI_ID}]})
        report = checker.check(self.root)
        self.assertEqual([p['folder'] for p in report.profiles], ['Lena B', 'DJ Kai'])
        self.assertEqual(report.active, 'Lena B')

    def test_hand_renamed_folder_found_by_id(self):
        self.kai.rename(self.top / 'Kai Renamed')
        report = checker.check(self.root)
        self.assertEqual(report.profiles[0]['folder'], 'Kai Renamed')
        self.assertIn("lists 'DJ Kai', found by id as 'Kai Renamed'", warnings(report))

    def test_listed_id_does_not_match_another_dj_with_that_folder_name(self):
        self.write({'profiles': [{'folder': 'Lena B', 'id': 'someone-else'}]})
        report = checker.check(self.root)
        self.assertIn("'Lena B' is not a profile here; skipped", warnings(report))

    def test_bad_entry(self):
        self.write({'profiles': [{'folder': '../escape'}]})
        self.assertTrue(any('valid folder name' in p for p in problems(checker.check(self.root))))

    def test_folder_with_only_subfolders_is_not_a_profile(self):
        nested = self.top / 'Old Store' / 'profiles' / 'kai'
        nested.mkdir(parents=True)
        shutil.copy(self.kai / 'logo.png', nested / 'logo.png')
        self.assertEqual([p['folder'] for p in checker.check(self.root).profiles], ['DJ Kai', 'Lena B'])


if __name__ == '__main__':
    unittest.main()
