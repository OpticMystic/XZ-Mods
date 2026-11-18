# Overcue prepared stems on the XDJ-XZ

The adapter reads the user's `CDJMODS/index.json` and prepared four-file and legacy seven-file bundles.
It does not use private Overcue source or CDJ-3000 firmware.

OverCue Desktop 2.2.26 and later write four files per track: vocal, drums,
harmonics and full mix. Older bundles add three pair files (instrumental,
vocals-drums, vocals-harmonics). The adapter opens, verifies and pins only the
four required files for both layouts. Every pair is mixed from the singles, as
OverCue firmware 0.13.1 does, so "Remove unused stem files" changes nothing here.
The [published format](https://github.com/OverCue-gg/overcue-stems-format/blob/54a52b8dfeed287af1d79cbf13480ffd3286e11f/FORMAT.md)
now documents four-file banks as `three_part: 2`; `three_part` and the pair hashes
stay optional here. The four required hashes, frame count and page size gate
playback. `index.json` may be up to 16 MiB on the XZ, both for playback and for the
browser's stem map, because OverCue Desktop 2.2.40 can grow it past the 4 MiB that
older OverCue firmware reads. Desktop keeps `index.json` the complete inventory.
The XZ looks tracks up there by exported path, which the format allows, and does not
read the bounded binary `index-v2` that OverCue firmware 0.14.0 uses: that index is
keyed by Rekordbox ID and only speeds up lookup. An index entry without
`sample_rate` is the 96 kHz profile, so 44.1 kHz `OVPGZ004` pages behind it are
refused. The experimental CDJ-2000 WAV profile (`.s16le.wav` roles) is not read;
those tracks play the normal full mix.

When an OverCue-upgraded USB already has `index-v2`, the desktop builder publishes a
new `index-v2` generation with every `index.json` change, so CDJ-3000s on OverCue
firmware 0.14 see the same stems.

`overcue_file.c` first matches the exported path, then falls back to an
unambiguous SHA-256 match of the loaded source when the path is absent.
Different IDs or duplicate export filenames do not require rewriting OverCue
mappings. An invalid exact-path binding is never bypassed. The reader
validates each PGZ page table, and checks every decoded page. Both `OVPGZ003`
lossless FLAC pages and legacy `OVPGZ001` zlib pages decode to the same 96 kHz
stereo signed-16-bit PCM. Each file's magic selects its decoder; `.pgz` filenames
and advisory codec labels do not. Indexed frame counts and hashes are authoritative.
The optional `overcue-manifest.json` remains compatible with older exports and
normalization gains. The desktop checker also verifies complete role hashes.
PWV3 analysis data supplies separate stem waveform amplitudes.

Repacked OverCue Desktop 2.2 exports work without converting or rewriting the USB.
The published format defines both codecs and the unchanged `overcue-index/1` layout. Unknown container
versions, invalid FLAC STREAMINFO, corrupt audio and conflicting indexed identities
are rejected. All decoding remains on the existing background worker, so the
audio callback's ownership, timeline alignment and safe fallback stay unchanged.

`overcue_stream.c` keeps two bounded PCM windows. File reads, decompression,
hashing, and alignment run on its worker. The audio callback does not allocate,
read files, lock a mutex, or wait. Unchanged full-mix playback retains the native
samples. A pending new selection keeps a covered previous mix or native
audio until its cache is ready. Once a mute is applied, a cache miss cannot
restore that stem. If all matching cache coverage is lost, the remaining
native-source contribution is retained and prepared contributions wait for data.
Any selection reads at most two singles, because the quietest stem is carried by
the native source. Windows follow the playing direction. Four consecutive backward
blocks place new windows behind the playhead, so reverse playback refills about as
often as forward playback. A changed selection while reversing waits for prepared
audio behind the playhead, not ahead of it.
Only the owned background workers use CPUs 1-2; native audio affinity is unchanged.
Two additional two-second PCM windows retain the current and pending mix
around the native loop start. A mix change does not evict the previous loop audio.
Each cache slot has reader/writer ownership; callbacks only read a pinned slot
and never wait for disk or the worker. This allows immediate loop returns while
the forward windows refill. A loop changed or recalled without preparation time
can still require buffering; the hardware acceptance must check for audible gaps.

## Stem store and the shared pool

Next to the windows, each stream builds a store (`overcue_flat.h`): the three singles as 16-bit PCM at the deck's
reader rate with the measured alignment applied, so the audio callback mixes by deck frame with no decode and no
wait. A track is cut into segments of 32 chunks of 16,384 frames (11.9 s at 44.1 kHz). The memory behind a segment
is one 6 MiB extent of a single stem pool shared by both decks (`stem_pool.c`): at most 56 extents, the stem part of
the address zone, each a RAM-disk file written before it is mapped. `XZ_MODS_STEM_POOL_EXTENTS=<n>` lowers the limit.

The pool's arbiter (`stem_arbiter.h`, a pure function) shares the extents. A stream is PRELOAD once aligned, and
ENGAGED while any stem level of its deck is off unity and for 120 s after the levels return to unity. Each asks for
a floor of 8 extents (95 s); an engaged deck whose track fits is held whole, the one that engaged first keeps it;
what is left raises the smallest grants first. Two five-and-a-half-minute tracks are 28 extents each; a premix gets
what the other deck's track leaves. A store granted less than its track holds a bubble: the playhead's segment
and the next one in the playing direction, the anchors (the track start and the loop start; cue point and hot cues
come through `xz_audio_set_anchors` once their reading is confirmed), then segments ahead at cost d and behind at
3d, so about three quarters of a bubble lie ahead. The bubble slides one segment per worker pass. A grant below
two segments is no store, unless the track has only one segment. Where the bubble does not reach, the windows play.

The store's reader protocol: the audio callback counts itself in the store's busy count for the whole block and
reads only through chunk bases it loaded inside. The worker clears a segment's pointer, waits until it sees busy at
zero (at most 200 ms, else it keeps the segment and tries later), clears the segment's ready flags and only then
gives the extent back. `tests/test_stem_protocol.c` reads through recycled extents under ThreadSanitizer, and fails
when that wait is removed.

Tracks first: a refused track RAM copy lowers the pool's limit 8 extents below what is mapped for 30 s, and the
stores give back their least wanted segments. A store whose grant falls below two segments gives up everything, but
keeps the playhead's segment and the next one until the windows hold the playhead, so a deck with stems in use plays
on from the windows instead of waiting for a refill. A refused extent (RAM ledger, RAM-disk space or file, address zone)
holds the limit at what is mapped for 5 s and is logged once per reason as `STEMS store refused`. With store first
(the default on the unit) the store builds from the stem files on the stick; if the pool grants too little for a
store for 30 s, the stem files are copied to RAM and the windows play from the copy while the store keeps asking.
The stem files stay open when the store is complete, because a segment given back is decoded again; a RAM copy,
once made, stays for the same reason (it replaced the stick file).

Status per deck (`xz_mods_stem_status_json_v1`): `flat:[built,chunks,ms]` and
`store:{held,grant,need,class,lead_ms,behind_ms}`, with lead and behind the built audio ahead of and behind the
playhead in the playing direction; `stems_ram` counts the RAM copy and the held extents. The RAM card says the
prepared stems are in RAM only when nothing more is read from the stick for them.

Prepared files are not assumed to align from metadata alone. The worker compares
native source samples with the prepared full mix over a bounded search. It
requires correlation above 0.995, an unambiguous peak, and a consistent result in
two separate sections. Fractional sample offsets are retained. Until then,
native audio continues and the UI reports alignment pending.

OverCue's MP3 timing corrections (`source_timing_policy: native-mp3-header/1` in
Desktop 2.2.5/2.2.6, and the FFmpeg-encoded MP3 realignment in 2.2.40/2.2.41) move
the prepared timeline by an MP3 metadata frame and encoder delay. That is tens of
milliseconds, well inside the measured search of 8,192 native frames each way.
Old, regenerated and realigned bundles therefore all align without reading the
policy field. The stream test checks a 2,257-frame offset at 44.1 kHz.

