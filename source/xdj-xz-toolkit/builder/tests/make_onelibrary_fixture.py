# SPDX-License-Identifier: MIT
"""Regenerate fixtures/onelibrary/wal-profile: an encrypted OneLibrary database whose DJ profile
rows exist only in an uncheckpointed exportLibrary.db-wal, as rekordbox leaves a fresh export.

Generation needs SQLCipher; the reader under test does not.
    pip install sqlcipher3-wheels
    python -m builder.tests.make_onelibrary_fixture      (from source/xdj-xz-toolkit)

The profile markers are built from the shared stick-two-profiles fixture: DJ Kai's carries the
current hash, Lena B's an old one. The script ends by reading both copies back with SQLCipher
and with builder.onelibrary and checking they agree.

"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import sqlite3
import tempfile

from builder import dj_profile, onelibrary

HERE = Path(__file__).parent
OUT = HERE / "fixtures" / "onelibrary" / "wal-profile"
PROFILES = HERE / "fixtures" / "dj-profile" / "stick-two-profiles" / "DJPROFILE" / "VJTOOLS" / "profiles"

# Verbatim from a rekordbox 7 export (docs/research/2026-10-19-onelibrary-format.md section 3).
SCHEMA = [
    "CREATE TABLE content(content_id integer primary key, title varchar, titleForSearch varchar, subtitle varchar, bpmx100 integer, length integer, trackNo integer, discNo integer, artist_id_artist integer, artist_id_remixer integer, artist_id_originalArtist integer, artist_id_composer integer, artist_id_lyricist integer, album_id integer, genre_id integer, label_id integer, key_id integer, color_id integer, image_id integer, djComment varchar, rating integer, releaseYear integer, releaseDate varchar, dateCreated varchar, dateAdded varchar, path varchar, fileName varchar, fileSize integer, fileType integer, bitrate integer, bitDepth integer, samplingRate integer, isrc varchar, djPlayCount integer, isHotCueAutoLoadOn integer, isKuvoDeliverStatusOn integer, kuvoDeliveryComment varchar, masterDbId integer, masterContentId integer, analysisDataFilePath varchar, analysedBits integer, contentLink integer, hasModified integer, cueUpdateCount integer, analysisDataUpdateCount integer, informationUpdateCount integer)",
    "CREATE TABLE artist(artist_id integer primary key, name varchar, nameForSearch varchar)",
    "CREATE TABLE image(image_id integer primary key, path varchar)",
    "CREATE TABLE playlist(playlist_id integer primary key, sequenceNo integer, name varchar, image_id integer, attribute integer, playlist_id_parent integer)",
    "CREATE TABLE playlist_content(playlist_id integer, content_id integer, sequenceNo integer)",
    "CREATE TABLE property(deviceName varchar, dbVersion varchar, numberOfContents integer, createdDate varchar, backGroundColorType integer, myTagMasterDBID integer)",
    "CREATE INDEX index_playlist_content_playlist_id on playlist_content(playlist_id)",
]
FILLER = 120


def marker(dj_id: str, current: bool) -> str:
    document = dj_profile.validate_old_document(json.loads((PROFILES / dj_id / "profile.json").read_text(encoding="utf-8")))
    return dj_profile.track_marker(dj_profile.from_old(document)[0]) if current else f"djprofile:1:{dj_id}:000000000000"


def track(db, content_id, title, artist_id, path, comment, image_id=None):
    db.execute("INSERT INTO content(content_id, title, titleForSearch, artist_id_artist, image_id, djComment, path, fileName, fileType)"
               " VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1)", (content_id, title, title.upper(), artist_id, image_id, comment, path, path.rsplit("/", 1)[-1]))


def build(folder: Path, sqlcipher) -> None:
    db = sqlcipher.connect(str(folder / "exportLibrary.db"), isolation_level=None)
    db.execute(f"PRAGMA key = '{onelibrary._passphrase()}'")
    db.execute("PRAGMA cipher_compatibility = 4")
    db.execute("PRAGMA journal_mode = WAL")
    db.execute("PRAGMA wal_autocheckpoint = 0")
    db.execute("BEGIN")
    for statement in SCHEMA:
        db.execute(statement)
    db.execute("INSERT INTO artist VALUES (1, 'Opening Act', 'OPENING ACT')")
    for n in range(1, FILLER + 1):
        track(db, n, f"Track {n}", 1, f"/Contents/Opening Act/Track {n}.mp3", "Purchased at a shop")
    db.execute("INSERT INTO playlist VALUES (1, 1, 'Gigs', NULL, 1, 0)")
    db.execute("INSERT INTO playlist VALUES (2, 1, 'Friday', NULL, 0, 1)")
    db.executemany("INSERT INTO playlist_content VALUES (2, ?, ?)", [(n, n) for n in range(1, FILLER + 1)])
    db.execute("INSERT INTO property VALUES ('FIXTURE', '1000', ?, '2026-10-19', 0, 0)", (FILLER,))
    db.execute("COMMIT")
    db.execute("PRAGMA wal_checkpoint(TRUNCATE)")

    kai, lena = FILLER + 1, FILLER + 2
    db.execute("BEGIN")
    db.execute("INSERT INTO artist VALUES (2, 'DJ Kai', 'DJ KAI')")
    db.execute("INSERT INTO image VALUES (1, '/PIONEER/Artwork/00001/a1.jpg')")
    track(db, kai, "DJ Kai", 2, "/Contents/DJ Kai/DJ PROFILE/DJ PROFILE - DJ Kai.mp3",
          marker("djp_kai", True) + " https://vj.tools/dj#p1.fixture", image_id=1)
    db.execute("INSERT INTO playlist VALUES (3, 2, 'DJ PROFILE', NULL, 0, 0)")
    db.execute("INSERT INTO playlist_content VALUES (3, ?, 1)", (kai,))
    db.execute("COMMIT")
    db.execute("BEGIN")
    db.execute("INSERT INTO artist VALUES (3, 'Lena B', 'LENA B')")
    track(db, lena, "Lena B", 3, "/Contents/Lena B/DJ PROFILE/DJ PROFILE - Lena B.mp3", marker("djp_lena", False))
    db.execute("INSERT INTO playlist_content VALUES (2, ?, ?)", (lena, FILLER + 1))
    db.execute("UPDATE property SET numberOfContents = ?", (lena,))
    db.execute("COMMIT")
    # Copy while the connection is open: closing the last connection checkpoints and deletes the WAL.
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ("exportLibrary.db", "exportLibrary.db-wal"):
        shutil.copyfile(folder / name, OUT / name)
    db.close()


def rows(connection) -> list:
    return [tuple(row) for row in connection.execute(
        "SELECT c.content_id, c.title, c.djComment, a.name, i.path FROM content c"
        " LEFT JOIN artist a ON a.artist_id = c.artist_id_artist LEFT JOIN image i ON i.image_id = c.image_id"
        " ORDER BY c.content_id")]


def check(sqlcipher) -> None:
    with tempfile.TemporaryDirectory() as temp:
        for name in ("exportLibrary.db", "exportLibrary.db-wal"):
            shutil.copyfile(OUT / name, Path(temp) / name)
        reference = sqlcipher.connect(str(Path(temp) / "exportLibrary.db"))
        reference.execute(f"PRAGMA key = '{onelibrary._passphrase()}'")
        expected = rows(reference)
        reference.close()
    root = Path(tempfile.mkdtemp())
    try:
        target = root.joinpath(*onelibrary.EXPORT_LIBRARY).parent
        target.mkdir(parents=True)
        for name in ("exportLibrary.db", "exportLibrary.db-wal"):
            shutil.copyfile(OUT / name, target / name)
        with onelibrary.open_library(root) as connection:
            actual = rows(connection)
        main_only = rows(sqlite3_image(onelibrary.decrypt((OUT / "exportLibrary.db").read_bytes())))
    finally:
        shutil.rmtree(root)
    assert actual == expected, "builder.onelibrary disagrees with SQLCipher"
    assert len(actual) == FILLER + 2 and len(main_only) == FILLER, "the profile rows must live only in the WAL"
    print(f"wrote {OUT}: {len(actual)} tracks with the WAL, {len(main_only)} without")


def sqlite3_image(image: bytes):
    connection = sqlite3.connect(":memory:")
    connection.deserialize(image)
    return connection


def main() -> None:
    from sqlcipher3 import dbapi2 as sqlcipher
    with tempfile.TemporaryDirectory() as temp:
        build(Path(temp), sqlcipher)
    check(sqlcipher)


if __name__ == "__main__":
    main()
