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
- The MODS and STEMS header buttons are drawn over every screen by `shell.c` (`XZ_SHELL_HEADER_X`
  and friends in `shell.h`): keep x >= 674 clear above y 24, and put title-bar items left of it.
- Waveforms, overviews, key text and the playhead position come from `shell_wave.c`
  (`sh_wave_lane`, `sh_wave_overview`, `sh_key_text`, `xz_shell_elapsed_now`) so every screen draws a
  deck the same way; wave colour, position and key notation are shell state (`u->display`).
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

## PLAY screen audit (2026-10-19)

Rule, from the owner: remove only what was carried over from the stand-alone flagship player and does
not apply to a two-deck unit; keep every indicator the stock XZ play screen shows, and let the DJ hide
the ones that are reasonable to hide (APPEARANCE, shown by default). "Stock" means the 33 numbered
elements of the normal playback screen in the XDJ-XZ operating instructions DRI1625-B, pp.69-73
(`E:/Claude/xz-ui-manuals/XDJ-XZ_DRI1625B_manual.txt`, figure `xz-p69.png`;
`docs/research/2026-10-19-player-ui-manual-map.md` lines 76-92).

| Element | On the stock XZ screen? (source) | Decision | Why |
|---|---|---|---|
| Top bar: SINGLE \| CONTINUE | No; PLAY MODE is a MY SETTINGS value (`my_settings.c` play_mode) | Keep, hideable (MODE LAMPS) | Applies to the unit and is a touch toggle; grouped with the two lamps it sits beside |
| Top bar: A. HOT CUE | Yes, element 15 (p.70) | Keep, hideable (MODE LAMPS) | Stock indicator |
| Top bar: A. CUE | Yes, element 14 (p.70) | Keep, hideable (MODE LAMPS) | Stock indicator |
| Top bar: BEAT FX strip (effect, beat or ms, FX BPM with AUTO/TAP, channel, FX QUANTIZE) | Yes, elements 29-33 (pp.72-73) | Keep; takes the lamps' room when they are hidden | Stock; the only on-screen BEAT FX readout |
| Top bar: LINK | No (link state is only in the connection pages, pp.23-28); no newer player draws one on its playing screen either (CDJ-3000 pp.21-23) | Remove; BEAT FX takes the room | A port addition, not stock anywhere; SHORTCUT still shows the link lamp |
| Top bar: INFO icon | No (INFO is a key, pp.81, 90) | Keep | Touch target into INFO |
| Top bar: STEMS, MODS | Mod | Keep | Only touch route to the mod panel and stem layouts |
| Column: DECK n tag | Yes, elements 4, 6 (pp.69-70) | Keep | Touch sets the active deck |
| Column: bar phase dashes | No | Keep | Applies; downbeat at a glance |
| Column: source (USB1) | Yes, element 5 (p.69) | Keep | Stock |
| Column: KEY | Yes, element 7 (p.70), the track's key | Change: live key | Plays-now key large, traffic-light colour, cents off the semitone, ORIGINAL analysed key once they differ, KEY SYNC lamp. Old runtime: key and key-shift step as before |
| Column: key-shift step (triangle and number) | No; KEY SHIFT is a mod pad bank | Change: folded into the live key | The playing key and ORIGINAL say the same thing more plainly; kept for a runtime without the analysed key |
| Column: BEAT JUMP and its size | No (the column holds deck, device, key and countdown, p.69; beat jump is a pad bank, pp.56-57) | Remove | Separate-player readout; the XZ has a grid of beat-jump pads |
| Column: Q and beat value | Duplicate of elements 24-25, which the XZ shows in the deck strip | Remove from the column | Shown once, where the stock screen has it |
| Lane: waveforms, cue / memory / hot-cue marks | Yes, elements 1, 12-13 | Keep | Stock |
| Lane: beat countdown (4.0) | Yes, element 8 (p.70) | Keep | Stock |
| Lane: loop shading and "4 BEAT LOOP" | Yes, elements 21-22 (p.71) | Keep | Stock |
| Lane: ON AIR | Yes, element 10 (p.70) | Keep | Stock; UTILITY ON AIR DISPLAY already hides it |
| Lane: bar numbers every four bars | No | Keep | Applies; phrase counting |
| Lane: SLIP ghost, loop-point box, alert banner, NO STEM WAVE YET | No (manual behaviours pp.47-50, 131; mod) | Keep | Applies; shown only while true |
| Lane: ZOOM / GRID | Yes, element 28 (p.72) | Keep | Stock and a touch target |
| Pad row: "HOT CUE" caption | No | Remove | A label restating the A-H lamps next to it |
| Pad row: hot-cue lamps A-H | No (the pads light themselves) | Keep | Applies; shows CALL state and colours |
| Pad row: MUTE and stem buttons | Mod | Keep | Touch targets |
| Strip: DECK n, title, artist, artwork, TRACK and number | Yes, elements 6, 9, 11 | Keep | Stock |
| Strip: QUANTIZE | Yes, elements 24-25 (pp.71-72): red on, grey off, beat value beside it; SHIFT + DECK QUANTIZE sets one side (p.116) | Change: moved up to the caption row between REMAIN / TIME and TEMPO, as the stock screen places it, with its beat value; hideable (QUANTIZE LAMP) | Stock indicator, now shown once per deck |
| Strip: REMAIN / TIME and the time | Yes, elements 16-17 | Keep | Stock and a touch target |
| Strip: TEMPO %, range | Yes, elements 20, 26 | Keep | Stock |
| Strip: MT | No (MASTER TEMPO is a lit button, p.35) | Keep; blinks 500 ms / 500 ms while KEY SYNC holds the key | Owner request: the XZ has no KEY SYNC lamp |
| Strip: SYNC | No (lit button and jog display, pp.36-37); the newer players show it (CDJ-3000 element 23) | Keep | Beside MT and the BPM box's MASTER, as on the newer players |
| Strip: SLIP, VINYL | No (lit buttons and the jog display, pp.36-37); the newer players do not show them either | Remove | Shown twice already; the lane's SLIP ghost stays |
| Strip: BPM and MASTER | Yes, element 23 | Keep | Stock |
| Strip: overview, playing address, minute scale, cue chips | Yes, elements 12-13, 18-19 | Keep | Stock and the needle-search target |
| Strip: NEEDLE LOCK tag | No (setting, p.63) | Keep | Says why a touch does not search |
| Stock AUTO PLAY lamp (element 27) | Yes | Not drawn (unchanged) | The model carries no AUTO PLAY state yet |
