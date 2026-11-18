# SPDX-License-Identifier: MIT
"""Read a rekordbox OneLibrary export (PIONEER/rekordbox/exportLibrary.db) without touching the stick.

The database is SQLCipher 4 with its default settings: AES-256-CBC over 4096-byte pages,
the key from PBKDF2-HMAC-SHA512 (256,000 rounds) over a per-file salt, and an HMAC-SHA512
tag on every page. Parameters and page layout follow sqlcipher.c (sqlcipher_page_cipher,
sqlcipher_page_hmac, sqlcipher_cipher_ctx_key_derive). Pages are decrypted here, so no
SQLCipher build is needed, and the plain image opens in the standard sqlite3 module.

rekordbox leaves much of a fresh export in exportLibrary.db-wal, so the committed WAL
frames are applied in memory too. Nothing is written to the stick or to disk.
"""
from __future__ import annotations

import base64
import contextlib
import hashlib
import hmac
import os
from pathlib import Path
import sqlite3
import struct
from typing import NamedTuple
import zlib

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

EXPORT_LIBRARY = ("PIONEER", "rekordbox", "exportLibrary.db")
MAX_DB_BYTES = 1 << 30

# The export passphrase is the same on every rekordbox install. This obfuscated form and its
# decoder come from pyrekordbox (MIT, Dylan Jones): BLOB in pyrekordbox/onelibrary/database.py
# and deobfuscate() with BLOB_KEY in pyrekordbox/utils.py.
_BLOB = b"PN_1dH8$oLJY)16j_RvM6qphWw`476>;C1cWmI#se(PG`j}~xAjlufj?`#0i{;=glh(SkW)y0>n?YEiD`l%t("
_BLOB_KEY = b"657f48f84c437cc1"

PAGE_SIZE = 4096
SALT_SIZE = 16
IV_SIZE = 16
HMAC_SIZE = 64
USABLE = PAGE_SIZE - IV_SIZE - HMAC_SIZE
KDF_ROUNDS = 256_000
HMAC_KDF_ROUNDS = 2
HMAC_SALT_MASK = 0x3A
KEY_SIZE = 32
SQLITE_HEADER = b"SQLite format 3\0"
# Header bytes 18 and 19 are 2 in WAL mode. The image here already has its WAL applied,
# and an in-memory database cannot open in WAL mode.
JOURNAL_MODE_AT = 18
ROLLBACK_JOURNAL = b"\x01\x01"

WAL_HEADER = struct.Struct(">8I")  # magic, version, page size, checkpoint, salt-1, salt-2, checksum-1, checksum-2
WAL_FRAME = struct.Struct(">6I")  # page number, pages after commit (0 if not a commit), salt-1, salt-2, checksums
WAL_MAGIC = 0x377F0682  # the low bit set means big-endian checksum words

LOCKED = ("The OneLibrary database on this USB could not be unlocked. It was not written by DJ software, "
          "or it is damaged. Export the USB again from your DJ software.")
DAMAGED = "The OneLibrary database on this USB is damaged ({}). Export the USB again from your DJ software."
MISSING = "This USB has no OneLibrary database."


class OneLibraryError(ValueError):
    pass


class _Keys(NamedTuple):
    cipher: bytes
    mac: bytes


def _passphrase() -> str:
    data = base64.b85decode(_BLOB)
    text = zlib.decompress(bytes(b ^ _BLOB_KEY[i % len(_BLOB_KEY)] for i, b in enumerate(data))).decode("utf-8")
    if not text.startswith("r8gd"):
        raise RuntimeError("The OneLibrary passphrase did not decode")
    return text


def _keys(passphrase: str, salt: bytes) -> _Keys:
    cipher = hashlib.pbkdf2_hmac("sha512", passphrase.encode("utf-8"), salt, KDF_ROUNDS, KEY_SIZE)
    mac_salt = bytes(b ^ HMAC_SALT_MASK for b in salt)
    return _Keys(cipher, hashlib.pbkdf2_hmac("sha512", cipher, mac_salt, HMAC_KDF_ROUNDS, KEY_SIZE))


