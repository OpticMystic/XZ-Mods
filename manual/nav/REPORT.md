# Navigation capture implementation report

This revision expands the real C host capture and replaces the nonphysical entry injections. The full request for every relevant chapter operation remains incomplete. Remaining script work is listed separately from platform limits below.

## Added and corrected

- SEARCH enters through physical BROWSE held for one second. PLAYLIST enters through BROWSE, six encoder detents from TRACK to PLAYLIST, then encoder push. The driver rejects PLAYLIST, SEARCH and MODS as direct script keys.
- `shell/nav_runtime.c` includes the existing runtime test fixture, with its test main renamed and never called. Physical mode buttons, pads, SHIFT+SYNC and the rekordbox interface action enter the real `ui_runtime.c` handlers. SHIFT edges enter `xz_deck_select_shift`. The shell handles QUANTIZE. Press/release events name the deck side and physical pad A-H, mode button, SHIFT or SYNC. The rekordbox button is identified as the button above LINK.
- Double SHIFT on both sides, both BEAT JUMP pages, all eight jump pads, stem pads and BYPASS, KEY SYNC and interface switching have dedicated scripts. KEY SYNC fails the capture unless the runtime engages its sync state. Beat-jump requests and mute changes are recorded as handler effects.
- Nine value-control tours exercise the supported setting controls on their actual pages. They call the production runtime action handler for the supported actions. They are not an exhaustive walk of every setting and every possible value.
- DJ profile creation types a name and tagline, chooses NO LOGO, saves two profiles and switches the active profile using local callbacks. No profile file is written.
- Report flows type text, review and SEND, with local SENT, SAVED/OFFLINE and UNSAVED/FULL callbacks. Nothing is uploaded or written to a stick.
- Network inventory is a local `fixture0` snapshot. The review flow chooses KEEP CURRENT and DHCP and visits the rendered review; it does not apply OS changes.
- Browse fixtures implement category/folder callbacks and deterministic title-substring search results. MY SETTINGS has local load/save callbacks. RAM copy has a fixture error result in addition to completion.
- CLASSIC compositions use retained stock pixels with the real inline stem, key-strip and deck-view renderers. STANDARD, LCARS, MATRIX and ANALOG have dedicated picker flows. Stock pixels remain a still capture; they are not closed-firmware execution.

## Device identity evidence

No production source or source-list file changed. All new adapters are host-only files, selected explicitly by the capture driver, outside the device source lists.

`tools/verify_nav_device_objects.py` compares source bytes with baseline `00f16052`, compiles baseline and current ARM objects using Zig's `arm-linux-gnueabi.2.13`, Cortex-A9, O2, PIC and hidden visibility flags, and rejects any byte difference. All 12 comparisons passed:

- SHIFT/deck-side seam: `deck_select.c`.
- Pad/mode/BEAT JUMP seam: `ui/stem_pads.c`, `ui/beat_jump.c`, `ui_runtime.c`.
- KEY SYNC and rekordbox seam: `ui_runtime.c`.
- QUANTIZE and browse seams: `shell/shell.c`, `shell/shell_browse.c`.
- CLASSIC overlays: `ui/deck_view.c`, `ui/key_strip.c`.
- Profile/report/network fixture boundaries: `ui/ui_profile.c`, `ui/ui_support.c`, `ui/ui_network.c`.
- Deterministic host clock: `doom/native_bridge.c` remains byte-identical; `shell/nav_clock.c` replaces only the host clock symbol.

The exact before/after SHA-256 values are retained in `E:/Claude/xz-beta-rc-scratch/nav-arm-objects.json`. This is object identity evidence, not a full linked-runtime build, installation, USB or hardware acceptance.

## Remaining host implementation work

These are unfinished tasks, not platform limitations:

- Exhaustive chapter-directed values, including shell look/columns/sort, every theme and colour choice, every setup wizard, PC-stem/app/game setup and game navigation. Value tours only cover the adapter's explicit supported action set.
- Profile logo-file selection and preview, profile save failures, report dock entry and retry/edit recovery. Current profile flows choose NO LOGO; report result fixtures complete immediately.
- Full static-IP keyboard/review/apply/rollback UI scenarios and network error results. Current inventory and review are local examples.
- Playlist/tag/rating edits, cue/loop loading and full MY SETTINGS failure/confirmation variants. Search is a fixture query, not catalog-backend acceptance.
- Gate/smart cue transport, software-deck performance-pad playback and quantize modifier variants. The native stock audio backend is absent, but further host input fixtures could still demonstrate the UI paths.
- CLASSIC four-deck native wave surfaces, additional native theme recolouring, key popover interaction and complete overlay runtime hit-routing. The current CLASSIC picker adapter uses the shared production geometry/draw helpers, not the native stock framebuffer hook.

## Platform and scope boundaries

Closed firmware owns stock CLASSIC browse/utility/dialog motion. Actual boot/install, USB changes/writes, RAM media copy, network responses, remote connections, audio and hardware require their respective platforms. Desktop app chapters need a separate desktop capture task. No controller connection, USB write, publication, LINK/PRO DJ LINK/CONTROL MODE protocol work occurred.

## Capture verification

The batch is under `E:/Claude/xz-beta-rc-scratch/nav-capture-2/`. INDEX.md records the final counts. The driver checks source/output hashes, exact script membership, physical entry-key rules, required handler effects, monotonic timestamps, positive frame holds and duration totals. Reused development binaries cannot pass `--check`. The host runtime uses the script clock; no runtime threads or clients are started.

Final verification: 79 flows completed with 908 PNG frames, including 30 new flows with 446 frames. All final PNGs match visually reviewed image hashes, covering 472 distinct PNG hashes across the review sheets and corrected browse-menu review. A repeated physical KEY SYNC capture produced byte-identical PNGs, timeline, frame-duration file and animated WebP. The full `--check` passed; deliberate stale-source and corrupt-output manifest hashes were rejected, and the restored manifest passed again. Python compilation and `git diff --check` passed. Existing tracked files retain CRLF line endings.

Review sheets and hash receipt: `E:/Claude/xz-beta-rc-scratch/nav-proof-2/`, `nav-proof-remaining/`, `nav-browse-menu-review.png` and `nav-final-review.json`. Capture integrity evidence: `nav-negative-proof.json` and `nav-determinism-proof.json` in the same scratch parent. Failed intermediate generations are retained as `nav-capture-2-attempt1`, `attempt2` and `attempt3`; only `nav-capture-2` is the accepted current batch.
