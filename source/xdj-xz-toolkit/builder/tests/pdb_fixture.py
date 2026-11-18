# SPDX-License-Identifier: MIT
"""Write a minimal Rekordbox export.pdb for synthetic USBs: a tracks table and, on request,
artists, playlist tree and playlist entries tables.

Layout follows the same format facts as builder/rekordbox_pdb.py: page 0 is the
file header, then per table an empty index page (real exports start every table
with one) followed by its data pages chained by next_page.
"""
from __future__ import annotations

import os
from pathlib import Path
import struct

LEN_PAGE = 4096
TRACKS = 0
ARTISTS = 2
PLAYLIST_TREE = 7
PLAYLIST_ENTRIES = 8
HEAP = 0x28
GROUP_SIZE = 0x24
GROUP_ROWS = 16
DATA_FLAGS = 0x24
INDEX_FLAGS = 0x64
TRACK_ROW_SIZE = 0x88
TRACK_ID_AT = 0x48
TRACK_STRINGS_AT = 0x5E
EMPTY_INDEX_ENTRY = 0x1FFFFFF8


def _string(text: str) -> bytes:
    if not text.isascii():
        data = text.encode("utf-16-le")
        return struct.pack("<BHx", 0x90, len(data) + 4) + data
    data = text.encode("ascii")
    if len(data) <= 126:
        return bytes([(len(data) + 1) << 1 | 1]) + data
    return struct.pack("<BHx", 0x40, len(data) + 4) + data


def _pad(row: bytearray) -> bytes:
    return bytes(row + bytes(-len(row) % 4))


def _track_row(track_id: int, file_path: str, comment: str = "", title: str = "", artist_id: int = 0) -> bytes:
    strings = [""] * 21
    strings[16], strings[17], strings[19], strings[20] = comment, title, file_path.rsplit("/", 1)[-1], file_path
    row = bytearray(TRACK_ROW_SIZE)
    struct.pack_into("<H", row, 0, 0x24)
    struct.pack_into("<I", row, 0x44, artist_id)
    struct.pack_into("<I", row, TRACK_ID_AT, track_id)
    struct.pack_into("<H", row, 0x5C, 3)
    # Empty fields share one empty string, as Rekordbox writes them.
    offsets, heap = [], _string("")
    for text in strings:
        offsets.append(TRACK_ROW_SIZE + (len(heap) if text else 0))
        heap += _string(text) if text else b""
    struct.pack_into("<21H", row, TRACK_STRINGS_AT, *offsets)
    return _pad(row + heap)


def _artist_row(artist_id: int, name: str) -> bytes:
    return _pad(bytearray(struct.pack("<HHIBB", 0x60, 0, artist_id, 3, 10)) + _string(name))


def _playlist_row(playlist_id: int, parent_id: int, name: str, folder: bool, order: int) -> bytes:
    return _pad(bytearray(struct.pack("<IIIII", parent_id, 0, order, playlist_id, int(folder))) + _string(name))


def _entry_row(entry_index: int, track_id: int, playlist_id: int) -> bytes:
    return struct.pack("<III", entry_index, track_id, playlist_id)


def _page_header(index: int, next_page: int, rows: int, flags: int, free: int, used: int, kind: int = TRACKS) -> bytes:
    counts = (rows | rows << 13).to_bytes(3, "little")
    return struct.pack("<4xIIII4x3sBHH", index, kind, next_page, 1, counts, flags, free, used)


