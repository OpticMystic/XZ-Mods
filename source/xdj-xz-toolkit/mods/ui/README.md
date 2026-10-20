# Native 800x480 mod controls

See [NATIVE_STYLE.md](NATIVE_STYLE.md) for the native-style typography, colours,
font provenance and local preview workflow. Track each deployed runtime and
physical acceptance separately from portable UI tests.

`ui.c` renders RGB565 into a caller-owned framebuffer. It has no DirectFB,
device, audio, network or allocation dependency. The runtime supplies a snapshot
and receives typed actions; the renderer never assumes a feature is implemented.

## Screen layout and hardware navigation

The panel is a fixed grid shared by every skin (`ui_skin.h`): a 40 px header
with BACK, the page title, optional deck pickers and CLOSE; a 124 px section
rail (CONTROLS, APPEARANCE, ADVANCED, EXTRAS) with drawn icons; content in
x 136..792, y 48..404; and an info dock at y 412..480. Controls never enter
the dock (`test_ui.c` enforces this for all 25 themes). The dock names the
focused control, its state (ON, OFF, SELECTED, NOT READY, ADJUSTING), what it
changes, the save status or latest notice, and the hardware key hints.

### DJ Profile in the header

The header's right corner shows the DJ Profile like a signed-in user: the
logo in a circle, the DJ name, the USB it came from and a caret. Beside the
deck pickers it shrinks to the logo and caret, and the deck pickers narrow to
170 px. The chip shows the active profile of the USB the master deck plays
from, else a USB another deck plays from, else the USB the browser shows,
else the first USB with a profile (`dj_runtime.c`). With no profile it shows
a silhouette and TAP TO SET UP.

Tapping the chip (or pushing it with the encoder) opens a list under the
corner, over the dimmed page: every profile on both USBs with its logo,
name, USB and tagline, a check on each USB's active profile, pages of five,
and PROFILE SETUP (SET UP DJ PROFILE when there is none, as the primary
button). Picking a profile rewrites only that USB's
`DJPROFILE/VJTOOLS/index.json`; the footer shows the result on every page
for three seconds. A damaged profile stays listed under its index name and
cannot be picked. While the list is open it is the only thing touch and the
encoder reach. A tap outside or BACK closes it, and focus returns to the chip.
The list's last button opens the DJ PROFILE page (`ui_profile.c`). It shows
one profile at a time: the full 160 px logo, name, tagline, USB and whether
VJ software sees it now, and PREV and NEXT between profiles. QR display and
online sharing are removed for now at the owner's request; the online page
is not available. Buttons: EDIT, MAKE ACTIVE, NEW PROFILE, CHECK USB. With no
profile the page walks insert USB, create, choose, with CREATE ON THIS XZ as
the primary button and CHECK USB beside it.

The editor (`XZ_UI_PROFILE_EDIT`) has four steps: DJ NAME and TAGLINE on an
on-screen keyboard (letters, digits, a symbols layer, SHIFT, SPACE, DELETE; 64
and 80 characters), LOGO from any PNG or JPEG on the USB (images directly in
`DJPROFILE/<name>` and `DJPROFILES/<name>` folders first, then the rest of the
stick up to three folders deep, rendered as the XZ tile before it is chosen,
or NO LOGO for a monogram), and REVIEW with SAVE TO USB 1 or 2. A new profile
takes the logo in the folder named like the typed name until the DJ picks
(`xz_ui_profile_autopick`). Without one, the LOGO step tells the DJ to put the
logo in `DJPROFILE/<typed name>`: as the whole step, with CHECK USB AGAIN as
the one button, when the stick has no image at all, otherwise above a
four-row list with RESCAN. The page with no profile leads with the same
folder: make `DJPROFILE`, a folder with your DJ name inside, the logo in it.

