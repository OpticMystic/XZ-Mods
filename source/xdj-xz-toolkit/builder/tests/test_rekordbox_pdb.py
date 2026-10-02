import hashlib
import json
import os
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from builder import rekordbox_pdb  # noqa: E402
from builder.tests import pdb_fixture  # noqa: E402

LEN_PAGE = pdb_fixture.LEN_PAGE
FIRST_DATA_PAGE = 2

# Real exports from public test corpora, pinned by commit and SHA-256. Set
# XZ_PDB_CORPUS to a file path; a missing file is downloaded from the matching URL
# named by XZ_PDB_CORPUS_SHA256 (default: the largest corpus file).
CORPUS = {
    "63597e1c1db011ddcd0ef5552eca121ad23cb8366b215574ae7a49b6887e8c6e": {
        "url": "https://raw.githubusercontent.com/Holzhaus/rekordcrate/"
               "14d54eded7b0d2f6fe67c7871533dca35a73e5fa/data/pdb/num_rows/export.pdb",
        "count": 3886,
        "mapping_sha256": "d2bc76e1b461f108f59a08753f21b2b21eaaf05c9dc9cb0f88d02f365e6766ef",
        "spot": {
            1: "/Contents/Andreas Gehm/The Worst of Gehm/9840607_My_So_Called_Robot_Life_Part_2_Origi.mp3",
            3: "/Contents/DJ Plant Texture/1ØPILLS003 MASTER MP3s/1ØPILLS003_mastered_04.mp3",
        },
    },
    "27d5a84d7688244357c467680b7e62a36a0e6f3f7eb0ab4c37b26aa312e7f8aa": {
        "url": "https://raw.githubusercontent.com/Holzhaus/rekordcrate/"
               "14d54eded7b0d2f6fe67c7871533dca35a73e5fa/data/complete_export/demo_tracks/"
               "PIONEER/rekordbox/export.pdb",
        "count": 2,
        "mapping_sha256": "d3532717adbea268458ae36061933f12e84225b3e48b60ad25f2bed01444c13b",
        "spot": {
            1: "/Contents/Loopmasters/UnknownAlbum/Demo Track 1.mp3",
            2: "/Contents/Loopmasters/UnknownAlbum/Demo Track 2.mp3",
        },
    },
    "e85c387e95f1069c3ad5b39cbdaf50e6dd390ed00a47966146c5488ae740366e": {
        "url": "https://raw.githubusercontent.com/dylanljones/pyrekordbox/"
               "f695541827cc488af267d6ca8a8e0052598d85a0/.testdata/export/PIONEER/rekordbox/export.pdb",
        "count": 6,
        "mapping_sha256": "1df98c70f1752d0cabed415b46b0744f56b19767fa46b2b93b0390c5efa3c312",
        "spot": {
            3: "/Contents/UnknownArtist/UnknownAlbum/HORN.wav",
            6: "/Contents/UnknownArtist/UnknownAlbum/SIREN.wav",
        },
    },
}
DEFAULT_CORPUS = "63597e1c1db011ddcd0ef5552eca121ad23cb8366b215574ae7a49b6887e8c6e"


