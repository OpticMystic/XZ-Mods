# Consolidated navigation capture report

The A/B merge preserves both adapters. Coverage is still partial at action level.
The final full source-built batch is `E:/Claude/xz-beta-rc-scratch/nav-capture-4/`.
The full batch contains **167 flows and 3,464 PNG frames**. `--check` passes.
All 1,997 distinct PNG hashes are accounted for by baseline reviews and 45 new
review sheets. The combined receipt has zero unreviewed images:
`E:/Claude/xz-beta-rc-scratch/nav-proof-4-final/all-reviewed.json`.
The early 23 sheets cover 448 distinct hashes; the final 22 sheets cover another
428. Some early hashes do not occur in the final batch; reuse is by hash only.

SECTIONS.json totals: 130 assigned excerpts; 30 desktop platform, 11 informational,
5 USB/boot, 3 physical USB and 2 closed-stock-firmware unassigned headings.
No null row uses needs_host_implementation. The assigned excerpts still have
the explicit host gaps below.

## Final verification receipts

| Retained baseline | Flows | PNGs | Replay differences |
|---|---:|---:|---:|
| nav-capture-3 | 96 | 1277 | 0 |
| nav-capture-a | 24 | 467 | 0 |
| nav-capture-b | 17 | 403 | 0 |

Every prior frame has the same relative name and bytes, with no extra frame in
any baseline flow. Receipt: `E:/Claude/xz-beta-rc-scratch/nav-replay-4.json`.
The discarded merge candidate briefly changed B's deck-error sequence; the
software-deck-only alert-clear correction restores the original ten frames.
Final replay has no pixel difference to explain. Timeline JSON gains the union
of observation/game fields and uses remapped B fixture IDs.

The full source build, `--check`, 181-heading inventory, Python compilation and
`git diff --check` pass. Negative checks reject a corrupt PNG, missing software
player observations and missing game phase even after rehashing the altered
timeline. Original bytes were restored and the full check passed again.
Receipt: `E:/Claude/xz-beta-rc-scratch/nav-negative-4.json`.
The final contact sheet is `E:/Claude/xz-beta-rc-scratch/nav-capture-4/contact-sheet.png`.
Interrupted/superseded merge and C candidate folders are development evidence only.

Local commits before this report: merge `e5604e8`, implementation `0ef0711`.
The branch was not pushed. No hardware or installed-runtime acceptance is claimed.

## Integration

Merge `e5604e8` combines A's performance/classic hooks and observations with B's
setup/game hooks, CHOOSE TRACK, SDK planning and game-state requirements. B's
fixture IDs 200..221 moved to 400..421 because A already used that range.
The filters retain both glob matching and prefix selection. Duplicate stem-setup
cases are one dispatcher with each branch's original callback behavior.
The later correction restricts A's alert-clear callback to software decks, retaining
B's native-deck error sequence. Fixture IDs and added timeline fields change JSON
metadata; the acceptance comparison requires all earlier PNGs to match byte for byte.

## Additional host excerpts

- Both software players load local silent WAV/analysis fixtures through the unchanged
  `extra_player.c`, use its queued input and publication functions, and exercise PLAY,
  CUE, SYNC, QUANTIZE, MASTER, LOOP IN/OUT and RELOOP. Required observations check
  loaded-player and SYNC on/off state; required effects check input dispatch. The fake
  sink is reused from `test_extra_player.c`. No worker or audio device starts. This
  does not prove audible output, fader pickup, all pads, or phase-lock transitions.
- A separate CLASSIC full-composition fixture draws real side columns and bottom
  mixer panels with native shell snapshots and software-player fixture snapshots.
  A's original CLASSIC fixture remains unchanged for replay. Stock pixels outside
  the overlays remain the retained photograph.
- Notice-matrix flows present each troubleshooting table row as a local outcome,
  then invoke real BROWSE and LOAD handlers. These prove message presentation and
  browse/load navigation, not that every backend failure or its individual recovery
  has been reproduced. Paired alternatives in a table row are shown together.
- Legacy locked and damaged profile snapshots use the real DJ PROFILE picker.
- Additional Arcade flows start Solo, Versus, Break Solo and Co-op through actual
  touch handlers, using difficulty choices 0, 1 and 2, then pause. Existing B flows
  retain game inputs and Wave Rider setup. Beat acquisition stays offline.

## Remaining host work

The requested last-gap closure is not complete. These are host gaps, not platform limits:

- MODERN software-detail opening. Shell navigation currently exercises lane selection;
  the existing detail opening adapter uses CLASSIC `touch_hook`. No fabricated MODERN
  transition or new product behavior was added.
- CLASSIC header controls still use the photograph. The new composition adds live
  column/mixer overlays and detail panels, not a complete header reconstruction.
- Exhaustive software-deck pads, tempo pickup, jog/loop edge/release behavior,
  route-loss recovery, phase-lock transitions and rekordbox assignment execution.
- Individual notice causes and appropriate recovery actions beyond browse/load,
  calm loopback-warning composition and stock-warning suppression.
