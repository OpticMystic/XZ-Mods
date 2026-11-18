# Media decoder cache regression

Run from the toolkit root or from `audio/tests` with Python 3, a Linux/WSL C compiler and FFmpeg available on PATH:

```sh
python3 audio/tests/verify_media_cache.py --out /tmp/xz-media-cache-results
python3 audio/tests/verify_media_cache.py --sanitize --out /tmp/xz-media-cache-asan
```

The runner builds host executables with `cc`. Select GCC or a host-compatible Zig compiler with `--cc gcc` or `--cc 'zig cc'`. Windows users can run these commands through WSL Ubuntu. Native Windows execution and cross-target execution are not covered by this runner.

`--source /path/to/mods` or `--source /path/to/mods/audio` selects a readonly source directory. The default is the installed runner's parent `audio` directory. Decoder sources are copied into the result directory before compilation; input source files are never changed. No VJ.Tools checkout, installed application, controller, network connection or external helper script is required. Without `--out`, successful runs remove their temporary build and fixtures.

The routine suite generates only twelve-second deterministic stereo/transient CBR and VBR MP3 files. It checks all returned PCM exactly against an independent sequential decoder, including the first sample after random seeks, encoder-delay/frame boundaries, EOF, negative ranges and reload. Both trimmed and untrimmed modes run. Valid APEv1, APEv2 with a header and APEv2 without a header must retain identical PCM. An oversized malformed footer must produce finite output or a reported error and clean close/reopen.

The failed-seek test reports two failures after physically moving the decoder, then verifies exact PCM on the first retry. The allocation test forces both the seek-index and PCM-cache allocations to fail and verifies exact fallback PCM. Decoder objects compile once and are reused across these test executables. `--sanitize` enables ASan/UBSan for decoder objects and tests. Longer tracks, resource measurements, MT stress and real audio acceptance remain separate checks.

## Negative controls

Use the original decoder source to reproduce the APE-tail failure:

```sh
python3 audio/tests/verify_media_cache.py \
  --source /path/to/original/mods --expect-failure ape-tail \
  --out /tmp/xz-media-cache-ape-red
```

Use the first cache/index candidate before physical-cursor invalidation to reproduce the retry failure:

```sh
python3 audio/tests/verify_media_cache.py \
  --source /path/to/cache-candidate-before-recovery/mods \
  --expect-failure seek-cursor --out /tmp/xz-media-cache-cursor-red
```

A negative control passes only when its selected regression fails with the expected exit code. Do not combine negative controls with `--sanitize`: the old regression exits early before normal test cleanup. The fixed source must pass the ordinary suite instead.

## Patch provenance and rebuild

The production patch is based on the decoder at XZ-Mods `59dcd60`. The local `dr_mp3.h` remains version 0.7.4 with its existing public-domain/MIT license text. The changes are local correctness fixes, not an upstream version update:

- Record validated located-header PCM duration for reservoir-starved preroll frames, populate synthesis history during preroll, and restore the correct raw PCM position.
- Parse APE size from footer bytes 12-15, including the footer and excluding an optional header. Validate version, header/footer consistency, flags, reserved bytes and size/seek bounds. Retain headerless APEv1.
- Use a 2,048-8,192-point index and a fixed, lazy 512 KiB source-PCM cache. Failed reads/seeks invalidate the physical cursor; previously valid cached tiles remain usable.

Both `drmp3` and `drmp3dec_frame_info` change layout. Rebuild `stem_mp3.c`, `stem_media.c` and every object that includes `dr_mp3.h` together. Mixing old objects with the new header is invalid. The `stem_media.h` API is unchanged.

[FFmpeg's APE parser](https://www.ffmpeg.org/doxygen/7.1/apetag_8c_source.html) is the primary reference for APE size and flag interpretation. Bit 31 denotes a header; bit 30 denotes an absent footer.
