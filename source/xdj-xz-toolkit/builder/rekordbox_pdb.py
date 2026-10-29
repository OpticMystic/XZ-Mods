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
ARTISTS = 2
PLAYLIST_TREE = 7
PLAYLIST_ENTRIES = 8
ARTWORK = 13

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
COMMENT = 16
TITLE = 17
FILE_PATH = 20
ARTWORK_ID_AT = 0x1C
ARTIST_ID_AT = 0x44

# Artist row: subtype, index_shift, id, then a u1 name offset at 9, or a u2 one at 10 when subtype bit 2 is set.
ARTIST_ROW = struct.Struct("<H2xI")
ARTIST_FAR_NAME = 4
# Playlist tree row: parent_id (0 at the root), sort_order, id, raw_is_folder, then the name string.
PLAYLIST_TREE_ROW = struct.Struct("<I4xIII")
PLAYLIST_NAME_AT = 0x14
# Playlist entry row: entry_index, track_id, playlist_id.
PLAYLIST_ENTRY_ROW = struct.Struct("<III")
# Artwork row: id, then the path string.
ARTWORK_ROW = struct.Struct("<I")

# DeviceSQL string kinds: odd = short ASCII whose kind>>1 is the whole field length;
# long forms carry u2 total length + pad byte before the text.
LONG_HEADER = struct.Struct("<xHx")
LONG_ASCII = 0x40
LONG_UTF16LE = 0x90

NO_EXPORT = "This USB has no exported music library. Export the track to it from your DJ software first."
NOT_EXPORTED = "This track is not in the library exported to this USB. Export it from your DJ software first."
AMBIGUOUS = ("The library exported to this USB lists this file more than once. "
             "Export the track again from your DJ software and try again.")
UNREADABLE = "The library exported to this USB is unreadable. Export the track again from your DJ software."


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


def _tables(data: bytes) -> tuple[int, dict[int, tuple[int, int]]]:
    """Page length and {table type: (first page, last page)}."""
    len_page, num_tables = FILE_HEADER.unpack_from(data)
    _need(HEAP + GROUP_SIZE <= len_page <= 0x10000 and TABLES_AT + num_tables * TABLE.size <= len_page)
    tables = {}
    for i in range(num_tables):
        kind, first, last = TABLE.unpack_from(data, TABLES_AT + i * TABLE.size)
        _need(kind not in tables)
        tables[kind] = first, last
    return len_page, tables


def _rows(data: bytes, len_page: int, kind: int, chain: tuple[int, int]):
    """Yield (page, row offset) for every present row on one table's page chain."""
    index, last = chain
    pages = len(data) // len_page
    visited = set()
    while True:
        # A chain that leaves the file, revisits a page, or wanders into another table is corrupt.
        _need(0 < index < pages and index not in visited)
        visited.add(index)
        page = data[index * len_page:(index + 1) * len_page]
        page_index, page_kind, next_page, counts, flags = PAGE_HEADER.unpack_from(page)
        _need(page_index == index and page_kind == kind)
        if not flags & INDEX_PAGE:
            for row in _row_positions(page, int.from_bytes(counts, "little") & ROW_OFFSETS_MASK):
                yield page, row
        if index == last:
            return
        index = next_page


def _track_string(page: bytes, row: int, field: int) -> str:
    return _string(page, row + TRACK_STRINGS.unpack_from(page, row + TRACK_STRINGS_AT)[field])


def _track_paths(data: bytes) -> dict[int, str]:
    len_page, tables = _tables(data)
    _need(TRACKS in tables)
    paths = {}
    for page, row in _rows(data, len_page, TRACKS, tables[TRACKS]):
        track_id, = TRACK_ID.unpack_from(page, row + TRACK_ID_AT)
        path = _track_string(page, row, FILE_PATH)
        _need(paths.setdefault(track_id, path) == path)
    return paths


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


def _artist(page: bytes, row: int) -> tuple[int, str]:
    subtype, artist_id = ARTIST_ROW.unpack_from(page, row)
    name_at, = struct.unpack_from("<H", page, row + 10) if subtype & ARTIST_FAR_NAME else struct.unpack_from("<B", page, row + 9)
    return artist_id, _string(page, row + name_at)


def _commented_tracks(data: bytes, text: str) -> list[dict]:
    len_page, tables = _tables(data)
    _need(TRACKS in tables)

    def rows(kind):
        return _rows(data, len_page, kind, tables[kind]) if kind in tables else ()

    tracks = {}
    for page, row in rows(TRACKS):
        comment = _track_string(page, row, COMMENT)
        if text in comment:
            track_id, = TRACK_ID.unpack_from(page, row + TRACK_ID_AT)
            tracks[track_id] = {
                "id": track_id, "title": _track_string(page, row, TITLE), "path": _track_string(page, row, FILE_PATH),
                "comment": comment, "artist_id": struct.unpack_from("<I", page, row + ARTIST_ID_AT)[0],
                "artwork_id": struct.unpack_from("<I", page, row + ARTWORK_ID_AT)[0]}
    if not tracks:
        return []
    artists = dict(_artist(page, row) for page, row in rows(ARTISTS))
    artwork = {ARTWORK_ROW.unpack_from(page, row)[0]: _string(page, row + ARTWORK_ROW.size) for page, row in rows(ARTWORK)}
    playlists = {}
    for page, row in rows(PLAYLIST_TREE):
        _parent, _order, playlist_id, folder = PLAYLIST_TREE_ROW.unpack_from(page, row)
        if not folder:
            playlists[playlist_id] = _string(page, row + PLAYLIST_NAME_AT)
    membership = {}
    for page, row in rows(PLAYLIST_ENTRIES):
        _index, track_id, playlist_id = PLAYLIST_ENTRY_ROW.unpack_from(page, row)
        if track_id in tracks and playlist_id in playlists:
            membership.setdefault(track_id, set()).add(playlist_id)
    result = []
    for track_id, track in tracks.items():
        result.append({"id": track_id, "title": track["title"], "artist": artists.get(track["artist_id"], ""),
                       "path": track["path"], "comment": track["comment"],
                       "image_path": artwork.get(track["artwork_id"]) if track["artwork_id"] else None,
                       "playlists": sorted(playlists[i] for i in membership.get(track_id, ()))})
    return result


def tracks_with_comment_containing(pdb: str | os.PathLike, text: str) -> list[dict]:
    """Tracks whose comment contains text, with artist, artwork path and the playlists that hold them.

    Table types and row layouts follow Deep Symmetry's rekordbox_pdb.ksy (artist_row,
    artwork_row, playlist_tree_row, playlist_entry_row, track_row comment string 16).
    Folders hold no entries, so only playlist names are listed.
    """
    path = Path(pdb)
    if path.stat().st_size > MAX_PDB_BYTES:
        raise ValueError(UNREADABLE)
    try:
        return _commented_tracks(path.read_bytes(), text)
    except (_Unreadable, struct.error, UnicodeDecodeError):
        raise ValueError(UNREADABLE) from None
