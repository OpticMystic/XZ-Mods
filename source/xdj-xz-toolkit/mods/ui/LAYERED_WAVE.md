# Native layered scrolling waveform

The XDJ-XZ draws each scrolling-waveform column as one solid colour. This mod
replaces that column with independent layers at independent heights, like the
CDJ-3000, in one of two looks chosen in MODS > Settings > SCROLLING WAVEFORM:

- **3-BAND** draws rekordbox's own PWV7 low/mid/high analysis as stacked layers:
  white highs at the core, amber mids around them, blue lows outside. Each layer
  is as thick as its band, so the outline is low+mid+high.
- **STEMS** stacks drums (outside), harmonics and vocals (core) the same way, from
  the per-stem analysis an OverCue bundle carries (`stems-*-waveform.2EX`, each
  stem's three bands summed), in the active theme's stem pad colours. Tracks
  without stems fall back to 3-BAND.
- **STOCK WAVE** leaves the native renderer alone.

No preparation step. Both looks read analysis rekordbox or the stem builder
already put on the USB, and nothing is written.

## Data

`layered_wave_source.c` runs on a background worker only.

- 3-band: the deck reader's path is split at `/Contents/`. The first lookup on
  a USB reads the PPTH of every `PIONEER/USBANLZ/*/*/ANLZ0000.DAT` (EXT if no
  DAT) and caches path hash to folder. The match is re-verified by reading that
  PPTH again and comparing case-insensitively. A miss rebuilds the index at most
  once per 15 s. PWV7 is then read from `ANLZ0000.2EX`, taking the entry offset
  from the section's own header length (24 bytes for PWV7, 20 for PWV6).
- Stems: `xz_oc_bundle()` resolves the bundle folder from `CDJMODS/index.json`
  without opening or hashing audio.
- Normalization is the 99.5th percentile of low+mid+high, so a few clipped
  transients do not flatten the track. Zoomed out, a column shows its loudest sample.

Why stacked at equal weight: rekordbox's own overall waveform height (PWV5) was
fitted against PWV7 on five exported tracks. A plain low+mid+high sum correlates
0.87-0.89 with it; linear fits give near-equal coefficients (e.g. 0.175/0.173/0.147).
The first renderer drew independent heights weighted 4/2/1 (correlation 0.68-0.74);
on the deck it read as blue blobs with a thin brown band instead of the Pioneer
look of kick transients with amber mids and a white core.

Measured on the deck (1.26, USB with 303 analysed tracks): cold index build and
3-band load 323 ms, cached 3-band 11 ms, stems 35 ms. Inside a busy rbp the
first cold load took 10.9 s (stem workers and the player share the USB), after
which lookups are cached. "Desire" yields 26,875
samples from both the track and its stems, so the two timelines agree.

## Native boundary

The XDJ-XZ 1.26 function `ui_PlayMode_Wave_SetUpData` is at `0x1fab28`, guard
`f04f2de98052a0e1`. It receives deck, screen column, first sample, end sample in
r0-r3, background mode on the stack and zoom in s0. The wrapper calls stock with
those arguments, then replaces the upper half of that column while the 536x268
surface is locked. The stock caller mirrors the upper half, then draws half-beat
and cue marks and the playhead.

The adapter is compiled separately with `-mfloat-abi=softfp -mfpu=neon` so its
`pcs("aapcs-vfp")` annotation describes the s0 argument; the library keeps its
soft-float ELF ABI.

The draw thread only try-locks the publication mutex and never waits for file
I/O. Player, reader, reader implementation and source path identity are checked
before a frame uses data. Missing data, a changed source, out-of-range samples or
an unexpected native state leave the stock column. `XZ_MODS_LAYERED_WAVE=0`
disables the hook; `XZ_MODS_WAVE_MODE=0|1|2` overrides the saved choice.

## Themes

Native themes remap every wave pixel through a lookup table, which would turn
the CDJ-3000 colours into theme tints. `xz_layered_wave_protect()` marks pixels
that still hold the exact colour this frame drew (cue and beat marks drawn over
them are not marked), the theme pass runs, and `xz_layered_wave_restore()` puts
the marked layer pixels back. Backgrounds and marks stay themed.

## Tools

- `preview_layered_wave.c`: offline render of a USB folder through the
  production loader and renderer to a PPM.
- `probe_layered_wave.c`: on-device, read-only loader timing against the real USB.
- `layered_wave_trial.py`: RAM-only restart of `rbp` onto a build, with an exact
  rollback script beside it.

## Overview

`ui_Counter_FillWaveBuff_New` (0x22ee14, caller 0x22ee4c) fills a 300x34 window per
deck bottom-up from `ui_CntWaveData_RGB`, then draws full-height 0xf800 cue lines
with black borders. Inside that lock `xz_layered_overview()` redraws every column
from the same data as the scrolling wave (sample range x*count/300), leaving cue
columns and their borders alone; the theme pass and restore then run as for the
main wave. The firmware refills it when `ui_WaveUpdateFlag[deck]` and
`ui_CntGetWaveDataFlg[deck]` are set; proof counters [10]/[11] record fills seen
and drawn. If the stock fill runs before the worker has loaded the track, the
overview stays stock until the next refill.