def mapping_digest(mapping):
    canonical = json.dumps(sorted(mapping.items()), ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def tracks(count, start=1):
    return {i: f"/Contents/Artist {i}/Track {i}.wav" for i in range(start, start + count)}


class RekordboxPdbTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.usb = Path(self.temp.name)
        self.pdb = self.usb.joinpath(*rekordbox_pdb.EXPORT_PDB)

    def write(self, mapping):
        return pdb_fixture.write_export(self.pdb, mapping)

    def patch_bytes(self, offset, fmt, *values):
        data = bytearray(self.pdb.read_bytes())
        struct.pack_into(fmt, data, offset, *values)
        self.pdb.write_bytes(data)

    def assert_unreadable(self):
        with self.assertRaisesRegex(ValueError, "Rekordbox export on this USB is unreadable"):
            rekordbox_pdb.track_paths(self.pdb)

    def test_round_trip_across_pages_and_string_forms(self):
        mapping = tracks(40)
        mapping[41] = "/Contents/Bjørk/日本語 Track.flac"
        mapping[42] = "/Contents/" + "x" * 150 + "/Long.wav"
        self.write(mapping)
        self.assertGreater(self.pdb.stat().st_size // LEN_PAGE, FIRST_DATA_PAGE + 1)
        self.assertEqual(rekordbox_pdb.track_paths(self.pdb), mapping)

    def test_deleted_rows_do_not_hide_later_groups(self):
        mapping = tracks(20)
        self.write(mapping)
        page = FIRST_DATA_PAGE * LEN_PAGE
        counts = int.from_bytes(self.pdb.read_bytes()[page + 0x18:page + 0x1B], "little")
        self.assertEqual(counts & 0x1FFF, 20)
        self.patch_bytes(page + LEN_PAGE - 4, "<H", 0)
        self.patch_bytes(page + 0x18, "<I", 20 | 4 << 13 | 0x34 << 24)
        self.assertEqual(rekordbox_pdb.track_paths(self.pdb), tracks(4, start=17))

    def test_find_track_exact_then_casefold(self):
        self.write({7: "/Contents/Artist/Track.WAV", 8: "/Contents/Artist/track.wav",
                    9: "/Contents/Other/Song.wav"})
        self.assertEqual(rekordbox_pdb.find_track(self.usb, "/Contents/Artist/track.wav"),
                         (8, "/Contents/Artist/track.wav"))
        self.assertEqual(rekordbox_pdb.find_track(self.usb, "/contents/other/SONG.wav"),
                         (9, "/Contents/Other/Song.wav"))
        with self.assertRaisesRegex(ValueError, "more than once"):
            rekordbox_pdb.find_track(self.usb, "/Contents/Artist/TRACK.wav")

    def test_find_track_matches_composed_and_decomposed_accents(self):
        composed = "/Contents/Café/Song.wav"
        self.write({5: composed})
        self.assertEqual(rekordbox_pdb.find_track(self.usb, "/Contents/Café/Song.wav"), (5, composed))

    def test_find_track_rejects_duplicate_rows(self):
        self.write({3: "/Contents/A/B.wav", 4: "/Contents/A/B.wav"})
        with self.assertRaisesRegex(ValueError, "lists this file more than once"):
            rekordbox_pdb.find_track(self.usb, "/Contents/A/B.wav")

    def test_find_track_not_exported(self):
        self.write(tracks(2))
        with self.assertRaisesRegex(ValueError, "has not exported this track"):
            rekordbox_pdb.find_track(self.usb, "/Contents/Missing.wav")

    def test_missing_export(self):
        with self.assertRaises(ValueError) as caught:
            rekordbox_pdb.find_track(self.usb, "/Contents/A.wav")
        self.assertEqual(str(caught.exception),
                         "This USB has no Rekordbox export. Export the track with Rekordbox first.")

    def test_truncated_file(self):
        self.write(tracks(40))
        self.pdb.write_bytes(self.pdb.read_bytes()[:FIRST_DATA_PAGE * LEN_PAGE + 100])
        self.assert_unreadable()
        self.pdb.write_bytes(b"")
        self.assert_unreadable()

    def test_cyclic_page_chain(self):
        self.write(tracks(40))
        last = self.pdb.stat().st_size // LEN_PAGE - 1
        self.patch_bytes(last * LEN_PAGE + 0x0C, "<I", FIRST_DATA_PAGE)
        self.patch_bytes(0x1C + 0x0C, "<I", 999)
        self.assert_unreadable()

    def test_row_index_larger_than_page(self):
        self.write(tracks(1))
        self.patch_bytes(FIRST_DATA_PAGE * LEN_PAGE + 0x18, "<H", 0x1FFF)
        self.assert_unreadable()

    def test_unknown_string_kind(self):
        self.write(tracks(1))
        row = FIRST_DATA_PAGE * LEN_PAGE + rekordbox_pdb.HEAP
        path_offset, = struct.unpack_from("<H", self.pdb.read_bytes(), row + 0x5E + 2 * 20)
        self.patch_bytes(row + path_offset, "<B", 0x20)
        self.assert_unreadable()

    def test_rejects_oversized_file_before_reading(self):
        self.write(tracks(1))
        with patch.object(rekordbox_pdb, "MAX_PDB_BYTES", LEN_PAGE), \
             patch.object(Path, "read_bytes") as read:
            self.assert_unreadable()
            read.assert_not_called()

    def test_corrupted_bytes_only_raise_value_error(self):
        self.write(tracks(40))
        original = self.pdb.read_bytes()
        rng = random.Random(0x5EED)
        for _ in range(400):
            data = bytearray(original)
            for _ in range(rng.randint(1, 8)):
                data[rng.randrange(len(data))] = rng.randrange(256)
            self.pdb.write_bytes(data)
            try:
                rekordbox_pdb.track_paths(self.pdb)
            except ValueError as error:
                self.assertIn("unreadable", str(error))

    @unittest.skipUnless(os.environ.get("XZ_PDB_CORPUS"), "set XZ_PDB_CORPUS to a real export.pdb path")
    def test_real_export_corpus(self):
        path = Path(os.environ["XZ_PDB_CORPUS"])
        expected_sha = os.environ.get("XZ_PDB_CORPUS_SHA256", DEFAULT_CORPUS)
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(CORPUS[expected_sha]["url"], timeout=60) as response:
                path.write_bytes(response.read())
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertIn(sha, CORPUS, "unknown corpus file")
        expected = CORPUS[sha]
        mapping = rekordbox_pdb.track_paths(path)
        self.assertEqual(len(mapping), expected["count"])
        for track_id, stored in expected["spot"].items():
            self.assertEqual(mapping[track_id], stored)
        self.assertEqual(mapping_digest(mapping), expected["mapping_sha256"])


if __name__ == "__main__":
    unittest.main()
