# Navigation capture implementation report

The host capture harness works and captures the flows listed in INDEX.md. The full request to capture every manual navigation is **not complete**. The chapter audit exposes the remaining assignments instead of treating page tours as exhaustive coverage.

## What runs

`mods/shell/nav_capture.c` links the actual shell and MODS renderer/input source lists. `tools/capture_manual_nav.py` builds it with Zig, runs JSON scripts, records deduplicated 800x480 PNGs, timelines, hold durations, lossless animated WebPs, a contact sheet, and a source/output hash manifest. There are no changes to production handlers or device build inputs. No existing tracked file was edited.

Captured paths include all seven MODS sections, MENU open/close, encoder focus and activation, CLASSIC theme selection, interface choice in SCREEN, browse/load, search keyboard, list menus, filter, INFO/SOURCE, SHORTCUT/UTILITY, NEEDLE LOCK, deck-view selection, stem mute/BYPASS, stem-pad configuration, beat-jump page selection, key shift/KEY SYNC, RAM copy request with explicit fixture completion, report topic/text-skip/review, and the available connection/extras setup landing pages.

The driver checks expected page IDs on important links. Required-action checks cover LOAD, DECK LAYOUT, STEM MUTE/BYPASS, KEY SHIFT/SYNC, and NEEDLE LOCK. Complete render calls publish input geometry before later events. Frame deduplication preserves the control timeline even when a detent does not change pixels. Fixture state is deterministic and local.

## Remaining implementation work

These paths are possible with more host adapters or scripts and must not be described as platform limitations:

- The physical rekordbox button above LINK switching interfaces. SCREEN's CLASSIC/MODERN buttons are captured; the runtime's `rekordbox_run_locked` hook is not linked into this host program.
- Physical SHIFT, performance-pad, double-SHIFT deck-side switching, pad-page changes on the play screen, gate/smart cue playback, and quantize controls. Their engine/native input paths need a host-callable adapter. The MODS setup pages are captured, not those physical gestures.
- Every value on every MODS page, complete DJ profile keyboard/logo/save/switch flows, feature setup wizards, network setup/review steps, PC-stem setup, app setup steps, and game setup/run navigation. Current scripts show their landing pages or selected excerpts.
- Full MODERN browse category/folder, search-result filtering, column/sort mutation, playlist editing, tag editing, cue/loop loading, MY SETTINGS load/save, and rating workflows. Current fixture callbacks do not implement those backend operations. Search intentionally keeps a deterministic fixture result list rather than asserting catalog query correctness.
- Report typing, SEND, saved/sent/error results, dock REPORT THIS, and retries. The report script reaches READY TO SEND and sends nothing.
- CLASSIC deck-view/key overlays and additional theme-family overlay compositions. Only CLASSIC inline stems and the MODS theme picker are currently captured.

`PLAYLIST` and `SEARCH` are shell semantic-key injections in the current scripts. They are not complete demonstrations of how a finger or physical BROWSE hold reaches those routes on the device. Do not use those timeline labels as physical-button animation instructions without replacing these entry steps. Key events record activation plus dwell, not physical release edges. See README.md for the exact timing contract.

## Unavailable or out of scope

- Stock CLASSIC browsing, stock utility, stock dialogs, and moving stock waveforms belong to closed firmware. The retained CLASSIC background is a still capture. Only our overlays and MODS pages can run here.
- Boot/install/update, USB insertion/removal, actual RAM copying, USB writes, network responses, remote app connections, audio, and hardware acceptance are not exercised. Some UI snapshots can be scripted later, but they cannot prove those operations.
- Desktop app chapters require a separate app capture task.
- No LINK, PRO DJ LINK, or CONTROL MODE protocol work was performed.

## Gate handoff

Run the driver's `--check` to compare current source hashes, the complete script set, and retained output hashes. Stale or missing flows must fail a future manual gate. Regenerate, inspect, then accept a new navigation manifest separately from static figures. The present checker proves integrity and freshness, not full manual coverage. A gate that requires complete navigation coverage must also reject the unfinished assignments above and in CHAPTER-AUDIT.md. `build_manual.py` was not modified.

## Verification retained in this session

All 49 scripts completed with 450 unique frames. All frames were visually inspected on six sequence sheets under `E:/Claude/xz-beta-rc-scratch/nav-final-proof/`, with additional full-size inspection of the deck picker. Strict Zig compilation passed. Required-action and expected-page assertions passed. Timeline timestamps are monotonic; every PNG reference exists; every saved image has a positive hold; hold durations sum to the recorded flow duration. The source/output freshness check passed. A deliberate source-hash mismatch was rejected. A second deck-view capture produced byte-identical PNGs, timeline, frame-duration file, and animated WebP.

The capture batch is under `E:/Claude/xz-beta-rc-scratch/nav-capture/`. Earlier scratch passes were retained separately. No controller/USB access, installation, publication, or push was performed.