def _decrypt_page(keys: _Keys, number: int, page: bytes) -> bytes:
    """One plain page. On disk, page 1 starts with the salt instead of the SQLite header."""
    start = SALT_SIZE if number == 1 else 0
    tag = hmac.new(keys.mac, page[start:USABLE + IV_SIZE] + struct.pack("<I", number), "sha512").digest()
    if not hmac.compare_digest(tag, page[USABLE + IV_SIZE:]):
        raise OneLibraryError(LOCKED if number == 1 else DAMAGED.format(f"page {number} failed its check"))
    decryptor = Cipher(algorithms.AES(keys.cipher), modes.CBC(page[USABLE:USABLE + IV_SIZE])).decryptor()
    plain = decryptor.update(page[start:USABLE]) + decryptor.finalize()
    return (SQLITE_HEADER if number == 1 else b"") + plain + page[USABLE:]


def _checksum(data: bytes, s1: int, s2: int, big_endian: bool) -> tuple[int, int]:
    words = struct.unpack((">" if big_endian else "<") + f"{len(data) // 4}I", data)
    for i in range(0, len(words), 2):
        s1 = (s1 + words[i] + s2) & 0xFFFFFFFF
        s2 = (s2 + words[i + 1] + s1) & 0xFFFFFFFF
    return s1, s2


def _wal_frames(wal: bytes) -> tuple[dict[int, bytes], int | None]:
    """The committed pages of a WAL, latest copy of each, and the page count after the last commit.

    Like SQLite, this reads frames from the start while their salts match the header and
    the running checksum holds, and keeps only frames up to the last commit frame.
    """
    if len(wal) < WAL_HEADER.size:
        return {}, None
    magic, _version, page_size, _checkpoint, salt1, salt2, sum1, sum2 = WAL_HEADER.unpack_from(wal)
    big_endian = bool(magic & 1)
    if magic & ~1 != WAL_MAGIC or page_size != PAGE_SIZE:
        return {}, None
    s1, s2 = _checksum(wal[:24], 0, 0, big_endian)
    if (s1, s2) != (sum1, sum2):
        return {}, None
    committed, pending, pages = {}, {}, None
    at = WAL_HEADER.size
    while at + WAL_FRAME.size + PAGE_SIZE <= len(wal):
        number, commit, frame1, frame2, sum1, sum2 = WAL_FRAME.unpack_from(wal, at)
        body = wal[at + WAL_FRAME.size:at + WAL_FRAME.size + PAGE_SIZE]
        if number == 0 or (frame1, frame2) != (salt1, salt2):
            break
        s1, s2 = _checksum(body, *_checksum(wal[at:at + 8], s1, s2, big_endian), big_endian)
        if (s1, s2) != (sum1, sum2):
            break
        pending[number] = body
        if commit:
            committed.update(pending)
            pending, pages = {}, commit
        at += WAL_FRAME.size + PAGE_SIZE
    return committed, pages


def decrypt(database: bytes, wal: bytes = b"", passphrase: str | None = None) -> bytes:
    """The plain SQLite image of an encrypted OneLibrary file and its WAL, every page tag checked."""
    if len(database) % PAGE_SIZE:
        raise OneLibraryError(DAMAGED.format("its size is not a whole number of pages"))
    frames, wal_pages = _wal_frames(wal)
    first = database[:PAGE_SIZE] or frames.get(1)
    if not first:
        raise OneLibraryError(DAMAGED.format("it is empty"))
    keys = _keys(passphrase if passphrase is not None else _passphrase(), first[:SALT_SIZE])
    stored = len(database) // PAGE_SIZE
    count = wal_pages if wal_pages is not None else stored
    image = bytearray()
    for number in range(1, max(count, stored) + 1):
        if number <= stored:
            plain = _decrypt_page(keys, number, database[(number - 1) * PAGE_SIZE:number * PAGE_SIZE])
        if number in frames:
            plain = _decrypt_page(keys, number, frames[number])
        elif number > stored:
            raise OneLibraryError(DAMAGED.format(f"page {number} is missing"))
        if number <= count:
            image += plain
    image[JOURNAL_MODE_AT:JOURNAL_MODE_AT + 2] = ROLLBACK_JOURNAL
    return bytes(image)


