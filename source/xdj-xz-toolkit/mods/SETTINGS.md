# USB preferences

The loader passes its mounted USB root in `XZ_MODS_USB`. Preferences are stored
at `VJ.Tools/XZ-Mods.cfg` on that USB. The audio/input callbacks never write files.
The settings worker atomically replaces a complete version1 record and reports
save success or failure in MODS. An invalid record is rejected as a whole.
Settings are shared by both internal decks.

The behaviour follows the stock My Settings:

- Browsing a local USB (USB1/USB2 key) loads that stick's settings. PC and
  LINK sources never change them.
- A stick with no settings file, or an invalid one, keeps the current
  settings. Pulling the selected stick keeps them too ("USB REMOVED: SETTINGS
  KEPT"). Nothing resets to defaults mid-set.
- Every change saves on its own to the selected stick. There is no save
  button. The result ("SAVED TO USB 2", "NOT SAVED TO USB 2: CHECK USB")
  takes the MODS footer on every page for 3 seconds.
- The XZ remembers the last applied settings on its internal settings volume
  (`settings_internal.c`, `/root/settings/VJ.Tools/preferences.{0,1}`: two
  CRC-checked slots, UBIFS `ubiN:settings` mount check, 256 KiB free-space
  floor; the stock `XdjSettings.dat` is never touched). A boot with no stick
  restores them; a stick with settings still wins once it loads. With no stick
  inserted, changes save only there ("SAVED ON THIS XZ / NO USB").
- `XZ_MODS_SETTINGS_READONLY=1` (RAM trials) writes nothing anywhere.

DJ profiles are not settings. They live only on the DJ's stick, never on the
XZ.

```
XZ_MODS_SETTINGS 1
stems=1
gate=0
smart=0
theme=0
stem_page=0
shift_pages=0
pad_feedback=1
shift_keysync=0
fb_takeover=1
takeover_assign=0
```

## Interface and themes

`interface` is the one switch for who draws the player screen: 1 MODERN (the
default) is the new full-screen interface (the FLAGSHIP shell), 0 CLASSIC is
the XZ's own layout. `classic_theme` (0 ORIGINAL to 23 ANALOG) restyles the
classic interface; `modern_theme` (0 STANDARD, the only one so far) is the
modern interface's look. Both lists are kept, so switching back restores the
theme the DJ had. MODS > APPEARANCE shows INTERFACE first, then THEME for the
selected interface only, then WAVEFORM and KEYS AND STEMS. Classic themes open
on their own CLASSIC THEMES page. The desktop setup wizard writes `interface`
and never changes `classic_theme`.

The three fields are appended after `stem_order` as one block; a record holds
all three or none. The older `theme` slot stays at its place and is written as
`xz_settings_look(interface, classic_theme)`: 24 (FLAGSHIP) while MODERN, else
the classic theme. Every renderer, the shell's ownership check included, reads
that derived look, so nothing else decides screen ownership. Records without
the block migrate from their `theme`:

| older `theme` | `interface` | `classic_theme` |
|---|---|---|
| 24 FLAGSHIP | 1 MODERN | 0, or the current classic theme when overlaid |
| 0 to 23 | 0 CLASSIC | the same value |
| no `theme` (records shorter than 4 fields) | unchanged | unchanged |

A record with the block ignores the value in the `theme` slot. The shipped
loader starts the runtime with the shell armed (`XZ_MODS_SHELL` follows
`XZ_MODS_UI`, like the other UI attachments), so `interface` alone decides.

## Rekordbox button (`rekordbox_press`, `rekordbox_shift`)

MODS calls the rekordbox button above LINK REKORDBOX BUTTON, its printed name; the code
and the wire keys use the name REKORDBOX too. It has two slots: `rekordbox_press` for a plain press and
`rekordbox_shift` for a press while either deck's SHIFT is held. Each holds one
of these (stored numbers never change):

| value | MODS label | the key |
|---|---|---|
| 0 | MODS MENU | opens and closes MODS: the shell's own MODS screen in MODERN (as a short MENU press there), the MODS panel in CLASSIC; both work with the browse encoder and BACK |
| 1 | ITS OWN JOB | the key's stock action: the stock screen for a laptop's or phone's library over the network (or USB). In MODERN the shell steps aside visibly until stock is back on its play screen |
| 2 | INTERFACE | switches MODERN and CLASSIC, with the same notice as MODS > APPEARANCE |
| 3 | VJ.TOOLS SCREEN | switches the VJ.Tools screen; without an enabled, connected VJ.Tools link it says so instead |
| 4 | STEMS | shows or hides the stem rows (the shell's STEMS header button in MODERN, the STEMS button's layout toggle in CLASSIC) |
| 5 | NOTHING | the key does nothing; stock never sees it |
| 6 | DECK VIEW | steps DECK VIEW 2 DECKS, 4 DECKS (1 2 3 4), 4 DECKS (3 1 2 4), then 2 DECKS, saves it and shows the new view in a short notice |

Defaults: `rekordbox_press=2` (INTERFACE), `rekordbox_shift=1` (stock). MODS >
PLAY > REKORDBOX BUTTON sets both, in both interfaces. Value 6 is newer than the
DECK VIEW block below; older runtimes refuse a record that holds it.

Recovery without a touchscreen: in CLASSIC, REKORDBOX (INTERFACE) switches to
MODERN, where the hardware MENU key opens the shell's MODS screen, and MODS is
driven with the browse encoder and BACK. So MODS stays reachable by hardware
alone with the default assignment.

The pair is appended after `modern_theme` as one block; a record holds both or
neither. `rekordbox_menu` keeps its slot and is always written as
`rekordbox_press == 0`, so an older runtime reading the stick sees REKORDBOX
open MODS exactly when MODS MENU is chosen. A record without the pair migrates:

| older record | `rekordbox_press` | `rekordbox_shift` |
|---|---|---|
| `rekordbox_menu=1`, or no `rekordbox_menu` (its default was 1) | 2 INTERFACE | 1 stock |
| `rekordbox_menu=0`, `takeover_assign=1` (REKORDBOX switched the VJ.Tools screen) | 3 VJ.TOOLS SCREEN | 1 stock |
| `rekordbox_menu=0`, any other `takeover_assign` | 1 stock | 1 stock |

Overlaid on the applied settings (boot restore), an older record that names
`rekordbox_menu` brings its migrated press; its SHIFT slot was never chosen and
keeps the applied value.

`fb_takeover`: 1 (enabled), 0 (disabled, default). Controls whether the VJ.Tools video and composite frame takes over the hardware display framebuffer. When disabled, the native Pioneer XDJ-XZ playback/browse screen is displayed uninterrupted.

`takeover_assign`: 0 (LINK physical button, default), 2 (Onscreen VJ.Tools button in opposite corner from MODS). Selects which control toggles FB display takeover on and off. 1 (REKORDBOX) is retired: the REKORDBOX button's VJ.TOOLS SCREEN choice replaced it. A record with 1 loads as 0 (LINK) and, without the REKORDBOX pair, migrates the press to VJ.TOOLS SCREEN (above); MODS no longer offers it.


`stem_page`: 0 Hot Cue, 1 Beat Loop, 2 Slip Loop, 3 Beat Jump. A/B/C toggle
drums/harmonics/vocals; D toggles bypass while the STEMS controls own the deck.
Shift plus the mode buttons always provides the stem shortcuts in the saved
order. SHIFT + BEAT JUMP is reserved for jump page 2. Old `shift_pages=0`
values are ignored. Releases remain owned even if Shift is released
first or the panel closes. Unmodified page buttons retain their native action.

`pad_feedback=1` selects the UI theme's stem RGB colours, dimming muted pads.
SHIFT + SYNC is always available on a standalone USB deck; matching requires
verified track-key and master metadata. Old `shift_keysync=0` values are ignored.
There is no menu enable switch for either shortcut family. Per-track mute/bypass and temporary key shifts are
performance state and are not restored on boot.

Hardware acceptance still requires a saved preference readback and player
restart with the same USB, a restart with no USB (internal restore) and a
stick pull mid-set. Host and isolated ARM filesystem tests
(`tests/verify_profiles.py`, under WSL gcc with ASan/UBSan) do not prove USB
removal, the real UBIFS mount or physical power-loss behaviour.

## Beat-jump pad assignments

Open MODS > PLAY > BEAT JUMP PADS. Choose
PAGE 1 or PAGE 2, then tap either pad in a pair. The editor mirrors
the physical two rows of four pads. Tap a size in the row of sizes along the
bottom, or push the pair and turn the browse encoder to step its size (it stops
at the smallest and largest size). A/B, C/D,
E/F and G/H are independent pairs. The first pad jumps backward and the
second jumps forward by the same amount. Assignments apply to both decks.

Available sizes are 1, 2, 4, 8, 16, 32, 64, 128 and 256 beats; the editor shows
the bars beside the beats (4 beats = 1 bar).

Press BEAT JUMP to select page 1. Hold Shift and tap the physical Beat Jump mode button to select page 2.
Release Shift before pressing the pads. Selection is independent for each deck.
Shifted pad presses keep their stock behavior.

Page 2 is always available, even when PAGE 1 CUSTOM JUMPS is off.
That switch controls only page-1 remapping. Page 1 starts at 1/2/4/8 beats; page 2 starts at
16/32/64/128 beats, equivalent to 4/8/16/32 bars. Custom jumps have priority over
stem pads assigned to Beat Jump. The page-2 shortcut takes priority over the
optional Shift + Beat Jump stem-bypass shortcut. Page-2 selection always wins that combo.

The version-1 record appends `jump_enabled`, `jump_shift_page` and
`jump_1` through `jump_8`. The first four assignments belong to page 1;
the last four belong to page 2. Values 0 through 6 mean 1/2/4/8/16/32/64 beats;
8 and 9 mean 128 and 256 beats. 7 is the older "16 BARS" choice, also 64 beats: it
still loads and shows as 64 beats, and nothing offers it any more (the schema marks
it `"offered": false`). Older records receive the defaults. The complete
new block must be present when supplied. The settings worker saves changes;
read-only RAM trials keep changes only until restart.

The adapter calls the firmware's signed-beat jump command. Native track,
buffer and transport limits still apply; a rejected jump does not fall back
to a different size or a time-based seek. Physical loaded-track acceptance,
including long jumps with quantize and slip, remains required.

Every pad displays separate BEATS and BARS rows. The selected pair shows an
explicit equality, and the heading states 4 BEATS = 1 BAR. Size steps skip
equivalent beat/bar entries. On physical page 2, the native red/orange grouping uses warm red and
amber-orange shades. Brightness scales with the native intensity, and
off/dim states, blinking and forced native status remain unchanged. Page 1 retains native colors.

`stems_overlay` stores the stem-row layout. Existing values retain their
meaning: 0 hidden with normal waveforms, 1 full-height rows below each deck.
The added values are 2 half-height rows below, 3 full-height rows above the
top deck and below the bottom deck, and 4 half-height rows at those outside
edges. The native STEMS button cycles 1, 2, 3, 4, 0, then 1. Its five marks
show the current choice. MODS > CONTROLS and the desktop USB settings editor
show the saved layout. Older records still default to 1.

Full rows are 48 pixels high; half rows are 24. Each half-height choice
returns 24 pixels to each waveform, raising its height from 84 to 108.
Hidden restores both 132-pixel native lanes. Cue bands and x coordinates
retain their original size; waveform touches map back to the original rows.
While four decks show (`ui/FOUR_DECK.md`) no stem rows are drawn; the saved
value is kept and returns when the CH3/CH4 route goes down.

Local stem buttons fill from left to right by their effective level. Sliding
right raises the level and fill; sliding left lowers both. Muting clears the
fill while retaining the stored percentage in the label. PC controls retain
their confirmed on/off feedback and setup button; they do not invent volume
values the PC transport does not report.

`modern_stems` is the MODERN interface's own stem layout, so CLASSIC keeps
`stems_overlay`. It takes the five values above plus 5, small stem buttons in
the pad row beside the hot-cue lamps (the default, also for records without
it). The header STEMS button, a REKORDBOX button set to STEMS and MODS step it
5, 1, 2, 3, 4, 0, then 5. It is the optional 44th field.

## DECK VIEW

`deck_view` is what the play screen shows; it changes neither sound, mixer channels
nor which deck a side controls:

| value | MODS label | waveform lanes, top to bottom | bottom panels |
|---|---|---|---|
| 0 | 2 DECKS (default) | 1, 2 | 1, 2 |
| 1 | 4 DECKS, NUMBER ORDER 1 2 3 4 | 1, 2, 3, 4 | 3 1 2 4 |
| 2 | 4 DECKS, MIXER ORDER 3 1 2 4 | 3, 1, 2, 4 | 3 1 2 4 |

The MENU key is not a setting. A short MENU opens or closes MODS only where it has no stock job, in both
interfaces (`ui/menu_key.h`, one table); a held MENU always opens the player's own UTILITY. The button next to
LINK keeps its saved assignment.

`deck_view_button` is retired (2026-10-26): the DECKS button is always at the top of the play
screen in both interfaces. Its wire slot stays; a stored 0 reads as 1 and 1 is written. It is in
`retired_fields` (readonly, default 1) and MODS has no row for it.

The two are one block after `modern_stems` (fields 45 and 46); a record holds both or
neither. A record without them loads with the defaults, and overlaid on the applied
settings (boot restore, a stick's profile) it leaves the DJ's current DECK VIEW as it
is. DECK VIEW replaced the session-only four-deck layout preference: `ui/layout_cycle.c`
keeps the `xz_deck_layout_*` calls, and every `xz_deck_layout_set` is now saved here.
The play-screen picker, the decks 3 and 4 card's TWO DECKS, MODS and the button next to
LINK all apply it through `deck_view_apply()` in `ui_runtime.c`, which saves it at once.

`wave_mode` is an optional trailing field after `stems_overlay`: 0 stock
scrolling waveform, 1 CDJ-3000 style layered 3-band (default, also for older
records), 2 stem layers in the theme's drums/harmonics/vocals colours. Stems
falls back to 3-band on tracks without an OverCue bundle. MODS > Settings >
SCROLLING WAVEFORM changes it live. Old `XZ_MODS_WAVE_MODE` overrides are ignored; the saved setting wins.
See `ui/LAYERED_WAVE.md`.
Stem-row visibility and the A-D/E-H pad bank now queue preference saves.
Stem colors are configured in MODS > APPEARANCE > STEM COLORS. The existing
drum and harmonic shades are swapped, so drums are blue, harmonics red and
vocals green in the Original theme. Each theme keeps its original shades.
Choose a color for each stem, or THEME to follow appearance. RESET STEM
COLORS restores all three theme defaults. Choices update the stem rows,
pad LEDs, prepared STEMS waveform and waveform shortcut preview together.
The frequency-based 3-BAND waveform uses the saved low, mid and high choices in
MODS > SCREEN > 3-BAND COLOURS. STANDARD preserves each renderer's original palette.

`stem_drums_color`, `stem_harmonics_color` and `stem_vocals_color` append
after `key_format`. Each is 0 theme default, 1 blue, 2 red, 3 green, 4 amber,
5 purple, 6 cyan, 7 pink or 8 white. Older settings records default all three
to 0. Fixed choices survive theme changes and save with the selected USB
profile. These fields also appear in the desktop USB settings editor.
Old theme, waveform, stems, key-engine and inline-view environment overrides
are ignored. Set XZ_MODS_SETTINGS_READONLY=0 for durable USB preferences.

`stem_order` appends after `metrics_enabled` as the 38th field. It stores
the left-to-right order of the three stems on the play-screen stem rows,
and pads A/B/C and their lights follow it: 0 vocals/harmonics/drums (the
original order, and the default for older records), 1 vocals/drums/harmonics,
2 harmonics/vocals/drums, 3 harmonics/drums/vocals, 4 drums/vocals/harmonics,
5 drums/harmonics/vocals. MODS > GENERAL > STEM SETTINGS > STEM ORDER cycles
it; the pad diagram beside the button shows the result, and the desktop USB
settings editor offers the same six choices. The layered STEMS waveform and
the rekordbox PC stem row keep their own order. Like every setting it saves
to the selected USB.

`XZ_MODS_SETTINGS_USB` can select a separate mounted USB partition for
preferences. It takes precedence over `XZ_MODS_USB` for settings only; media
and stem discovery retain their existing root. The live test uses the first
partition because the second was remounted read-only after a FAT error.

## Feature switches and old files

SHIFT + BEAT JUMP page 2, top-bar metrics and free-RAM information do not
require enable switches. RAM appears in the metrics readout; the compact RAM
box remains a fallback when a host does not draw metrics. Tap METRICS for
setup and live verification. It no longer turns readings off.

The active desktop schema omits `shift_pages`, `shift_keysync`,
`jump_shift_page`, `ram_debug` and `metrics_enabled`. Their old version-1 wire slots remain in `wire_order` and
`retired_fields` so positional files and older readers stay compatible.
Disabled values are accepted and ignored by native parse and profile overlay.
The desktop reader discards them, and its writer emits 1 in those slots.
Records still accept every historical field count. USB and internal CRC slots
use that same parser. Options such as page-2 lengths remain editable.

Stock-altering settings retain one value owned by MODS. Desktop USB settings
and USB profiles write that value, rather than an extra prerequisite.
The last explicit profile application or menu edit wins. Performance actions
such as mute, bypass, pitch adjustment, game launch and RAM-copy requests are
additive MODS-page actions. They need no separate master enable. Classification
depends on whether the trigger is inert on stock, even when its action changes
audio. An explicit shift stem action activates the same audio state when needed;
it is not defeated by the automatic stem-on-load preference. See
[the complete decision table](../../../docs/feature-toggle-audit.md).

## Preview waveform in MODERN browse

`browse_preview` is field 47, appended after the DECK VIEW block. It is 0 by
default, hiding the preview waveform column completely so the remaining columns
use its space. 1 shows the waveform column. A fresh start or an older settings
record without this field starts hidden. Overlaying an older profile preserves
the currently applied choice; an explicit 0 hides it.

The PREVIEW tile in the browse header is the only on-screen control. One tap
shows or hides the column and queues the preference through the shell's action
queue to `ui_runtime.c` and its settings worker. Rendering never writes files.
The off tile says PREVIEW OFF / TAP TO SHOW. Browse encoder navigation is
unchanged; PREVIEW is touch-only. CLASSIC browse does not use this preference.


## 3-BAND colours and MODERN display preferences (fields 48 to 57)

Two all-or-none blocks follow `browse_preview`. Fields 48 to 50 are
`band_low_color`, `band_mid_color`, `band_high_color`, in that order. Each
accepts 0 STANDARD, 1 BLUE, 2 RED, 3 GREEN, 4 AMBER, 5 PURPLE, 6 CYAN,
7 PINK or 8 WHITE. All three default to 0. STANDARD keeps each renderer's
original colour, including the white browse preview highs, cream MODERN lane
highs and CLASSIC's own blue/amber/white shades. The played part of MODERN
overviews stays grey. Custom LOW also supplies the darker low shade of the
native FLAGSHIP waveform. Stem colours remain independent.

Fields 51 to 56 are a second block. It stores the shell's display choices,
including changes made in UTILITY, MODS > SCREEN and the waveform zoom control:

| Field | Key | Values | Default |
|---|---|---|---|
| 51 | `modern_wave_color` | 0 BLUE, 1 RGB, 2 3-BAND, 3 STEMS | 2 |
| 52 | `wave_position` | 0 CENTRE, 1 LEFT | 0 |
| 53 | `key_display` | 0 AS THE DECK SHOWS IT, 1 CLASSIC, 2 ALPHANUMERIC | 0 |
| 54 | `wave_zoom` | 0 to 6 | 3 |
| 55 | `usb_colors` | 0 TILE AND WAVEFORM, 1 TILE ONLY, 2 OFF | 0 |
| 56 | `hidden_parts` | 0 to 31, bitmask below | 0 |

`hidden_parts` has five independent bits: 1 HIDE MODE LAMPS, 2 HIDE QUANTIZE
LAMP, 4 HIDE BYPASS BUTTON, 8 HIDE WAVEFORM TITLES and 16 HIDE 4 DECKS STEM
BUTTONS. Add the chosen bits; 0 shows all five parts and 31 hides them all.
The desktop schema has no bitmask/checkbox-group control, so this is one
advanced numeric field.

Bit 16 is the STEMS button's choice in MODERN's 4 DECKS view. There the button
shows or hides the stem buttons with one press each, and the 2 DECKS layout
(`modern_stems`, field 43) stays as it is; in 2 DECKS the button steps that
layout. The two are kept apart because four lanes have no room for the
layouts: hiding the buttons to give four waveforms their height does not take
them off the 2 DECKS screen. CLASSIC draws no stem rows in four lanes, so its
STEMS button there changes nothing and says STEM BUTTONS SHOW IN 2 DECKS VIEW.
A runtime older than this bit rejects a record that sets it.

The audit also found MODERN's browse column mask in RAM. Optional field 57,
`modern_browse_columns`, now saves it. 0 keeps the automatic columns. Values
1 to 2047 select bits in this order: NUMBER, TITLE, ARTIST, BPM, KEY, RATING,
COLOR, TIME, ALBUM, GENRE, DATE ADDED. TITLE is always included when a custom
mask is applied. This is an advanced numeric field in the desktop schema.
The on-deck browse column picker remains the normal way to choose columns.

Records may stop at 47, 50, 56 or 57 fields. A partial colour or display block
is invalid. An absent block receives its defaults on parse, and leaves the
current values unchanged on profile overlay. The maximum valid complete
record is 829 bytes, below the internal store's 1023-byte payload limit.
An older runtime refuses the newer record as a whole and keeps its current
settings, as with the earlier append-only additions. It does not truncate or
partially apply the file.

The shell applies the first known settings frame, then only incoming values
that differ from the last applied input. A cached old frame cannot undo a
local edit while its save is pending. An explicit UTILITY WAVEFORM COLOR
choice still wins over the automatic 3-BAND default. Changes queue to the
existing settings worker; rendering never writes files. USB ownership,
internal fallback and `XZ_MODS_SETTINGS_READONLY=1` follow the rules above.
Read-only trials change the live display without writing USB or internal slots.

## Remaining RAM-state audit

| State | Result |
|---|---|
| MODERN waveform colour, position, key display, zoom, USB colours and four hide switches | Saved in fields 51 to 56. |
| MODERN browse column selection | Saved in field 57. |
| `XZ_UI_FOUR_WAVE_ORDER`, shell WAVE ORDER, play-screen deck layout | Already routed through saved `deck_view`; no extra setting. |
| MODERN browse preview | Already saved as `browse_preview`, field 47. |
| CLASSIC browse columns, key prefix/traffic, key notation, themes, stem order/colours/layout, jump assignments, button assignments | Already in the settings record. |
| MODERN browse sort column/direction | Per-list request state. Category, folder and source navigation deliberately reset it. Left session-only; a global or per-category sort policy needs a separate contract, rather than changing this reset behavior. |
| MODERN FILTER choices and enable state | Active query state, including BPM/key matching derived from the current master deck. Left session-only; restoring fixed filter defaults separately from a live query needs a separate contract. |
| Browse/search category, path, query, selection, active source, panel/tab/focus, GRID EDIT and active deck | Navigation or live working state, left session-only. |
| TAG LIST, playlist and PLAYED/UNPLAYED edits | Already written to the selected USB sidecar. These are library edits, not preferences in this record. |
| Stock UTILITY settings and MY SETTINGS | Firmware-owned; stock persists its settings and MY SETTINGS uses the stick's own export data. MODS does not duplicate those fields. |
| Arcade clock source, style, quiet, difficulty and jog sensitivity; Rhythm Lab/Wave Rider setup, game choice, player mode, action bindings and jog calibration | Still RAM-only game preferences. They are outside DJ display settings and require separate game preference schema and runtime bindings; no game state was added to this record. |
| SDK app configuration/autostart and DJ profile metadata | Owned by their existing USB app manifests/profile files, not by the shell display table. |
| Mutes, levels, bypass, key shift/sync, loaded tracks, transport, loops, pad page, GRID EDIT changes in progress, RAM copies, games in progress | Performance or working state. Never restored as preferences. |

Host checks cover the codec, desktop schema, rendering, live action publication,
USB worker and internal CRC slots on disposable volumes. They do not establish
controller, physical USB removal, real UBIFS or reboot acceptance.
