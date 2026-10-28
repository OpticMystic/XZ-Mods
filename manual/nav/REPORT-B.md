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