def _data_page(index: int, next_page: int, rows: list[bytes], kind: int = TRACKS) -> bytes:
    page = bytearray(LEN_PAGE)
    heap = HEAP
    for slot, row in enumerate(rows):
        group, bit = divmod(slot, GROUP_ROWS)
        end = LEN_PAGE - group * GROUP_SIZE
        struct.pack_into("<H", page, end - 6 - 2 * bit, heap - HEAP)
        page[heap:heap + len(row)] = row
        heap += len(row)
    for group in range(-(-len(rows) // GROUP_ROWS)):
        present = (1 << min(GROUP_ROWS, len(rows) - group * GROUP_ROWS)) - 1
        struct.pack_into("<H", page, LEN_PAGE - group * GROUP_SIZE - 4, present)
    index_bytes = -(-len(rows) // GROUP_ROWS) * GROUP_SIZE
    page[:HEAP] = _page_header(index, next_page, len(rows), DATA_FLAGS,
                               LEN_PAGE - heap - index_bytes, heap - HEAP, kind) + bytes(8)
    return bytes(page)


def _index_page(index: int, next_page: int, kind: int = TRACKS) -> bytes:
    page = bytearray(LEN_PAGE)
    page[:0x20] = _page_header(index, next_page, 0, INDEX_FLAGS, 0, 0, kind)
    page[0x20:0x3C] = struct.pack("<HHHHIIQHH", 0x1FFF, 0x1FFF, 0x03EC, 0, index, next_page,
                                  0x03FFFFFF, 0, 0x1FFF)
    entries = (LEN_PAGE - 0x3C - 20) // 4
    page[0x3C:0x3C + entries * 4] = struct.pack("<I", EMPTY_INDEX_ENTRY) * entries
    return bytes(page)


def _paginate(rows: list[bytes]) -> list[list[bytes]]:
    pages, current, used = [], [], HEAP
    for row in rows:
        groups = -(-(len(current) + 1) // GROUP_ROWS)
        if current and used + len(row) > LEN_PAGE - groups * GROUP_SIZE:
            pages.append(current)
            current, used = [], HEAP
        if HEAP + len(row) > LEN_PAGE - GROUP_SIZE:
            raise ValueError("Track row does not fit in one page")
        current.append(row)
        used += len(row)
    return pages + [current]


def write_export(path: str | os.PathLike, tracks: dict[int, str], *, comments: dict[int, str] | None = None,
                 titles: dict[int, str] | None = None, artists: dict[int, str] | None = None,
                 track_artists: dict[int, int] | None = None,
                 playlists: list[tuple[int, int, str, bool, list[int]]] | None = None) -> Path:
    """Write export.pdb at path whose tracks table maps each id to file_path.

    With playlists, also write the playlist tree and entries tables from
    (id, parent_id, name, is_folder, track ids) tuples; with artists, an artists table.
    """
    comments, titles, track_artists = comments or {}, titles or {}, track_artists or {}
    tables = [(TRACKS, [_track_row(i, p, comments.get(i, ""), titles.get(i, ""), track_artists.get(i, 0))
                        for i, p in tracks.items()])]
    if artists is not None:
        tables.append((ARTISTS, [_artist_row(i, name) for i, name in artists.items()]))
    if playlists is not None:
        tables.append((PLAYLIST_TREE, [_playlist_row(i, parent, name, folder, order)
                                       for order, (i, parent, name, folder, _) in enumerate(playlists)]))
        tables.append((PLAYLIST_ENTRIES, [_entry_row(n, track, i) for i, _, _, _, members in playlists
                                          for n, track in enumerate(members, 1)]))
    chains = [(kind, _paginate(rows)) for kind, rows in tables]
    end = 1 + sum(1 + len(pages) for _, pages in chains)
    header = bytearray(LEN_PAGE)
    struct.pack_into("<4xIIIIII", header, 0, LEN_PAGE, len(chains), end + len(chains) - 1, 5, 1, 0)
    pages, first = [], 1
    for number, (kind, data_pages) in enumerate(chains):
        last = first + len(data_pages)
        # next_page of each table's final page points past the file, as Rekordbox writes it
        empty = end + number
        struct.pack_into("<IIII", header, 0x1C + 16 * number, kind, empty, first, last)
        pages.append(_index_page(first, first + 1, kind))
        pages += [_data_page(first + 1 + n, first + 2 + n if first + 1 + n < last else empty, rows, kind)
                  for n, rows in enumerate(data_pages)]
        first = last + 1
    pages.insert(0, bytes(header))
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(b"".join(pages))
    return out