- Long image catalogs, name bounds, changed catalogs after rescan, every settings
  worker result, game powers and beat-source variants.

SECTIONS.json assigns excerpts only. Its null rows are retained only for explicit
platform limits or informational content. Assigned headings can still have the
host action gaps above; heading assignment is not complete procedure acceptance.
Closed stock firmware, real boot/USB persistence, network responses, audio and
computer-app operations need their corresponding platforms. No XZ connection,
USB write, publishing, LINK protocol work or network reverse engineering occurred.
No production source, UI source, chapter, version or release file was edited.

## ARM identity

All 34 tested production ARM objects match baseline `00f16052` byte for byte.
Receipt: `E:/Claude/xz-beta-rc-scratch/nav-arm-objects-4-final.json`.
The 25-object A/B union is extended with software-player, CLASSIC column/mixer,
analysis, key-shift, layered-source and stem-media/decoder objects. This is object
identity, not a linked device build or hardware acceptance.

## Prior branch evidence

The following reports describe the earlier accepted A and B batches. Their remaining
work lists are historical; the current list above states what this pass adds and
what remains. Their numbers describe their separate pre-merge captures.

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

# Chapter B navigation evidence

This pass audits the 35 previously unassigned headings in the seven requested chapters. Assignments remain excerpts, not exhaustive action acceptance. No boot, USB write, player connection, network operation or publication was performed.

| Chapter | Assigned excerpts | Unassigned | Reason |
|---|---:|---:|---|
| getting-started.md | 6 | 3 | go-back-to-stock: usb_or_boot; what-you-need: informational; prepare-a-usb: desktop_platform |
| hardware.md | 7 | 1 | the-top-panel: informational |
| troubleshooting.md | 5 | 3 | e-8307-hubs-are-not-supported: informational; messages-in-the-xz-mods-app: desktop_platform; the-diary-on-your-usb-sticks: informational |
| extras.md | 4 | 1 | what-the-player-sends: informational |
| mods-menu.md | 1 | 1 | older-pages: informational |
| screen-and-themes.md | 1 | 1 | deck-colours: informational |
| connect.md | 1 | 0 | All have host excerpts |

## New flows

Retained source-built batch: `E:/Claude/xz-beta-rc-scratch/nav-capture-b/`. Run the README command with `--flow-prefix b-` for both capture and `--check`. The prefix selects this branch's new scripts without changing the default full-batch gate.

- Beat Arcade and Wave Rider render their actual C game frames, use production encoder target geometry, push, BACK, touch, jog and CUE handlers. Wave Rider walks all four setup steps and enters the ride. Beat Arcade starts a round and changes the scene. The waveform and tempo are deterministic local fixtures, with no source worker, network or audio runtime.
- MY SETTINGS shows pause-before-load, failed load and retry, and a read-only empty stick. Existing `my-settings-load-save` retains the actual two-push save request. The new read-only flow makes no save request.
- Profile rescan presses CHECK USB FOR PROFILES. USB removal at review shows the blocked original save target, then restoring presence permits SAVE TO USB 2. No profile writer runs.
- App setup visits all four steps, changes the start choice, validates RUN with the production SDK command planner, then supplies a separate local RUNNING snapshot before IT WORKS. No child app is launched or shown.
- Stem setup uses actual controls for missing engine, unprepared track, damaged set and ready snapshots. Its callback displays the next-step explanation; CHECK does not run the engine or verify media.
- Deck read error opens CHOOSE TRACK and loads through browse. A play notice similarly leads to browse and reload. Settings-save snapshots show failed, missing, locked and full USB status followed by a setting change and a saved snapshot. These snapshots do not establish persistence.
- Hardware MENU/SOURCE/INFO/SHORTCUT/UTILITY navigation and MASTER TEMPO after KEY SYNC run through existing handlers. Prior stem pad, beat jump, double-SHIFT and encoder flows are assigned to the corresponding hardware headings.

## Remaining action-level host work

The 35 heading labels are resolved, but the owner's complete action-level goal remains unfinished. Do not treat this pass as full chapter acceptance.

- Hardware: software-deck TEMPO pickup, jog/loop edges, transport, LOAD routing after side takeover, route-loss recovery, and the full rekordbox assignment execution matrix still need host input adapters and readback. Existing setting-choice captures prove selection only. Physical pad-light output and closed stock key behavior separately need their platforms.
- Troubleshooting: the full notice matrix and individual recovery actions are not exhausted by the load-error excerpt. The calm CLASSIC loopback-warning composition and actual stock-warning suppression need their corresponding runtime fixtures or stock platform evidence.
- Profile: damaged/legacy profile variants, long image lists, rescan catalog changes, and no-name/full-name bounds remain additional host scenarios.
- Settings: every save-state string and each worker result are not exhausted by the four failure snapshots. The stem-check callback is an explanatory outcome, not production engine verification.
- Games: Co-op/Versus, every difficulty, all powers and beat-source selections are outside these excerpts. Native beat acquisition, audible continuity and jog ownership need runtime/hardware acceptance.
- CLASSIC first-start MODS entry is illustrated through interface and renderer excerpts. Closed native framebuffer/input interception is not emulated.
- Computer profile creation, USB preparation and app troubleshooting need desktop captures; actual boot, USB persistence, network responses and audio need their platforms.

