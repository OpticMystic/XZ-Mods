# Capture player navigation

Run from the XZ-Mods worktree. Install Pillow in the Python environment first.

```powershell
python tools/capture_manual_nav.py --zig C:/Users/short/Github/VJToolsLibrary/.tmp/cdj-zig/ziglang/zig.exe --out E:/Claude/xz-beta-rc-scratch/nav-capture
python tools/capture_manual_nav.py --zig C:/Users/short/Github/VJToolsLibrary/.tmp/cdj-zig/ziglang/zig.exe --out E:/Claude/xz-beta-rc-scratch/nav-capture --check
```

Use a fresh output directory for rendering. Existing flow directories are refused to prevent mixed generations. Add `--flow deck-view` to capture one script. Use the same filter when checking a single-flow output.

Each JSON script describes controls, optional expected page IDs, and required emitted actions. `select` turns the real encoder handler one detent at a time until its focused label matches, then pushes it. `tap` resolves the current real widget geometry and sends separate down/up events. `turn`, `push`, `key`, `back`, `touch`, and `wait` also work directly. This driver does not calculate navigation state. A missing label, unreachable encoder target, unexpected page, or missing required action fails the run.

`fixture` changes a runtime snapshot, never navigation state. Currently 1 selects CLASSIC inline stem fixtures; 2 supplies RAM-copy completion. Those events use `fixture runtime` as their control in the timeline. The host has no player, USB, network client, settings writer, or audio backend.

The host program links the union of `SHELL_SOURCES` and `UI_SOURCES`. Inputs enter `xz_shell_input`, `xz_ui_encoder_turn`, `xz_ui_encoder_push`, `xz_ui_touch`, `xz_ui_back`, or `xz_ui_inline_touch`. MENU uses the shared `xz_menu_rule` table. Shell rendering uses `xz_shell_render_frame` so painted geometry is published before input. Production source files are unchanged. The name `nav_capture.c` deliberately avoids the existing `preview_*.c` glob, which expects standalone figure programs.

## Timing contract

The timeline clock starts at 0; the C monotonic clock starts at 100000 ms because zero means unpainted in some input guards. An initial screen holds for 500 ms. Touch presses last 120 ms, followed by 400 ms of viewing time. Encoder pushes last 120 ms plus 380 ms of viewing time. Push handlers themselves atomically synthesize activation; the recorded release is for the physical animation and causes no second activation. Detents are 160 ms apart for label selection and 200 ms apart for explicit turns. Key events are activation timestamps followed by a 500 ms dwell; their release edges are not modeled. Every flow ends with a 1000 ms hold.

The host redraws complete C frames after each event and at the shell's 12 ms live-frame cadence during waits. This is a deterministic host presentation clock, not an emulation of Linux scheduling, native framebuffer delivery, audio progression, or scanout. Fixture playback positions remain fixed. The real renderer still animates time-dependent lamps and pictures. Redraws at the same timestamp share the last presented image. Every saved frame has a positive hold. Adjacent identical pixels share a PNG; events still retain their timestamps. `frames.json` records the duration of each unique frame. The lossless animated WebP uses those durations. Use `timeline.json` and `frames.json` for Blender synchronization; WebP players may round short delays.

Each timeline entry includes `t_ms`, the printed control label, action, detail, current PNG, interface page ID, and emitted typed handler actions. `clock` entries represent redraws. Typed actions have `host`, numeric enum `kind`, `deck`, `index`, and `value`. Resolve enum IDs against the source version recorded in the manifest. They show requests made by the UI; fixture callbacks supply only the modeled outcomes. They do not prove backend success.

CLASSIC inline frames use `xz_ui_inline_render` and the retained `manual/captures/classic-play.png` at the same 131,18 origin as the existing preview composition. Stock background pixels remain a still photograph. MODS itself can be rendered in CLASSIC themes without closed firmware.

## Future manual gate

No changes were made to `build_manual.py`, figure acceptance, or the release gate. A future integration should run this driver's `--check` on the retained output before accepting navigation clips. Reject missing scripts, missing outputs, changed source hashes, changed chapter/script hashes, and mismatched output hashes. The manifest includes renderer sources, local headers, fixtures, source-list modules, the driver, scripts, chapter files, the CLASSIC background, and toolchain versions. It excludes timestamps and absolute checkout paths.

Regenerate stale flows, inspect their frames and timelines, then accept their manifest separately from static figure acceptance. The current conservative manifest watches all chapters and scripts for every flow; it invalidates the batch as a unit. Do not make a green hash check mean chapter coverage is complete. `INDEX.md` and `CHAPTER-AUDIT.md` retain the capture backlog. A release gate must reject that backlog if complete navigation coverage is required.