Hand-made folder profiles (`dj_folders.c`, standard §1.1) appear in the
header and on the page in the stick's rekordbox colour. MAKE ACTIVE or EDIT
on one saves it as a `VJTOOLS` profile with the folder's logo. A new profile walks the steps
in order; EDIT opens on REVIEW and each CHANGE button returns there. BACK goes
one step back before it leaves. Every key is focusable, so the encoder alone
can type. Typing stays in `xz_ui`; SAVE hands the draft to the worker, which
writes only `DJPROFILE/VJTOOLS` on that USB (`dj_profile_write.c`, mirroring
Rust `publish_profile`) and makes a new profile active there. Profiles are
never stored on the XZ. `model.dj` is NULL when the runtime has no reader,
which hides the chip, the page's buttons and the editor.

Every control is reachable without the touchscreen, so a player with a broken
digitizer keeps the whole MODS menu:

| Key | While MODS is open | While MODS is closed |
|---|---|---|
| REKORDBOX | what CONTROLS > BUTTONS assigns (below); MODS MENU closes MODS | what CONTROLS > BUTTONS assigns; MODS MENU opens MODS |
| Browse encoder turn | moves focus: content in reading order, then deck pickers, then the section rail, wrapping | stock |
| Browse encoder push | activates the focused control exactly like a tap | stock |
| BACK | leaves a value edit, else goes up one page, else closes | stock |

Focus is stored by control identity (`focus_kind`, `focus_index`,
`focus_deck`), so relayouts keep it, and a touch moves it to the touched
control. Pushing a beat-jump pair or the X-PAD volume enters an edit where
turning changes the value; push or BACK ends it. `xz_ui_encoder_turn`,
`xz_ui_encoder_push`, `xz_ui_focused` and `xz_ui_describe` are pure and
host-tested; the runtime hooks `UiKey_EncoderRotate` (0xe15b0),
`UiKey_EncoderPush` (0xdec08) and `UiKey_rekordbox` (0xe0a50) behind their
prologue guards.

### REKORDBOX button

