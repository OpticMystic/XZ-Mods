# Chapter A host capture report

This branch adds scripted excerpts, not complete action-level acceptance. Two
four-deck headings still have no flow because the software-player transport
adapter is unfinished. That is host work, not a platform limit.

## Per-chapter assignment status

Counts include existing flows from the base and every level-two/three heading.
They count assignments, not fully covered procedures.

| Chapter | Assigned | Unassigned | Reasons |
|---|---:|---:|---|
| four-decks | 10/16 | 6 | 3 physical USB, 1 informational, 2 host implementation |
| play-screen | 11/11 | 0 | Assigned excerpts only |
| performance | 13/16 | 3 | Informational readout/fallback descriptions |
| stems | 12/13 | 1 | Desktop preparation and USB copy |
| browse | 13/14 | 1 | Closed stock CLASSIC browser |

## Remaining host acceptance work

- `four-decks#what-decks-3-and-4-can-do`: actual software-player pad/transport
  callbacks and state readback. Native cue callbacks do not prove software-deck
  performance controls.
- `four-decks#sync-on-decks-3-and-4`: SYNC off/on, QUANTIZE, master changes,
  tempo-only and phase-lock transitions through the actual software player.
- The assigned MODERN four-deck screen excerpt selects a lane and toggles stems;
  it does not open the software-deck detail panel. CLASSIC does open both detail
  panels through `touch_hook`, but its header, side column and bottom panels are
  still photographed native pixels. Full CLASSIC composition remains host work.
- The top-bar excerpt uses INFO and STEMS. The native mode-lamp setters are not
  exhaustively exercised. Browse settings selects sort and key policies; actual
  column-toggle rows still need a separate flow. USB-stem setup exercises local
  readiness snapshots on deck 1, not real file decoding or audio alignment.
- Feature setup confirms the UI's acknowledgement step. The separate gate/smart
  flow exercises the real cue machine with local stock-playback callbacks. It
  does not establish that the user heard audio, or that a device live test passed.

## Changes and verification

The 24 `nav-a-*` flows add route-state motion, real RESCAN, deck-order choices,
a deck-3 LOAD cell, INFO/SOURCE/UTILITY, zoom and time, SCREEN choices, CLASSIC
waveform choices and detail-panel navigation, gate/smart setup and cue callbacks,
a refused native jump, RAM-only presentation, six stem layouts, four-deck stem
visibility, USB-stem readiness and browse/source selection.

New observation requirements check CLASSIC panel selection, four-deck stem
visibility and deck-3 load readback. Deck-specific action requirements prevent
a left-side LOAD to deck 1 from passing as a deck-3 recovery. The screenshot
review caught incorrect native stem capabilities on the deck-3 error fixture;
that fixture now removes them before invoking CHOOSE TRACK.

The new adapters are host-only headers. Small dispatch additions stay in the
existing capture/runtime files. Production source bytes, source lists, chapters,
UI files, versions and release files are unchanged. ARM evidence is recorded in
DEVICE-BUILD.md. There was no XZ connection, USB write, network protocol work,
public push or publishing.

## Retained proof

Final source-built batch: `E:/Claude/xz-beta-rc-scratch/nav-capture-a/`.
It contains 24 flows, 467 PNG frames, a contact sheet, timelines, frame-duration
files, lossless WebPs and the source/output manifest. `--check` passes for
`--flow 'nav-a-*'`.

All 300 distinct PNG hashes were inspected. The first 15 review sheets cover
294 distinct images from the preceding candidate batch; the final delta sheet
covers all 13 new hashes. Seven earlier hashes are absent from the final batch.
Identical hashes reuse review. Final receipt:
`E:/Claude/xz-beta-rc-scratch/nav-proof-a-final/reviewed.json`.

Negative checks rejected changed source, corrupt PNG bytes, removal of required
CLASSIC panel observations, and a deck-3 load action changed to deck 1. Evidence
was restored byte-for-byte and `--check` passed again. Receipt:
`E:/Claude/xz-beta-rc-scratch/nav-proof-a-final/negative-checks.json`.
Python compilation, the 181-heading inventory check and `git diff --check` pass.
The candidate capture directories ending in `superseded-1`, `superseded-2` and
`superseded-3` are retained for diagnosis, not accepted capture evidence.
