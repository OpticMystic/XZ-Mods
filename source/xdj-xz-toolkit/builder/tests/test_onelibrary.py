"""Reading rekordbox OneLibrary exports: SQLCipher 4 pages decrypted in Python, the WAL applied in memory.

fixtures/onelibrary/rekordbox7-sample is a real rekordbox 7 export (see its NOTICE.md).
fixtures/onelibrary/wal-profile is made by make_onelibrary_fixture.py with SQLCipher itself.
"""
import hashlib
from pathlib import Path
import shutil
import sqlite3
import struct
import tempfile
import unittest

from builder import onelibrary

FIXTURES = Path(__file__).parent / 'fixtures' / 'onelibrary'
SAMPLE = FIXTURES / 'rekordbox7-sample'
WAL_PROFILE = FIXTURES / 'wal-profile'
PAGE = onelibrary.PAGE_SIZE
FRAME = onelibrary.WAL_FRAME.size + PAGE


def stick_state(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in Path(root).rglob('*') if path.is_file()}


def reseal(wal, header_salts=None):
    """Recompute every WAL checksum as SQLite would, optionally after changing the header salts only."""
    wal = bytearray(wal)
    big = bool(wal[3] & 1)
    frame_salts = struct.unpack_from('>II', wal, 16)
    if header_salts:
        struct.pack_into('>II', wal, 16, *header_salts)
    s1, s2 = onelibrary._checksum(bytes(wal[:24]), 0, 0, big)
    struct.pack_into('>II', wal, 24, s1, s2)
    for start in range(onelibrary.WAL_HEADER.size, len(wal) - FRAME + 1, FRAME):
        s1, s2 = onelibrary._checksum(bytes(wal[start:start + 8]), s1, s2, big)
        s1, s2 = onelibrary._checksum(bytes(wal[start + 24:start + FRAME]), s1, s2, big)
        struct.pack_into('>IIII', wal, start + 8, *frame_salts, s1, s2)
    return bytes(wal)


def image_connection(image):
    connection = sqlite3.connect(':memory:')
    connection.deserialize(image)
    return connection


class OneLibraryTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.db = self.root.joinpath(*onelibrary.EXPORT_LIBRARY)

    def stick(self, fixture, wal=True):
        self.db.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fixture / 'exportLibrary.db', self.db)
        if wal and (fixture / 'exportLibrary.db-wal').exists():
            shutil.copyfile(fixture / 'exportLibrary.db-wal', self.wal)

    @property
    def wal(self):
        return self.db.with_name('exportLibrary.db-wal')

    def patch(self, path, offset, data):
        content = bytearray(path.read_bytes())
        content[offset:offset + len(data)] = data
        path.write_bytes(content)

    def profile_tracks(self):
        with onelibrary.open_library(self.root) as connection:
            return {track['title']: track for track in onelibrary.tracks_with_comment_containing(connection, 'djprofile:1:')}

    def test_passphrase_matches_the_published_prefix(self):
        passphrase = onelibrary._passphrase()
        self.assertTrue(passphrase.startswith('r8gd'))
        self.assertEqual(len(passphrase), 64)

    def test_reads_the_real_rekordbox_7_export(self):
        self.stick(SAMPLE)
        self.assertTrue(onelibrary.present(self.root))
        self.assertEqual(onelibrary.summary(self.root), {'device_name': 'SAMPLE', 'db_version': '1000', 'tracks': 3,
                                                        'playlists': 1, 'background_colour_type': 0})
        with onelibrary.open_library(self.root) as connection:
            tracks = onelibrary.tracks_with_comment_containing(connection, 'CC BY')
            root_parent = connection.execute('SELECT playlist_id_parent FROM playlist').fetchone()[0]
        self.assertEqual(root_parent, 0)
        self.assertEqual([track['title'] for track in tracks], ['Electrodoodle', 'Cipher', 'Cold Funk'])
        self.assertEqual(tracks[0], {'content_id': 1, 'title': 'Electrodoodle', 'artist': 'Kevin MacLeod',
                                     'path': '/Contents/Kevin MacLeod/Incompetech/electrodoodle.mp3',
                                     'djComment': 'CC BY 4.0 - incompetech.com', 'image_path': None,
                                     'playlists': ['Sample Library']})

    def test_the_connection_is_read_only_and_the_stick_is_untouched(self):
        self.stick(WAL_PROFILE)
        before = stick_state(self.root)
        with onelibrary.open_library(self.root) as connection:
            with self.assertRaises(sqlite3.OperationalError):
                connection.execute("DELETE FROM content")
        self.assertEqual(stick_state(self.root), before)

    def test_profile_rows_live_only_in_the_wal(self):
        self.stick(WAL_PROFILE)
        main_only = image_connection(onelibrary.decrypt(self.db.read_bytes()))
        self.assertEqual(main_only.execute("SELECT count(*) FROM content").fetchone()[0], 120)
        self.assertEqual(main_only.execute("SELECT count(*) FROM playlist WHERE name = 'DJ PROFILE'").fetchone()[0], 0)
        self.assertEqual(main_only.execute("SELECT count(*) FROM content WHERE djComment LIKE 'djprofile:%'").fetchone()[0], 0)

        tracks = self.profile_tracks()
        self.assertEqual(set(tracks), {'DJ Kai', 'Lena B'})
        kai = tracks['DJ Kai']
        self.assertEqual(kai['artist'], 'DJ Kai')
        self.assertEqual(kai['image_path'], '/PIONEER/Artwork/00001/a1.jpg')
        self.assertEqual(kai['playlists'], ['DJ PROFILE'])
        self.assertTrue(kai['djComment'].startswith('djprofile:1:djp_kai:'))
        self.assertEqual(tracks['Lena B']['playlists'], ['Friday'])
        self.assertEqual(onelibrary.summary(self.root)['tracks'], 122)
        self.assertEqual(onelibrary.summary(self.root)['playlists'], 2)

    def commit_frames(self):
        wal = self.wal.read_bytes()
        frames = (len(wal) - onelibrary.WAL_HEADER.size) // FRAME
        return [n for n in range(frames) if struct.unpack_from('>I', wal, onelibrary.WAL_HEADER.size + n * FRAME + 4)[0]]

    def test_only_committed_frames_with_valid_checksums_count(self):
        self.stick(WAL_PROFILE)
        commits = self.commit_frames()
        self.assertEqual(len(commits), 2)
        first_commit_end = onelibrary.WAL_HEADER.size + (commits[0] + 1) * FRAME
        wal = self.wal.read_bytes()

        self.wal.write_bytes(wal[:first_commit_end + FRAME // 2])
        self.assertEqual(set(self.profile_tracks()), {'DJ Kai'}, 'a torn last commit is dropped')

        self.wal.write_bytes(wal)
        self.patch(self.wal, first_commit_end + onelibrary.WAL_FRAME.size + 100, b'\xff\x00\xff')
        self.assertEqual(set(self.profile_tracks()), {'DJ Kai'}, 'a frame failing its checksum ends the WAL')

        self.wal.write_bytes(wal[:first_commit_end - FRAME])
        self.assertEqual(self.profile_tracks(), {}, 'frames after the last commit are not applied')

    def test_a_wal_from_another_generation_is_ignored(self):
        self.stick(WAL_PROFILE)
        wal = self.wal.read_bytes()
        self.assertEqual(reseal(wal), wal, 'the checksum code matches what SQLite wrote')
        self.wal.write_bytes(reseal(wal, header_salts=(1, 2)))
        self.assertEqual(self.profile_tracks(), {}, 'frames whose salts differ from the header are stale')
        self.patch(self.wal, 24, b'\0\0\0\0')
        self.assertEqual(self.profile_tracks(), {}, 'a WAL header failing its checksum is ignored')
        self.wal.write_bytes(b'')
        self.assertEqual(self.profile_tracks(), {})

    def test_wrong_key_is_a_clear_error(self):
        database = (SAMPLE / 'exportLibrary.db').read_bytes()
        with self.assertRaises(onelibrary.OneLibraryError) as caught:
            onelibrary.decrypt(database, passphrase='not the rekordbox key')
        self.assertIn('could not be unlocked', str(caught.exception))

    def test_a_plain_sqlite_file_is_not_a_onelibrary_export(self):
        self.db.parent.mkdir(parents=True)
        plain = sqlite3.connect(self.db)
        plain.execute('CREATE TABLE content(content_id integer primary key, djComment varchar)')
        plain.commit()
        plain.close()
        with self.assertRaisesRegex(onelibrary.OneLibraryError, 'could not be unlocked'):
            onelibrary.summary(self.root)

    def test_a_damaged_page_names_the_page(self):
        self.stick(SAMPLE)
        self.patch(self.db, 4 * PAGE + 200, b'\x00\x01')
        with self.assertRaisesRegex(onelibrary.OneLibraryError, r'damaged \(page 5 failed its check\)'):
            onelibrary.summary(self.root)
        self.stick(SAMPLE)
        self.patch(self.db, 2 * PAGE - 1, b'\x55')
        with self.assertRaisesRegex(onelibrary.OneLibraryError, 'page 2 failed its check'):
            onelibrary.summary(self.root)
        self.stick(SAMPLE)
        self.db.write_bytes(self.db.read_bytes()[:-10])
        with self.assertRaisesRegex(onelibrary.OneLibraryError, 'whole number of pages'):
            onelibrary.summary(self.root)

    def test_a_tampered_wal_page_fails_its_tag(self):
        self.stick(WAL_PROFILE)
        wal = bytearray(self.wal.read_bytes())
        wal[onelibrary.WAL_HEADER.size + FRAME + onelibrary.WAL_FRAME.size + 300] ^= 1
        self.wal.write_bytes(reseal(wal))
        with self.assertRaisesRegex(onelibrary.OneLibraryError, r'page \d+ failed its check'):
            self.profile_tracks()

    def test_missing_database(self):
        self.assertFalse(onelibrary.present(self.root))
        with self.assertRaisesRegex(onelibrary.OneLibraryError, 'no OneLibrary database'):
            onelibrary.summary(self.root)


if __name__ == '__main__':
    unittest.main()
