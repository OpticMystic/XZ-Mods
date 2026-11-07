# Overcue prepared stems on the XDJ-XZ

The adapter reads the user's `CDJMODS/index.json` and prepared bundles.
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

`overcue_file.c` matches the exact exported path, verifies the source SHA-256,
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
python3 mods/ui/verify_runtime_controls.py
```

The prepared audio is a development adapter. Portable tests and isolated ARM
tests do not establish real-song alignment, audible stem separation, seek
continuity, or physical inline-display acceptance. Record those separately.

The optional spare-channel EQ mode uses local mixer ADC reports and the same stem gain path. It follows the USB source and external-channel lockouts described in `../ui/README.md`.