def _read(path: Path) -> bytes:
    if path.stat().st_size > MAX_DB_BYTES:
        raise OneLibraryError(DAMAGED.format("it is larger than 1 GB"))
    return path.read_bytes()


def present(root: str | os.PathLike) -> bool:
    return Path(root, *EXPORT_LIBRARY).is_file()


@contextlib.contextmanager
def open_library(root: str | os.PathLike):
    """A read-only sqlite3 connection on the stick's OneLibrary database with its WAL applied, in memory."""
    path = Path(root, *EXPORT_LIBRARY)
    wal = path.with_name(path.name + "-wal")
    try:
        database = _read(path)
        journal = _read(wal) if wal.is_file() else b""
    except FileNotFoundError:
        raise OneLibraryError(MISSING) from None
    image = decrypt(database, journal)
    connection = sqlite3.connect(":memory:")
    try:
        try:
            connection.deserialize(image)
            connection.execute("PRAGMA query_only = ON")
            healthy = connection.execute("PRAGMA quick_check").fetchone()[0] == "ok"
        except sqlite3.DatabaseError as exc:
            raise OneLibraryError(DAMAGED.format(exc)) from None
        if not healthy:
            raise OneLibraryError(DAMAGED.format("its tables do not check out"))
        connection.row_factory = sqlite3.Row
        yield connection
    finally:
        connection.close()


def summary(root: str | os.PathLike) -> dict:
    """Device name, database version, track and playlist counts, and the USB colour setting."""
    with open_library(root) as connection:
        try:
            row = connection.execute("SELECT deviceName, dbVersion, backGroundColorType FROM property").fetchone()
            tracks, = connection.execute("SELECT count(*) FROM content").fetchone()
            playlists, = connection.execute("SELECT count(*) FROM playlist WHERE attribute IS NOT 1").fetchone()
        except sqlite3.Error as exc:
            raise OneLibraryError(DAMAGED.format(exc)) from None
    return {"device_name": row["deviceName"] if row else None, "db_version": row["dbVersion"] if row else None,
            "tracks": tracks, "playlists": playlists,
            "background_colour_type": row["backGroundColorType"] if row else None}


def tracks_with_comment_containing(connection: sqlite3.Connection, text: str) -> list[dict]:
    """Tracks whose djComment contains text, with artist, artwork path and the playlists that hold them.

    Root playlists have playlist_id_parent 0, not NULL, and folders have attribute 1.
    Folders hold no tracks, so only playlist names are listed.
    """
    try:
        rows = connection.execute(
            "SELECT c.content_id, c.title, a.name AS artist, c.path, c.djComment, i.path AS image_path"
            " FROM content c LEFT JOIN artist a ON a.artist_id = c.artist_id_artist"
            " LEFT JOIN image i ON i.image_id = c.image_id"
            " WHERE instr(coalesce(c.djComment, ''), ?) > 0 ORDER BY c.content_id",
            (text,)).fetchall()
        tracks = {row["content_id"]: {**dict(row), "artist": row["artist"] or "", "playlists": []} for row in rows}
        for content_id, name in connection.execute(
                "SELECT pc.content_id, p.name FROM playlist_content pc JOIN playlist p ON p.playlist_id = pc.playlist_id"
                " WHERE p.attribute IS NOT 1 ORDER BY p.name"):
            if content_id in tracks:
                tracks[content_id]["playlists"].append(name)
    except sqlite3.Error as exc:
        raise OneLibraryError(DAMAGED.format(exc)) from None
    return list(tracks.values())