The native playback strip uses the qualified 536x268 waveform window. Both
scrolling waveforms retain their cue bands; a 64-row strip adds deck selection,
bypass, three toggles, levels, and prepared waveform overviews. Native waveform
touches use the inverse coordinate mapping. Captured strip gestures cannot leak
into stock controls, and a track change resets gains and cancels old gestures.

The STEMS button next to MODS toggles strip visibility without changing audio or
pad ownership. With stems enabled, HOT CUE A/B/C toggle Vocal/Harmonics/Drums,
and D toggles bypass on that physical deck. The configured additional pad page
also works. Either deck remains independent of panel visibility and displayed
deck. Turning the audio STEMS setting off restores normal cue dispatch.
The strip follows the native active deck after prepared stems become available;
manual selection lasts until native focus or the focused track changes.

The native draw hook has an observation mode, `XZ_MODS_INLINE=observe`. Verify
its exact caller, scene, dimensions, pitch, and lifecycle on hardware before
claiming the inline screen works. `xz_inline_proof_v1` and
`xz_prepared_proof_v1` expose bounded read-only diagnostic counters.

## Verification

```powershell
python mods/audio/tests/verify_overcue.py --zig C:\path\to\zig.exe
```

On Linux:

```sh
python3 mods/audio/tests/verify_overcue.py --cc gcc --sanitizer address
python3 mods/audio/tests/verify_overcue.py --cc 'gcc -fno-pie -no-pie' --sanitizer thread
python3 mods/audio/tests/verify_runtime.py
python3 mods/audio/tests/verify_stem_pool.py --cc gcc
python3 mods/ui/verify_runtime_controls.py
```

The prepared audio is a development adapter. Portable tests and isolated ARM
tests do not establish real-song alignment, audible stem separation, seek
continuity, or physical inline-display acceptance. Record those separately.

The optional spare-channel EQ mode uses local mixer ADC reports and the same stem gain path. It follows the USB source and external-channel lockouts described in `../ui/README.md`.
