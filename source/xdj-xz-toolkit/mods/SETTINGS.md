# USB preferences

The loader passes its mounted USB root in `XZ_MODS_USB`. Preferences are stored
at `VJ.Tools/XZ-Mods.cfg` on that USB. The audio/input callbacks never write files.
The settings worker atomically replaces a complete version1 record and reports
save success or failure in MODS. An absent file uses defaults; an invalid record
is rejected as a whole. Settings are shared by both internal decks.

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

`fb_takeover`: 1 (enabled, default), 0 (disabled). Controls whether the VJ.Tools video and composite frame takes over the hardware display framebuffer. When disabled, the native Pioneer XDJ-XZ playback/browse screen is displayed uninterrupted.

`takeover_assign`: 0 (LINK physical button, default), 1 (REKORDBOX physical button), 2 (Onscreen VJ.Tools button in opposite corner from MODS). Selects which control toggles FB display takeover on and off.


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
restart with the same USB. Host and isolated ARM filesystem tests do not prove
USB removal or physical power-loss behaviour.

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

`stems_overlay` is an optional trailing boolean, default 1 for older records.

`wave_mode` is an optional trailing field after `stems_overlay`: 0 stock
scrolling waveform, 1 CDJ-3000 style layered 3-band (default, also for older
records), 2 stem layers in the theme's drums/harmonics/vocals colours. Stems
falls back to 3-band on tracks without an OverCue bundle. MODS > Settings >
SCROLLING WAVEFORM changes it live. `XZ_MODS_WAVE_MODE` overrides it at start.
See `ui/LAYERED_WAVE.md`.
Stem-row visibility and the A-D/E-H pad bank now queue preference saves.
The live launcher must omit XZ_MODS_THEME to respect the saved theme and set
XZ_MODS_SETTINGS_READONLY=0 for durable USB preferences.

`XZ_MODS_SETTINGS_USB` can select a separate mounted USB partition for
preferences. It takes precedence over `XZ_MODS_USB` for settings only; media
and stem discovery retain their existing root. The live test uses the first
partition because the second was remounted read-only after a FAT error.