CONTROLS > BUTTONS assigns the physical REKORDBOX key twice: a plain press and
SHIFT + press (either deck's SHIFT). Each slot is one of MODS MENU, EXPORT MODE
PLAYBACK (the key's stock action), INTERFACE, VJ.TOOLS SCREEN, STEMS or NOTHING;
the defaults are INTERFACE and EXPORT MODE PLAYBACK (`../SETTINGS.md`). The
FLAGSHIP shell's MODS screen shows the same two rows on its BUTTONS tab.

One decision per press. `ui_runtime.c`'s onKey hook reads SHIFT at the press,
picks the action and latches it. In MODERN it gives the action to the shell
first (`xz_shell_runtime_rekordbox_action`), whose key verdict
(`xz_shrt_rekordbox_verdict`) swallows only its own actions: MODS opens or
closes the shell's MODS screen (`XZ_SHELL_KEY_MODS`, as a short MENU there)
and STEMS runs the header STEMS request. The stock action passes with a
visible hand-off; INTERFACE, VJ.TOOLS SCREEN and NOTHING pass untouched.
Whatever passes reaches `UiKey_rekordbox`, whose hook runs the latched action
once and calls stock only for the stock action. In CLASSIC the shell is
stopped and the hook alone acts. MODS stays reachable by hardware with the
default assignment: REKORDBOX switches CLASSIC to MODERN, where MENU opens the
shell's MODS screen. With MODS MENU assigned and the shell unable to show its
screen, the MODS panel opens instead. VJ.TOOLS SCREEN without a connected link
says so (the shell's notice in MODERN, the VJ.TOOLS page with the reason in
CLASSIC).

The games launched from MODS take the same keys, so they need no touch either:

| Key | Wave Rider, Rhythm Lab, Glyph Run, Beat Arcade | Doom |
|---|---|---|
| Browse encoder turn | first detent shows a focus ring on the screen's main control, later detents move it through every on-screen control | menu up / down |
| Browse encoder push | taps the focused control | Enter |
| BACK | one screen back (setup step, sub-screen, menu); from a game's first screen, leaves it; ignored mid-round | Escape (game menu) |
| REKORDBOX (any assignment but NOTHING) | leaves the game and opens MODS on the page it was launched from | quits Doom; the next press opens MODS |

Each game renderer records the controls it draws that frame
(`arcade/game_nav.h`). The encoder moves over that list and a push replays a
touch at the control's centre through the game's own touch handler, so touch
and encoder share one hit test. A new set of controls means a new screen, and
focus returns to its primary control; it never starts on DECK or DJ VIEW, so a
stray push mid-round does nothing. Touching the screen hides the ring.
`arcade/verify_game_nav.py` taps every recorded control on 22 game screens and
drives Wave Rider setup with the encoder alone. Doom receives these keys
through a tap queue in the shared input page (protocol version 2).
SDK applications, including the kart preview, keep their own controls;
REKORDBOX assigned to MODS MENU opens MODS over them, where APPS can stop them.

## Runtime contract

1. Zero-initialize `xz_ui_model`; initialize each deck's `groove_active` and
   `sample_active` to -1, levels and sample volume to 1. Call `xz_ui_init` once.
2. Set each deck's `ready` bits only for connected, validated handlers. Keep
   `enabled` separate: implemented and enabled are different states. Deck 3/4
   labels describe external PC sources, not additional onboard players.
   The VJ connection snapshot is device-wide and independent of selected deck.
3. Feed pointer down/move/up into `xz_ui_touch`. Execute returned actions, then
   publish confirmed values in the next model. Do not optimistically enable a
   setting that failed. A refused action returns `XZ_UI_UNAVAILABLE` and the
   footer explains that readiness is missing.
4. Render with `xz_ui_render`. Stride is **pixels**, buffer count is **uint16_t
   elements**. It returns zero for insufficient size or invalid selected deck.
   Present through the host-owned display adapter without touching the existing
   VJ receiver's surface.
5. Call `xz_ui_cancel` and execute its release action on focus loss, overlay
   removal, track/device removal or teardown. Serialize touch, model snapshots
   and render on the UI thread.

`XZ_UI_PANEL` carries the old page in `index` and new page in `value`. Closing
X-PAD must stop its sound and restore the physical pad/memory/delete/volume
roles. Closing MODS or hiding the STEMS strip preserves independent stem pad control
on both decks while stems are enabled. Disabling stems restores normal hot cues. `XZ_UI_DECK` carries old deck in `deck` and new
deck in `index`; runtime must release old deck panel ownership and establish
the new one. `XZ_UI_CLOSE` returns the screen to the host's stock interface.
The pure UI cannot execute any of these engine transitions itself.

| Action | Payload and semantics |
|---|---|
| ENABLE | `index` is capability bit, `value` is requested 0/1 |
| LEVEL / VOLUME | normalized 0..1 `value`, PRESS/MOVE |
| MUTE | stem index 0..2, PRESS toggles the shared touch/pad mute latch |
| BYPASS | requested 0/1; runtime chooses original audio |
| GROOVE_PAD | slot 0..7; runtime toggles it and releases any previous slot |
| SAMPLE_PAD / HOTCUE_PAD | slot 0..7 and PRESS/RELEASE |
| STRIP | loop index 0..5, loop beats in `value`, pitch -12..12 in `secondary`; RELEASE ends gesture unless runtime HOLD is on |
| HOLD / OVERDUB | requested 0/1; disabling overdub clears the event sequencer |
| SET_THEME | theme index 0..23 |
| SERVER_AUTO | requested AUTO=1 / MANUAL=0 |
| SERVER_ADDRESS | request the host's address editor; no keyboard is claimed here |
| KEY_SHIFT / KEY_SYNC | signed semitone step / sync request; capability gated |
| CONNECTION_ENABLE / DISCOVERY | requested 0/1; receiver callback must confirm before the model changes |

Momentary controls retain pointer capture when dragged outside their rectangle.
Release targets the original deck and control even if runtime readiness changes.
Unassigned Groove slots emit normal HOTCUE actions; assigned but unloaded slots
remain NOT READY. `groove_assigned` and `groove_loaded` are therefore distinct.
The runtime remains responsible for physical-pad dispatch and mutual ownership
between hot cues, Groove and X-PAD.

The five tabs expose three stem faders, hold-to-mute labels, bypass, modification
dot, eight Groove pads, six X-PAD lengths, pitch strip, HOLD, OVERDUB, volume,
eight sample pads, capability-aware settings and 24 theme choices.
The VJ.Tools Connection tab remains visible even when no receiver integration is
loaded. It shows receiver-reported readiness, connection state, device IP, port,
protocol and frame rate. Unknown values read NOT REPORTED. `connection.ready`
means the runtime has a receiver status provider; `can_enable` and `can_discover`
mean the corresponding control callback exists. Read-only status works without
these callbacks. Frame rate is shown only when connected and `stats_valid`.

Zero-initialize the connection snapshot until actual receiver data is available.
`CONNECTION_ENABLE` and `DISCOVERY` only request changes; they neither start a
new receiver nor replace the existing protocol. The VJ.Tools connection is
optional: native DJ controls must retain their independent readiness and work
without a desktop. The header carries VJ.Tools branding, and the Connection
page credits cdj3k-mods, nsaintot and contributors.

Waveform peaks come from runtime data; absent data is explicitly labeled.
No sample names, waveforms or feature success are invented.

## Verification

```powershell
python mods/ui/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
python mods/ui/preview.py --zig .tmp/cdj-zig/ziglang/zig.exe --output .tmp/xz-ui-preview
```

The renderer's C files are listed once in `ui_sources.py`; every build and test
script compiles that list.

`test_ui` checks actions, held releases, all six strip bins, pitch endpoints,
capability refusal, unassigned/failed Groove slots, receiver callback readiness,
deck-independent connection controls, all themes and buffer bounds. Its
parity check lays out every page in every theme and requires the same controls
as theme 0, inside the screen, at least 56x36, non-overlapping and touchable.
`--prove-assertions` intentionally fails; the build also rejects NDEBUG.
Passing tests produce five PPM screen captures plus theme/readiness examples.
The connection demo has no reported receiver and never invents a live connection.
Host rendering and ARM object
compilation passed. DirectFB presentation, physical touch routing, audio actions
and complete stock-screen recoloring remain separate runtime acceptance gates.
## Physical stem pads

With stems enabled, the selected bank and pad mode control Vocals, Harmonics,
Drums, then bypass on that physical deck. Other pad modes remain native,
including Hot Cue when a different mode is selected. A-D is the default; E-H is an optional saved
setting in MODS > Controls. The inactive bank keeps its normal hot cues. Screen
labels, pad LEDs and touch actions follow the selected bank. The audio engine
retains its internal Drums/Harmonics/Vocals storage order. Both decks stay
independent while MODS or the inline strip is hidden. Turning stems off restores
normal pad dispatch, including paired releases for presses already owned by the
mod.

Physical pad colors stay green for Vocals, blue for Harmonics, red for Drums
and white for Bypass, independent of the display theme. Active pads use native
normal brightness; muted or zero-volume stems are dim. Bypass lights white
when enabled. Both A-D and E-H LED records follow the selected stem page.

Touch and pad mute controls share one latch. In the inline strip, tapping a
stem toggles it and dragging adjusts its level. Raising a muted stem with a
drag unmutes it. Inline waveform placement uses the same
widget layout as the labels and touch targets.

`test_pad_order.c` checks the physical A/B/C/D routing against onscreen actions
on both decks and verifies the full panel no longer duplicates the mixer. Pad-state checks cover both
banks, inactive-bank hot cues and release ownership across bank changes.
`test_runtime_controls.c` checks touch/pad interoperability, LED colors, scene
ownership and display bounds.
Physical acceptance of each new runtime remains separate from these tests.

## Two-deck stem controls on the native screen

The 536x268 native waveform window keeps both deck waveforms. It places a
48-pixel control row immediately after each waveform. Every row has four
buttons in the same order as the physical stem pad bank: Vocals, Harmonics,
Drums, and Bypass. Eight touch targets stay visible together on the play
screen, with no deck-switch button.

Tap a stem to mute it. Drag horizontally inside a stem button to set its
level. A held touch keeps its starting deck and stem when it crosses a row
boundary. Touches that begin on either waveform stay with the native waveform
gesture. Bypass applies only to its own deck.

The MODS panel keeps the pad-bank and show settings. Closing it or hiding the
inline rows does not change active stem levels or physical pad assignments.
The dual-row layout has portable render and touch checks; hardware audio and
screen acceptance must be recorded separately.

## Streamlined menu and themes

MODS opens to Controls, with Appearance and Advanced as its other main pages.
Open Advanced → VJ.Tools Connection for the optional computer link, including
when offline. The full-screen stem mixer is removed; Groove pads and X-PAD
remain available under Advanced with truthful readiness indicators.
Each inline stem shows its volume percentage and accepts a horizontal drag.

The original seven themes keep their saved IDs. Game Boy, Super Nintendo,
Windows 95, Game Boy Color and Aqua / iTunes add original interface chrome;
the two handheld themes also use an original pixel font. LCARS, Matrix and
Analog (IDs 12-14) are full-interface skins: each owns its MODS panel geometry
and drawing behind `ui_skin.h`, while `page_controls()` in `ui.c` still decides
which controls exist. See `SKINS.md`. The native skin
adapter applies the selection to native artwork, drawing colors, both text
paths, waveforms, markers and caution graphics as well as MODS. Physical
screen-family coverage and audio acceptance remain separate release gates.

Development deployments can use `--ram-settings` to read existing settings
while keeping changes in RAM. This avoids writing a newer theme ID into the
older USB runtime's preferences; reboot restores the original settings.

## Native interface skin adapter

`native_skin_runtime.c` builds immutable asset copies and RGB565 lookup tables
on a background worker. A guarded callback on the native UI event loop
publishes a completed theme and uses the firmware's normal repaint path.
Original assets and native color caches remain untouched. Selecting Original
regenerates the native windows from original data.

The paired receiver styles the actual RGB16 window surfaces, including native
surfaces that bypass the public CreateSurface wrapper. Per-window transparency
keys are preserved for drawing, text, and bitmap sources; opaque colors cannot
accidentally become transparent after mapping. The two native font paths keep
their original metrics and use native fallback for unsupported glyphs.

Fresh waveform buffers use bounded private copies. Partial marker updates use
pristine backing with destruction tracking. Write-only caution rectangles use
scratch buffers and merge only the pixels the native producer wrote. Album
art and calibration patterns are excluded from blanket buffer processing.

The portable suite verifies palettes, asset views, transparency and glyph
bounds. `mods/tests/verify_native_surface.py` exercises the receiver functions
themselves. Linux fixtures `test_native_skin_runtime.c`,
`test_native_raw_skin.c` and `test_native_wave_skin.c` cover theme epochs,
concurrent cache retirement, repeated redraws, retained pixels and ownership.
None of these tests substitutes for native page and audio testing on the XZ.

## Enter and leave the VJ.Tools view

The top-left VJ.Tools button appears only while the MODS VJ.TOOLS CONNECTION setting
is enabled and the network connection is live.
It reads VJ.Tools on the native screen and Exit VJ while the stream view is
enabled. The full VJ.Tools settings page stays in MODS even when the network is
offline. The physical shortcut assignment is optional. Exiting keeps incoming
frames from taking over until the user opens the view again. View choice is
saved when the settings USB is writable.

Native-view touches stay native; streamed-view touches go to the desktop.
A gesture keeps its owner until release even when it crosses the view button
or a source shortcut changes the view.

## Shared MODS layout

All themes use the LCARS control positions from `ui_layout.c`: left navigation,
top deck selectors, three-column Appearance picker and fixed control groups.
Each skin retains its colors, text and widget rendering. Non-LCARS page details
share those coordinates; inline stem rows and native main-screen layouts are
unchanged. The layout test checks identical rectangles and touch targets for
every control across all 25 themes.

FLAGSHIP (theme 24, `skin_flagship.c`) draws the same grid as the UTILITY and
browse screens of the current flagship club players: a `#191919` header and
list rows with 1 px black separators, a `#3c3c3c` category rail whose open
cell is `#646465`, `#007de1` for the selected choice row and primary buttons,
outlined ON/OFF chips for toggles, hatched `#313131` launch tiles, outlined
caption boxes (DECK, BPM, VOLUME, KEY) that carry a big value, HOT CUE style
pad chips, radio rows on the theme list, and a `#ff4619` NOT READY / READ ONLY
tag on anything the runtime cannot do yet. One `kind_style` table maps every
widget kind to its row style before anything is drawn.
