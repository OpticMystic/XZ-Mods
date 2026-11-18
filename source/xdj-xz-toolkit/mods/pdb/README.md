# DeviceSQL reader

Pure C11, read-only `PIONEER/rekordbox/export.pdb` reader. No filesystem,
network, device, UI, allocator, locale, or thread dependencies in `pdb.c`.

## API

`pdb_open` puts the reader in a caller-owned arena and accepts an exact-read
callback, user pointer and 64-bit file size. The file must stay immutable.
`pdb_each_track`, `pdb_each_playlist` and `pdb_each_history` stream rows to
callbacks. Text is UTF-8 with explicit byte lengths and an extra terminating
NUL. Embedded NULs remain part of the explicit length. Callback text expires
when the callback returns. Operations on the same reader are not reentrant.

Tracks expose title, artist, album, genre, key, label, BPM x100, seconds,
rating, colour ID/name, album track number, path, artwork ID/path, date added
and bitrate, plus related IDs and sample rate. Missing side-table IDs resolve
to empty text. Stored empty titles remain empty.

`pdb_playlist_entries` returns one playlist's entries sorted by position,
preserving repeated tracks. Pass `history=1` for history entries.
`pdb_build_index` returns one entry per track for ARTIST, ALBUM, TRACK,
GENRE, KEY, BPM, RATING or COLOR. Text categories sort by Unicode casefolded
name, group ID, title and track ID. BPM, RATING and COLOR sort by numeric
value/ID, then title and track ID. Adjacent group IDs form category groups.
This does not claim to reproduce the stock player's locale collation.

`pdb_search` returns track IDs in database order. Every whitespace-delimited
term must occur in title, artist or album; terms may match different fields.
It uses Unicode default casefold, including expanding folds such as `ß` to
`ss`, without accent stripping or Unicode normalization. Invalid UTF-8
queries return `PDB_EINVAL`. Empty/whitespace queries match every track.
`unicode_fold.h` is checked in; `generate_unicode.py` regenerates it from
Python's Unicode data without network access.

Index, entry and search results occupy the arena until `pdb_reset` to a
previous `pdb_mark`. On failure they roll back and publish no partial result.
`pdb_memory_used` reports retained arena bytes, excluding temporary callback
text. `PDB_ENOMEM` means the requested operation does not fit; streaming
does not require retaining every track.

## Bounds and format coverage

No `malloc`, including indirect sorting allocations. Two page buffers, three
page bitmaps, 64 fixed ID-to-row lookup cache slots, and the reader occupy
the arena. With 4 KiB pages and the generated fixture the 64-bit host retains
10,180 bytes before results. Bitmaps cost `3 * ceil(file_pages / 8)` bytes;
page buffers cost `2 * page_size`. Temporary decoded strings and retained
results share the remainder. Stack use is fixed; sorting is iterative.
The tests use 512 KiB. Arena addresses need not be aligned. Large files or
results may need a larger arena; insufficient space never truncates strings.
Lookup caches contain validated row locations and negative lookups, so cache
eviction costs another side-table scan rather than losing metadata.

Reads check file bounds, page identity/type, linked-page cycles, backwards
row indexes, presence masks, row boundaries, string headers and UTF-16
surrogates. A malformed row invalidates its whole page before any callback
sees that page. A sound header's next link allows later pages to be read;
an unreadable/wrong-table header ends that chain. Stats count distinct corrupt
pages, including page zero for invalid chain references, and I/O errors.

Parsed tables: tracks 0, genres 1, artists 2, albums 3, labels 4, keys 5,
colours 6, playlist tree 7, playlist entries 8, history playlists 11,
history entries 12 and artwork 13. Unknown tables are ignored. History sync
table 19, `exportExt.pdb`, My Tags, analysis files, audio and artwork bytes are
not parsed. Database pages must be a power of two from 512 to 65,536 bytes.

Format facts follow [Deep Symmetry's analysis](https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html)
and [schema](https://github.com/Deep-Symmetry/crate-digger/blob/master/src/main/kaitai/rekordbox_pdb.ksy).
Local cross-checks use XZ-Mods `builder/rekordbox_pdb.py` and the MIT
`mods/key/catalog.c` page/string rules. Track artist/ID offsets are
`0x44/0x48`, matching the schema and Python reader. The older catalog's
`0x48/0x4c` pair is not used here.

## Verification

From the repository root:

```powershell
python -B packages/xdj-xz-toolkit/mods/pdb/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
```

Python 3 and `pyelftools` are required. The reference reader path defaults to
the sibling XZ-Mods checkout; override with `--reference`. Add read-only
databases with repeated `--pdb` options. All generated files, compiler caches
and probe logs stay under `.build/` in this directory.

The verifier builds warning-free host C11 tests with live assertions, rejects
`NDEBUG`, generates clean/corrupt/16 KiB-page fixtures, compares all seven
shared fields and track paths against Python, and tests two local snapshots
when present: `E:/Codex/xz-library-repair-20261014/original/` (301 tracks) and
`E:/Codex/xz-live-stems-20261014/opticmystic-before/` (51 tracks). These are
read-only local snapshot inputs; no live USB is accessed or bundled.
Generated fixtures cover far artist/album offsets, deleted rows across row
groups, long ASCII, accented/Japanese UTF-16, surrogate pairs, folders,
playlists, repeated entries and a corrupt page followed by a valid page.

It runs 2,000 mutations/truncations on each clean/corrupt fixture, probes
whether host ASan/UBSan instrumentation actually works, and otherwise builds
`PDB_BOUNDS_CHECK` to assert page/string integer reads plus callback bounds,
arena guards and UTF-8 output. A separate sanitizer pass is claimed only if
the deliberate failing probe reports instrumentation. Fuzzing is a smoke
test, not a proof against every corrupt input.

ARM compilation matches `mods/build.py`: `-target arm-linux-gnueabi.2.13
-mcpu=cortex_a9 -O2 -std=c11 -Wall -Wextra -Werror`, soft-float ABI.
The verifier checks both an object and a linked shared library; the latter's
ELF architecture, ABI, undefined imports and GLIBC versions are recorded in
`.build/arm-symbols.json`. No symbol newer than GLIBC 2.4 is allowed.
There is no deck integration or hardware acceptance in this module.