## Previously unassigned headings

| Section | Flows or reason |
|---|---|
| connect#make-a-profile-on-a-computer | b-profile-rescan |
| extras#moving-around-a-game | b-rider, b-arcade |
| extras#beat-arcade | b-arcade |
| extras#wave-rider | b-rider |
| extras#report-an-error-from-the-dock | report-dock-entry |
| extras#what-the-player-sends | informational: Report payload explanation; no player action. |
| getting-started#what-changes-and-what-stays-stock | utility |
| getting-started#go-back-to-stock | usb_or_boot: Power-off and boot with the loader USB absent require a player boot; no host screen owns these actions. |
| getting-started#what-you-need | informational: Requirements table; no player action. |
| getting-started#prepare-a-usb | desktop_platform: Install/write/eject wizard runs on the computer, not the player. |
| getting-started#first-start | b-first-menu |
| getting-started#modern-on-first-start | b-first-menu, deck-view, stem-mute-bypass |
| getting-started#classic-on-first-start | physical-rekordbox, interface-choice |
| getting-started#choose-modern-or-classic | interface-choice |
| getting-started#updating | updates |
| hardware#the-top-panel | informational: Annotated physical control map; per-control actions are assigned under Browse section keys and Deck keys. |
| hardware#browse-section-keys | b-browse-keys, encoder-settings, browse-load |
| hardware#link-and-the-vjtools-view | vj-tools |
| hardware#deck-keys | b-master-tempo, physical-key-sync, physical-double-shift-1, physical-double-shift-2, physical-stem-pads, physical-beat-jump |
| hardware#stem-pads | stem-pads-values, physical-stem-pads |
| hardware#pad-lights | stem-pads-values, physical-pad-mode-pages |
| hardware#decks-3-and-4-from-the-deck-controls | physical-double-shift-1, physical-double-shift-2 |
| hardware#when-a-control-does-not-respond | b-browse-keys, rekordbox-button-values, mods-four-decks |
| mods-menu#saving-to-the-usb | b-save-recovery |
| mods-menu#older-pages | informational: Historical page redirect table; retired pages cannot be opened by a reader. |
| screen-and-themes#deck-colours | informational: Explains fixed badges and colour meanings; no player action. |
| screen-and-themes#when-something-looks-wrong | b-theme-recovery |
| troubleshooting#if-the-player-will-not-start-with-the-usb | updates, b-first-menu |
| troubleshooting#stems-on-the-usb | b-stem-setup |
| troubleshooting#notices-on-the-play-screen | b-notice-load-recovery |
| troubleshooting#errors-on-a-deck | b-deck-error-recovery, report-dock-entry |
| troubleshooting#e-8307-hubs-are-not-supported | informational: Explains automatic warning suppression and the cable card; no player action in this section. Actual cable detection needs hardware. |
| troubleshooting#when-settings-cannot-be-saved | b-save-recovery |
| troubleshooting#messages-in-the-xz-mods-app | desktop_platform: Every action is in the computer app. |
| troubleshooting#the-diary-on-your-usb-sticks | informational: Explains automatically written diagnostic files; no on-player control. Actual USB diary persistence requires USB runtime. |

## Verification receipt

The final source-built batch has 17 flows and 403 PNG frames, timelines and lossless animated WebPs. All 329 distinct PNG hashes were visually inspected, with shared pixels reviewed once. The contact sheet is in the capture root; review sheets and `reviewed.json` are in `E:/Claude/xz-beta-rc-scratch/nav-proof-b/`. The corrected save-state flow holds each snapshot for 500 ms. The long NO SETTINGS USB line clips in the unchanged renderer; this host pass does not alter UI source.

`--check`, Python compilation, section inventory and `git diff --check` pass. Negative checks reject corrupt output, a missing flow, and a missing required game phase even when the timeline output hash is updated. Restored evidence passes. A second source-built Wave Rider capture reproduced all 19 flow files byte for byte. Receipt: `E:/Claude/xz-beta-rc-scratch/nav-negative-proof-b.json`. All 21 before/after ARM objects match baseline; see DEVICE-BUILD.md.

## New flow frame counts

| Flow | PNG frames |
|---|---:|
| b-apps-wizard | 12 |
| b-arcade | 212 |
| b-browse-keys | 11 |
| b-deck-error-recovery | 10 |
| b-first-menu | 16 |
| b-master-tempo | 7 |
| b-my-settings-failure | 5 |
| b-my-settings-pause | 5 |
| b-my-settings-readonly | 5 |
| b-notice-load-recovery | 9 |
| b-profile-rescan | 7 |
| b-profile-usb2-recovery | 20 |
| b-rider | 16 |
| b-save-recovery | 14 |
| b-stem-recovery | 4 |
| b-stem-setup | 12 |
| b-theme-recovery | 38 |
