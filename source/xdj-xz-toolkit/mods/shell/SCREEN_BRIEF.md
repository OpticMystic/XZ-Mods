# Brief shared by every shell screen-family builder

You build one screen family of the XZ FLAGSHIP shell: a pure C11 renderer and input handler that draws
an entire 800x480 RGB565 screen in the visual language and interaction model of the current flagship
club players, adapted to the XDJ-XZ.

## Read first
1. `docs/decisions/2026-10-18-xz-flagship-shell-plan.md` (plan and the hand-off fallback rule).
2. `docs/research/2026-10-18-cdj3000-xdj-az-ui.md`: the target. Sections 1 (screen inventory),
   3 (navigation model, browse layout, SHORTCUT, UTILITY and its item list), 4 (colours, type ladder,
   icons) and 6.2 (800x480 budget for lists and three-pane menus).
3. `docs/research/2026-10-18-xz-native-surface.md` sections 3 and 4: what data really exists. Never
   design a field the deck cannot supply without a known bit that renders it as unknown.
4. The shell so far: `shell.h`, `shell_internal.h`, `shell.c` (navigation table: which key opens your
   screen, BACK/BACK_HOLD, HAND_OFF when a model slot is NULL or a capability is missing),
   `shell_play.c` and `shell_draw.c` (drawing helpers, type faces, colours; reuse them), `test_shell.c`,
   `preview.c`, `fixtures.h`.
5. Reference captures (Read tool): `.tmp/cdjdocs/mods-browse-edit-off.png` (browse list with rail and
   preview column), `mods-mod-settings-open.png` (three-pane UTILITY chrome), `mods-theme-original.png`
   (status strip), `az-playlist-move.png` and `az-themes-day.png` (AZ list and layout).
6. The current shell previews in `.tmp/xz-shell-preview-2/` show the finished play screen your
   screens must sit beside.

## Rules
- Work only in the files your brief lists, plus new files whose names start with your family prefix.
  Do not edit `shell.c`, `shell.h`, `shell_internal.h`, `shell_play.c`, `shell_draw.c`,
  `shell_sources.py`, `verify.py`, `preview.py`, `test_shell.c` or `preview.c`. If you need a shared
  helper, put a static copy in your file and say so in your reply. If you need a change to a shared
  file, describe it exactly in your reply and the parent will make it.
- Your model types go in your own header (`shell_<family>.h`), which defines the opaque slot struct
  `shell.h` forward-declares (`struct xz_shell_browse_model`, `struct xz_shell_settings_model`,
  `struct xz_shell_info_model` or `struct xz_shell_source_model`). Every value carries a known bit or a
  count; unknown renders as the flagship's blank convention, never a guess. Strings are runtime-owned
  pointers.
- Input: turn the shell events (touch, encoder turn/push/hold, keys) into the flagship behaviour from
  research section 3: touch-down highlights, a second touch or encoder push confirms, flick and drag
  scroll, encoder turn moves the cursor, encoder hold does letter or page jump on lists, BACK goes up
  one level. Emit typed actions for anything the runtime executes (`XZ_SHELL_ACTION_LOAD`,
  `SET_SETTING`, `HAND_OFF`...). A missing capability means emitting HAND_OFF, or showing the control
  as unavailable with one sentence of reason; never a dead control.
- Look and feel: black page, #191919 chrome, #3c3c3c rails, #646465 selected rail cell, #007de1
  selected row, #282828 alternate rows, #ff4619 alerts, #a4a4a4 captions, white caps labels, the
  numeral faces in `shell_font.h` for big numbers. Keep the deck status visible at the bottom of
  non-play screens, as the flagship does (research 3.3: a compact strip with player, time, mini
  overview, tempo and BPM for both decks). Draw it with your own static helper reading the same deck
  model. Hit targets are at least 44 px tall, or padded to 44.
- Text never leaves its region: measure with the font width and truncate. No brand names (CDJ,
  Pioneer, rekordbox, AlphaTheta) in identifiers, file names, comments or on-screen text. Say "flagship
  player". Comments only for a non-obvious why.
- Verification: write `test_<family>.c` (picked up automatically by `verify.py`; start it with the same
  NDEBUG `#error` guard as `test_shell.c`). Assert navigation, cursor and scroll clamps, confirm and
  back semantics, the actions emitted, unknown-field rendering with NULL pointers, and bounds (guard
  words around the frame). Write `preview_<family>.c` with its own `main(int argc, char **argv)` that
  writes PPMs named `<family>-<state>.ppm` into `argv[1]` (see `preview.c` for the PPM writer),
  covering full data, empty or unknown data, long strings, a deep cursor position and each sub-state.
  Run, from `packages/xdj-xz-toolkit`:
  `python mods/shell/verify.py --zig ../../.tmp/cdj-zig/ziglang/zig.exe` and
  `python mods/shell/preview.py --zig ../../.tmp/cdj-zig/ziglang/zig.exe --output ../../.tmp/xz-shell-<family>-<n>`
  (a new directory each run). Other builders work in parallel, so a failure inside another family's
  file is not yours. Re-run after a minute, and report it if it persists.
- Then look at every one of your PNGs with the Read tool, next to the reference captures and the play
  screen preview. Iterate until each one reads as the flagship player's screen at 800x480: hierarchy,
  alignment, nothing clipped, nothing overlapping, no empty-looking regions. Say what you changed after
  looking.
- No git commits. No device or network access.

Reply with: the files you created, the model struct (short), the navigation and actions, the verify
output tail, the preview directory, the images you inspected and what you changed, and any
shared-file changes you need.
