# Navigation capture implementation report

The third pass is verified and committed as partial coverage. The owner's goal of every player action section is still incomplete. The remaining list below deliberately retains host implementation work. It cannot yet be reduced to platform limits.

## Added in the third pass

- Seventeen flows extend the previous 79-flow batch. The current full capture has 96 flows and 1,277 PNG frames, including 358 frames in the new flows.
- All 24 CLASSIC theme choices are pressed through production widgets. SCREEN and STEMS selection-row tours now include display, waveform and stem-layout controls. LAMPS AND TITLES includes its visibility and USB-colour controls. These tours do not cover every setting or every toggle direction.
- DJ profile flows choose a logo image, show a decoded-image fixture preview, edit with KEEP CURRENT, recover from an unusable logo by choosing NO LOGO, and show save failure followed by a successful new attempt. Visual review caught and corrected the host callback dropping KEEP CURRENT's logo. No image decoder or profile writer runs.
- REPORT THIS enters from the live dock error strip through the real alert handler. Separate flows edit an unsuccessful report and retry one with a changed local callback outcome. Report text and topic changes use production controls.
- STATIC IP enters an invalid review first, then types IP, prefix and gateway through the production keypad, reviews, applies and reverts. A second flow supplies failed local verification and conditional revert. The apply callback first consumes the real painted-review publication with `xz_ui_network_take_apply`.
- Browse panel metadata comes from `nav_browse.c`, a host-only wrapper around unchanged production `shell_browse.c`. Scripts turn the real encoder to reveal rows and tap production geometry. New flows add/remove a track tag, choose sort rows, edit all five INFO rating stars, and issue separate memory-cue and loop load requests. The current sort flow does not yet press the column-toggle rows. Cue/loop requests receive a local notice, not transport execution.
- `SECTIONS.json` inventories all 181 level-two and level-three headings, including informational and desktop headings. Seventy-three headings have excerpt flow assignments; 108 have null assignments with reasons: 73 need host implementation, 27 require the desktop platform, 6 concern USB/boot, and 2 concern closed stock firmware. These are heading assignments, not complete action-level coverage numbers. No informational heading has been silently dropped.
- `tools/nav_sections.py` checks exact heading membership, duplicates, section text hashes and flow references. The driver excludes SECTIONS.json from scripts and runs this check before capture and `--check`.

## Verification and retained evidence

The final full batch is `E:/Claude/xz-beta-rc-scratch/nav-capture-3/`, including `contact-sheet.png`, timelines, duration files and animated WebPs. It was rebuilt from source after correcting logo retention. `--check` passed against that exact batch.

All 144 distinct PNG hashes that differ from `nav-capture-2` were inspected on nine review sheets plus one additional sheet. Receipt: `E:/Claude/xz-beta-rc-scratch/nav-proof-3/reviewed.json`. Identical pixels reuse the same visual review. This includes changed frames in existing flows, not only the 17 new scripts.

A separately rebuilt `profile-logo` capture reproduced all 27 flow files byte-for-byte, including all PNGs, timeline, frame-duration JSON and lossless animation. Stale-source, corrupt-output, missing-flow and missing-section negative checks were rejected; restored evidence passed. Receipt: `E:/Claude/xz-beta-rc-scratch/nav-negative-proof-3.json`. Python compilation and `git diff --check` passed. Existing working-tree CRLF conventions were preserved; Git still normalizes text files according to its existing attributes.

All 13 before/after ARM handler objects are byte-identical to baseline `00f16052`. DEVICE-BUILD.md maps the new seams to objects and records the additional INFO hash. This is object identity, not a linked-device build, installation or hardware acceptance.

## Remaining host implementation work

- Exhaustive chapter-directed settings and values, including stem-order cycles, remaining shell columns, sort readback, every setup wizard, PC-stem/app/game setup and actual host game navigation. Selection-row tours are insufficient for this requirement.
- Remaining DJ-profile error variants, USB-presence changes, damaged/legacy profiles, image paging/rescan and save-to-USB-2 variants. Logo preview is a decoded-image snapshot, not USB decoding acceptance.
- Further static-network scenarios: multiple adapters, priority reordering with an actual changed order, route/gateway validation, loader support, changed-generation refusal, cancellation and all failure results. The current single-adapter priority taps cannot demonstrate reordering.
- Playlist creation/rename/delete and track move/remove callbacks and scripts, actual column toggles, rating zero, MY SETTINGS failure/read-only/pause/confirmation variants, and cue/loop transport snapshot readback. The playlist adapter currently acknowledges a request only; it does not maintain an edited playlist catalog.
- Gate/smart cue transport, software-deck performance-pad playback and quantize modifier variants. Missing native audio is a platform limit; input and UI fixtures for these paths remain host work.
- CLASSIC four-deck wave surfaces, native theme recolouring, key popover interaction and complete overlay runtime hit-routing. Existing deck-picker composition is not the native framebuffer hook.
- Refine the all-heading inventory into individually audited player-action coverage. Existing assigned flows illustrate excerpts, so 73 assigned headings must not be reported as 73 fully covered action sections.

## Platform and scope boundaries

Closed stock firmware owns stock CLASSIC browse/utility/dialog motion. Actual boot/install, USB persistence/media copy, real network responses, remote connections, audio and hardware require their respective platforms. Desktop app chapters need separate desktop captures. No XZ connection, USB write, public push, publication or LINK/PRO DJ LINK/CONTROL MODE protocol work occurred. Superseded third-pass generations are retained in separately named sibling directories and are not current acceptance evidence.
