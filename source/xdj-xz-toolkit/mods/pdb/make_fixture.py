"""Build deterministic DeviceSQL fixtures; no media or device access."""
from pathlib import Path
import argparse
import struct

PAGE = 4096

def put(row, offset, fmt, value):
    struct.pack_into("<" + fmt, row, offset, value)

def string(text, mode=None):
    if mode == "wide" or not text.isascii():
        raw = text.encode("utf-16-le")
        return b"\x90" + struct.pack("<H", len(raw) + 4) + b"\0" + raw
    raw = text.encode("ascii")
    if mode == "long" or len(raw) > 126:
        return b"\x40" + struct.pack("<H", len(raw) + 4) + b"\0" + raw
    return bytes([(len(raw) + 1) * 2 + 1]) + raw

def named(kind, ident, text, far=False):
    if kind in (2, 3):
        base, ident_at, near_at = (12 if far else 10, 4, 9) if kind == 2 else (24 if far else 22, 12, 21)
        row = bytearray(300 if far else base)
        put(row, 0, "H", (0x60 if kind == 2 else 0x80) | (4 if far else 0))
        put(row, ident_at, "I", ident)
        put(row, base - 2 if far else near_at, "H" if far else "B", len(row))
    elif kind == 6:
        row = bytearray(8)
        put(row, 5, "H", ident)
    else:
        row = bytearray(8 if kind == 5 else 4)
        put(row, 0, "I", ident)
        if kind == 5:
            put(row, 4, "I", ident)
    return bytes(row) + string(text)

TRACKS = [
    dict(id=101, title="Alpha Night", artist=1, album=10, genre=20, key=30, color=1,
         path="/Contents/Alpha Night.mp3", bpm=12800, duration=240, rating=5, number=1, artwork=40),
    dict(id=202, title="Été 東京 🎵", artist=2, album=11, genre=21, key=31, color=2,
         path="/Contents/Été 東京 🎵.flac", bpm=12345, duration=301, rating=3, number=2, artwork=41),
    dict(id=303, title="beta " + "extended mix " * 12, artist=1, album=10, genre=20, key=30, color=1,
         path="/Contents/beta.wav", bpm=9000, duration=99, rating=0, number=3, artwork=0),
    dict(id=404, title="Straße", artist=999, album=0, genre=0, key=0, color=0,
         path="/Contents/Deep/Cuts/strasse.mp3", bpm=12800, duration=180, rating=4, number=4, artwork=0),
]

def track(t):
    row = bytearray(0x88)
    for at, fmt, value in [
        (0, "H", 0x24), (8, "I", 48000), (0x1c, "I", t["artwork"]), (0x20, "I", t["key"]),
        (0x28, "I", 50), (0x30, "I", 320), (0x34, "I", t["number"]), (0x38, "I", t["bpm"]),
        (0x3c, "I", t["genre"]), (0x40, "I", t["album"]), (0x44, "I", t["artist"]),
        (0x48, "I", t["id"]), (0x54, "H", t["duration"]), (0x58, "B", t["color"]), (0x59, "B", t["rating"]),
    ]:
        put(row, at, fmt, value)
    for i in range(21):
        value = {10: "2026-10-18", 17: t["title"], 20: t["path"]}.get(i, "")
        put(row, 0x5e + i * 2, "H", len(row))
        row += string(value, "long" if i == 20 and t["id"] == 101 else None)
    return row

def playlist(ident, parent, order, folder, text):
    return struct.pack("<IIIII", parent, 0, order, ident, folder) + string(text)

def page(number, kind, rows, next_page=0, deleted=(), index=False):
    buf = bytearray(PAGE)
    struct.pack_into("<III", buf, 4, number, kind, next_page)
    put(buf, 24, "I", len(rows) | (len(rows) - len(deleted)) << 13)
    buf[27] = 0x40 if index else 0x24
    used = 40
    groups = (len(rows) + 15) // 16
    for slot, row in enumerate(rows):
        end = PAGE - slot // 16 * 36
        if slot not in deleted:
            flags, = struct.unpack_from("<H", buf, end - 4)
            put(buf, end - 4, "H", flags | 1 << (slot % 16))
        put(buf, end - 6 - slot % 16 * 2, "H", used - 40)
        assert used + len(row) <= PAGE - groups * 36
        buf[used:used + len(row)] = row
        used += len(row)
    put(buf, 30, "H", used - 40)
    return buf

def make(corrupt=False, page_size=PAGE):
    global PAGE
    PAGE = page_size
    pages = [bytearray(PAGE)]
    tables = []
    for kind, rows in [
        (0, [track(t) for t in TRACKS]),
        (1, [named(1, 20, "House"), named(1, 21, "Électronique")]),
        (2, [named(2, 1, "Night Artist"), named(2, 2, "Björk 日本", True)]),
        (3, [named(3, 10, "First Album"), named(3, 11, "夏 Album", True)]),
        (4, [named(4, 50, "Independent")]),
        (5, [named(5, 30, "8A"), named(5, 31, "F♯m")]),
        (6, [named(6, 1, "Red"), named(6, 2, "青")]),
        (7, [playlist(1000, 0, 1, 1, "Sets"), playlist(1001, 1000, 2, 1, "夏"),
             playlist(2000, 1000, 3, 0, "Opening"), playlist(2001, 1001, 1, 0, "日本"),
             playlist(2002, 0, 4, 0, "Empty")]),
        (8, [struct.pack("<III", order, tid, pid) for order, tid, pid in
             [(3, 101, 2000), (1, 202, 2000), (2, 303, 2000), (1, 202, 2001), (4, 101, 2000)]]),
        (11, [named(11, 3000, "HISTORY 001")]),
        (12, [struct.pack("<III", 202, 3000, 2), struct.pack("<III", 101, 3000, 1)]),
        (13, [named(13, 40, "/PIONEER/Artwork/a.jpg"), named(13, 41, "/PIONEER/Artwork/夏.jpg")]),
    ]:
        first = len(pages)
        pages.append(page(first, kind, [], first + 1, index=True))
        # Deleted slots span two groups, including slot 16. Do not parse their garbage.
        if kind == 0:
            spaced = [b"\xfe"] * 18
            for slot, row in zip((0, 4, 16, 17), rows):
                spaced[slot] = row
            live = {0, 4, 16} if corrupt else {0, 4, 16, 17}
            pages.append(page(first + 1, kind, spaced, deleted=set(range(18)) - live))
            if corrupt:
                put(pages[-1], 12, "I", first + 2)
                bad_track = track(dict(TRACKS[0], id=9999))
                put(bad_track, 0x5e + 17 * 2, "H", 65535)
                # A valid row before a bad one must not escape page validation.
                pages.append(page(first + 2, kind, [track(dict(TRACKS[0], id=8888)), bad_track], first + 3))
                # Track 404 exists only after the bad page, proving recovery.
                pages.append(page(first + 3, kind, [track(TRACKS[-1])]))
        else:
            pages.append(page(first + 1, kind, rows))
        tables.append((kind, first, len(pages) - 1))
    struct.pack_into("<III", pages[0], 4, PAGE, len(tables), len(pages))
    for i, (kind, first, last) in enumerate(tables):
        struct.pack_into("<IIII", pages[0], 28 + i * 16, kind, 0, first, last)
    return b"".join(pages)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--corrupt", action="store_true")
    args = parser.parse_args()
    args.output.write_bytes(make(args.corrupt))

if __name__ == "__main__":
    main()
