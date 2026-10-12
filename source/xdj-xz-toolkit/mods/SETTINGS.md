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

`rekordbox_menu`: 1 (default) makes the physical REKORDBOX key open and close
MODS, so every setting stays reachable with the browse encoder and BACK when the
touchscreen does not respond. 0 restores the key's stock rekordbox action. It is
appended after `ram_debug`; records without it load the default.

`fb_takeover`: 1 (enabled, default), 0 (disabled). Controls whether the VJ.Tools video and composite frame takes over the hardware display framebuffer. When disabled, the native Pioneer XDJ-XZ playback/browse screen is displayed uninterrupted.

`takeover_assign`: 0 (LINK physical button, default), 1 (REKORDBOX physical button), 2 (Onscreen VJ.Tools button in opposite corner from MODS). Selects which control toggles FB display takeover on and off. REKORDBOX only switches takeover while `rekordbox_menu=0`.


`stem_page`: 0 Hot Cue, 1 Beat Loop, 2 Slip Loop, 3 Beat Jump. A/B/C toggle
drums/harmonics/vocals; D toggles bypass while the STEMS controls own the deck.
With `shift_pages=1`, Shift plus the four page buttons provides those four
actions in the same order. Releases remain owned even if Shift is released
first or the panel closes. Unmodified page buttons retain their native action.

`pad_feedback=1` selects the UI theme's stem RGB colours, dimming muted pads.
`shift_keysync=1` reserves Shift+Sync for Key Sync; automatic pitch matching is
currently unavailable pending verified track-key/master metadata. New USBs
default that setting to0. Per-track mute/bypass and temporary key shifts are
performance state and are not restored on boot.

Hardware acceptance still requires a saved preference readback and player
restart with the same USB, a restart with no USB (internal restore) and a
stick pull mid-set. Host and isolated ARM filesystem tests
(`tests/verify_profiles.py`, under WSL gcc with ASan/UBSan) do not prove USB
removal, the real UBIFS mount or physical power-loss behaviour.

## Beat-jump pad assignments

Open MODS > ADVANCED > BEAT JUMP SETUP. Enable CUSTOM BEAT JUMP, choose
EDIT PAGE 1 or EDIT PAGE 2, then tap either pad in a pair. The editor mirrors
the physical two rows of four pads. UP/DOWN or the browse encoder changes
the selected pair's size; the selection stops at the smallest/largest size. A/B, C/D,
E/F and G/H are independent pairs. The first pad jumps backward and the
second jumps forward by the same amount. Assignments apply to both decks.

Available sizes are 1, 2, 4, 8, 16, 32 and 64 beats, plus 16, 32 and 64 bars.
A bar means four beats, so the bar choices send 64, 128 and 256 beats.

Press BEAT JUMP to select page 1. With SHIFT + BEAT JUMP enabled in the menu,
hold Shift and tap the physical Beat Jump mode button to select page 2.
Release Shift before pressing the pads. Selection is independent for each deck.
Shifted pad presses keep their stock behavior.

Custom jumps and Shift page selection default on. Page 1 starts at 1/2/4/8 beats; page 2 starts at
16/32/64/128 beats, equivalent to 4/8/16/32 bars. Custom jumps have priority over
stem pads assigned to Beat Jump. The page-2 shortcut takes priority over the
optional Shift + Beat Jump stem-bypass shortcut while enabled.

The version-1 record appends `jump_enabled`, `jump_shift_page` and
`jump_1` through `jump_8`. The first four assignments belong to page 1;
the last four belong to page 2. Values 0 through 6 mean 1/2/4/8/16/32/64 beats;
7/8/9 mean 16/32/64 bars. Older records receive the defaults. The complete
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

Local stem buttons fill from left to right by their effective level. Sliding
right raises the level and fill; sliding left lowers both. Muting clears the
fill while retaining the stored percentage in the label. PC controls retain
their confirmed on/off feedback and setup button; they do not invent volume
values the PC transport does not report.

`wave_mode` is an optional trailing field after `stems_overlay`: 0 stock
scrolling waveform, 1 CDJ-3000 style layered 3-band (default, also for older
records), 2 stem layers in the theme's drums/harmonics/vocals colours. Stems
falls back to 3-band on tracks without an OverCue bundle. MODS > Settings >
SCROLLING WAVEFORM changes it live. `XZ_MODS_WAVE_MODE` overrides it at start.
See `ui/LAYERED_WAVE.md`.
Stem-row visibility and the A-D/E-H pad bank now queue preference saves.
Stem colors are configured in MODS > APPEARANCE > STEM COLORS. The existing
drum and harmonic shades are swapped, so drums are blue, harmonics red and
vocals green in the Original theme. Each theme keeps its original shades.
Choose a color for each stem, or THEME to follow appearance. RESET STEM
COLORS restores all three theme defaults. Choices update the stem rows,
pad LEDs, prepared STEMS waveform and waveform shortcut preview together.
The frequency-based 3-BAND waveform keeps its blue/amber/white palette.

`stem_drums_color`, `stem_harmonics_color` and `stem_vocals_color` append
after `key_format`. Each is 0 theme default, 1 blue, 2 red, 3 green, 4 amber,
5 purple, 6 cyan, 7 pink or 8 white. Older settings records default all three
to 0. Fixed choices survive theme changes and save with the selected USB
profile. These fields also appear in the desktop USB settings editor.
The live launcher must omit XZ_MODS_THEME to respect the saved theme and set
XZ_MODS_SETTINGS_READONLY=0 for durable USB preferences.

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
