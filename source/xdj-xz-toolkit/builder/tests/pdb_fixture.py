# SPDX-License-Identifier: MIT
"""Write a minimal Rekordbox export.pdb with only a tracks table, for synthetic USBs.

Layout follows the same format facts as builder/rekordbox_pdb.py: page 0 is the
file header, page 1 an empty index page (real exports start every table with one),
then track data pages chained by next_page.
"""
from __future__ import annotations

import os
from pathlib import Path
import struct

LEN_PAGE = 4096
TRACKS = 0
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


def _track_row(track_id: int, file_path: str) -> bytes:
    empty, name, path = _string(""), _string(file_path.rsplit("/", 1)[-1]), _string(file_path)
    row = bytearray(TRACK_ROW_SIZE)
    struct.pack_into("<H", row, 0, 0x24)
    struct.pack_into("<I", row, TRACK_ID_AT, track_id)
    struct.pack_into("<H", row, 0x5C, 3)
    strings = [TRACK_ROW_SIZE] * 19 + [TRACK_ROW_SIZE + len(empty), TRACK_ROW_SIZE + len(empty) + len(name)]
    struct.pack_into("<21H", row, TRACK_STRINGS_AT, *strings)
    row += empty + name + path
    return bytes(row + bytes(-len(row) % 4))


def _page_header(index: int, next_page: int, rows: int, flags: int, free: int, used: int) -> bytes:
    counts = (rows | rows << 13).to_bytes(3, "little")
    return struct.pack("<4xIIII4x3sBHH", index, TRACKS, next_page, 1, counts, flags, free, used)


def _data_page(index: int, next_page: int, rows: list[bytes]) -> bytes:
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
                               LEN_PAGE - heap - index_bytes, heap - HEAP) + bytes(8)
    return bytes(page)


def _index_page(index: int, next_page: int) -> bytes:
    page = bytearray(LEN_PAGE)
    page[:0x20] = _page_header(index, next_page, 0, INDEX_FLAGS, 0, 0)
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


def write_export(path: str | os.PathLike, tracks: dict[int, str]) -> Path:
    """Write export.pdb at path whose tracks table maps each id to file_path."""
    data_pages = _paginate([_track_row(i, p) for i, p in tracks.items()])
    last = 1 + len(data_pages)
    end = last + 1  # next_page of the final page points past the file, as Rekordbox writes it
    header = bytearray(LEN_PAGE)
    struct.pack_into("<4xIIIIII", header, 0, LEN_PAGE, 1, end, 5, 1, 0)
    struct.pack_into("<IIII", header, 0x1C, TRACKS, end, 1, last)
    pages = [bytes(header), _index_page(1, 2)]
    pages += [_data_page(2 + n, 3 + n, rows) for n, rows in enumerate(data_pages)]
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(b"".join(pages))
    return out
