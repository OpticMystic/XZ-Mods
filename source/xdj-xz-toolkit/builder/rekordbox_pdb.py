# SPDX-License-Identifier: MIT
"""Read track ids and file paths from a Rekordbox Device Library export.pdb.

Written from Deep Symmetry's format analysis:
https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html
All integers are little-endian. Only data pages of the tracks table are read.
"""
from __future__ import annotations

import os
from pathlib import Path
import struct
import unicodedata

EXPORT_PDB = ("PIONEER", "rekordbox", "export.pdb")
MAX_PDB_BYTES = 1 << 30

# File header: 4 zero bytes, then len_page and num_tables; table pointers start at 0x1c.
FILE_HEADER = struct.Struct("<4xII")
TABLES_AT = 0x1C
TABLE = struct.Struct("<I4xII")  # type, (empty_candidate), first_page, last_page
TRACKS = 0

# Page header: page_index, type, next_page, then at 0x18 three bytes packing
# num_row_offsets (low 13 bits) and num_rows (high 11 bits), then page_flags.
PAGE_HEADER = struct.Struct("<4xIII8x3sB")
ROW_OFFSETS_MASK = 0x1FFF
INDEX_PAGE = 0x40  # page_flags bit set on index pages, which hold no rows
HEAP = 0x28  # row offsets count from the end of the 0x20 header + 8-byte data header

# Row index grows backwards from the page end, 0x24 bytes per group of 16 rows:
# group g ends at len_page - g*0x24 with u2 txn flags, u2 presence flags, then
# u2 row offsets 0..15 going down.
GROUP_SIZE = 0x24
GROUP_ROWS = 16
PRESENT_BACK = 4
OFFSET_BACK = 6

# Track row: id at 0x48, then 21 u2 string offsets (relative to the row) at 0x5e.
TRACK_ID = struct.Struct("<I")
TRACK_ID_AT = 0x48
TRACK_STRINGS = struct.Struct("<21H")
TRACK_STRINGS_AT = 0x5E
FILE_PATH = 20

# DeviceSQL string kinds: odd = short ASCII whose kind>>1 is the whole field length;
# long forms carry u2 total length + pad byte before the text.
LONG_HEADER = struct.Struct("<xHx")
LONG_ASCII = 0x40
LONG_UTF16LE = 0x90

NO_EXPORT = "This USB has no Rekordbox export. Export the track with Rekordbox first."
NOT_EXPORTED = "Rekordbox has not exported this track to this USB. Export it with Rekordbox first."
AMBIGUOUS = ("The Rekordbox export on this USB lists this file more than once. "
             "Re-export the track with Rekordbox and try again.")
UNREADABLE = "The Rekordbox export on this USB is unreadable. Export the track with Rekordbox again."


class _Unreadable(Exception):
    pass


def _need(condition: bool) -> None:
    if not condition:
        raise _Unreadable


def _string(page: bytes, at: int) -> str:
    _need(at < len(page))
    kind = page[at]
    if kind & 1:
        size, start, encoding = kind >> 1, at + 1, "ascii"
    elif kind in (LONG_ASCII, LONG_UTF16LE):
        size, = LONG_HEADER.unpack_from(page, at)
        start, encoding = at + LONG_HEADER.size, "ascii" if kind == LONG_ASCII else "utf-16-le"
    else:
        raise _Unreadable
    _need(size >= start - at and at + size <= len(page))
    return page[start:at + size].decode(encoding)


def _row_positions(page: bytes, offsets: int):
    """Yield (id, file_path) for every present track row in one data page."""
    groups = -(-offsets // GROUP_ROWS)
    # struct.unpack_from reads negative offsets from the end, so bound the index explicitly.
    _need(HEAP + groups * GROUP_SIZE <= len(page))
    # Iterate by num_row_offsets, not num_rows: deleted rows leave gaps and later groups.
    for slot in range(offsets):
        group, bit = divmod(slot, GROUP_ROWS)
        end = len(page) - group * GROUP_SIZE
        present, = struct.unpack_from("<H", page, end - PRESENT_BACK)
        if not present >> bit & 1:
            continue
        row = HEAP + struct.unpack_from("<H", page, end - OFFSET_BACK - 2 * bit)[0]
        yield row


def _page_rows(page: bytes, offsets: int):
    for row in _row_positions(page, offsets):
        track_id, = TRACK_ID.unpack_from(page, row + TRACK_ID_AT)
        path_at = row + TRACK_STRINGS.unpack_from(page, row + TRACK_STRINGS_AT)[FILE_PATH]
        yield track_id, _string(page, path_at)


def _track_paths(data: bytes) -> dict[int, str]:
    len_page, num_tables = FILE_HEADER.unpack_from(data)
    _need(HEAP + GROUP_SIZE <= len_page <= 0x10000 and TABLES_AT + num_tables * TABLE.size <= len_page)
    tables = [TABLE.unpack_from(data, TABLES_AT + i * TABLE.size) for i in range(num_tables)]
    tracks = [(first, last) for kind, first, last in tables if kind == TRACKS]
    _need(len(tracks) == 1)
    index, last = tracks[0]
    pages = len(data) // len_page
    visited, paths = set(), {}
    while True:
        # A chain that leaves the file, revisits a page, or wanders into another table is corrupt.
        _need(0 < index < pages and index not in visited)
        visited.add(index)
        page = data[index * len_page:(index + 1) * len_page]
        page_index, kind, next_page, counts, flags = PAGE_HEADER.unpack_from(page)
        _need(page_index == index and kind == TRACKS)
        if not flags & INDEX_PAGE:
            for track_id, path in _page_rows(page, int.from_bytes(counts, "little") & ROW_OFFSETS_MASK):
                _need(paths.setdefault(track_id, path) == path)
        if index == last:
            return paths
        index = next_page


def track_paths(pdb: str | os.PathLike) -> dict[int, str]:
    """Map every track row id to its file_path exactly as stored in export.pdb."""
    path = Path(pdb)
    if path.stat().st_size > MAX_PDB_BYTES:
        raise ValueError(UNREADABLE)
    try:
        return _track_paths(path.read_bytes())
    except (_Unreadable, struct.error, UnicodeDecodeError):
        raise ValueError(UNREADABLE) from None


def find_track(usb_root: str | os.PathLike, file_path: str) -> tuple[int, str]:
    """Return (track id, stored path) for a /Contents/... path on a Rekordbox USB.

    Exact match first, then one match after NFC normalization and casefolding: FAT is
    case-insensitive, and exporters and file pickers may compose accents differently.
    """
    pdb = Path(usb_root, *EXPORT_PDB)
    if not pdb.is_file():
        raise ValueError(NO_EXPORT)
    rows = track_paths(pdb).items()
    def folded(text):
        return unicodedata.normalize("NFC", text).casefold()
    wanted = folded(file_path)
    matches = ([row for row in rows if row[1] == file_path]
               or [row for row in rows if folded(row[1]) == wanted])
    if not matches:
        raise ValueError(NOT_EXPORTED)
    if len(matches) > 1:
        raise ValueError(AMBIGUOUS)
    return matches[0]


def library_tracks(pdb):
    """Read track display metadata and artist names from the Device Library tables.

    Field offsets follow Deep Symmetry's rekordbox_pdb.ksy track_row and artist_row.
    """
    path=Path(pdb)
    if path.stat().st_size>MAX_PDB_BYTES:raise ValueError(UNREADABLE)
    data=path.read_bytes()
    try:
        paths=_track_paths(data)
        length,count=FILE_HEADER.unpack_from(data)
        artists={};details={}
        for i in range(count):
            kind,index,last=TABLE.unpack_from(data,TABLES_AT+i*TABLE.size)
            if kind not in (0,2):continue
            visited=set()
            while True:
                _need(0<index<len(data)//length and index not in visited);visited.add(index)
                page=data[index*length:(index+1)*length]
                number,page_kind,next_page,counts,flags=PAGE_HEADER.unpack_from(page)
                _need(number==index and page_kind==kind)
                if not flags&INDEX_PAGE:
                    for row in _row_positions(page,int.from_bytes(counts,'little')&ROW_OFFSETS_MASK):
                        if kind==2:
                            subtype=struct.unpack_from('<H',page,row)[0]
                            key=struct.unpack_from('<I',page,row+4)[0]
                            offset=struct.unpack_from('<H',page,row+10)[0] if subtype&4 else page[row+9]
                            artists[key]=_string(page,row+offset)
                        else:
                            key=TRACK_ID.unpack_from(page,row+TRACK_ID_AT)[0]
                            offsets=TRACK_STRINGS.unpack_from(page,row+TRACK_STRINGS_AT)
                            details[key]={'title':_string(page,row+offsets[17]),'artist_id':struct.unpack_from('<I',page,row+0x44)[0],
                                'bpm':struct.unpack_from('<I',page,row+0x38)[0]/100,'duration':struct.unpack_from('<H',page,row+0x54)[0],
                                'sample_rate':struct.unpack_from('<I',page,row+8)[0]}
                if index==last:break
                index=next_page
        return [{'id':key,'file_path':file_path,'title':details[key]['title'] or Path(file_path).stem,
                 'artist':artists.get(details[key]['artist_id'],''),**{k:details[key][k] for k in ('bpm','duration','sample_rate')}} for key,file_path in paths.items()]
    except (_Unreadable,struct.error,UnicodeDecodeError,KeyError):raise ValueError(UNREADABLE) from None
