# Four decks on the Classic play screen

This interface belongs to the USB-loaded RAM runtime. Powering off removes
the runtime changes. It requires no permanent controller or PC app install.

DECK 3 and DECK 4 are software players in the runtime, heard on mixer CH3 and
CH4 through the USB loopback route. The players publish `struct xz_extra_decks`
(`../extra_decks.h`); this page is how the classic (Pioneer-drawn) play screen
shows them. DECK VIEW (`deck_view.h`) chooses what the play screen shows: 2 DECKS
shows the two decks the physical sides control (see "2 DECKS follows the sides";
with both sides on DECK 1 and 2 it is the unchanged native two-deck path with the
saved stem layout); 4 DECKS in
number order (1 2 3 4) or mixer order (3 1 2 4) shows four lanes whether or not
decks 3 and 4 are connected. While they are not, their lanes carry the card
described under "Decks 3 and 4 not connected".

## Layout

While DECK VIEW is FOUR or MIXER (route up or not), the 536x268 scrolling-waveform surface at screen
(131,18) holds four 64-row lanes with 4-row gaps (`wave_viewport.h`,
`xz_four_layout_*`). Native DECK 1 and DECK 2 shrink into their lanes with the
same row map the stem layouts use (`xz_wave_lane_row`: the ten cue rows at each
lane edge keep their size, the 112 interior rows are resampled). The wave
transformation stays inside that surface. The framebuffer is not scaled.

The shared `xz_deck_layout_get()` preference is DECK VIEW: TWO (default),
FOUR (1 2 3 4) or MIXER (3 1 2 4). The wave lock freezes it in
`four_frame_layout`, and the draw thread's scene predicate admits a four-deck
view on its own, so four lanes are drawn even with the route down.
`xz_deck_layout_order(layout)` in `deck_layout.h` orders the upper lanes.
Every view change goes through `deck_view_apply()` in `ui_runtime.c`, the one
entry point (CLASSIC picker and card, MODERN through `XZ_SHELL_UI_DECK_VIEW`,
MODS DECK VIEW, the button next to LINK and the retired wave order actions). It
applies the saved `deck_view` setting (`deck_view_save_locked`, then
`settings_queue`); `xz_deck_layout_get()` returns the applied view every frame.
The DECKS button is always on the play screen in both interfaces. The retired `deck_view_button` setting
(SETTINGS.md) is read as shown, so a unit saved with HIDE shows the button again. Every lane names its deck. A pale
blue tag shows which deck the left or right physical controls drive.
`preview_four_deck.py` checks the upper waveform layout in both orders.

### Deck badges

The channels stay in mixer order while DECK VIEW reorders the lanes, so in
number order the lane over channel "1" is DECK 1 and the channel under it is
DECK 3. Every four-deck screen therefore draws each deck's number in a badge
(`deck_colour.h`), the same in both interfaces. The badge follows the stock
deck indicator on the jog displays, which inverts decks 3 and 4 (owner,
2026-10-26): DECK 1 and DECK 2 are a white digit on grey `#5a5a5a` (6.9 : 1),
DECK 3 and DECK 4 a black digit on white (21 : 1). The digit names the deck and
the polarity names the pair; no deck has a hue of its own, so the badges never
compete with the colours that already mean something here. A deck that is not
connected keeps its badge dimmed toward its background, polarity kept. A light
badge on a light ground (the pale blue driven tab, a light theme's menu) gets a
dark one-pixel edge. The badges sit on the lane tags (MODERN info column,
CLASSIC side column and wave lane tags), on the channels (MODERN strip badge,
CLASSIC title band, and the MODERN four-deck strip under browse and the menus),
in the detail panel's header and lane numbers, on the WORKS ON chips and the
4 DECKS grid's column heads in MODS, on the DECKS button's numbers and on the
picker's lane and channel numbers. The driven-deck highlight (blue or pale blue
fill with the arrow, LEFT or RIGHT) and USB stick colours stay on the rest of
the tag. The CLASSIC theme pass recolours the wave surface, so `style_four`
puts the badges back afterwards (`xz_four_identity_draw`). 2 DECKS screens with
both sides on DECK 1 and 2 are unchanged; a 2 DECKS half showing DECK 3 or 4
carries its badge (below).

## 2 DECKS follows the sides

2 DECKS shows the decks the two physical sides control now. SHIFT pressed twice
on a side (`deck_select.h`, the stock-style switch; same presses as stock) or
SHORTCUT > DECK SELECT moves the left side between DECK 1 and DECK 3 and the
right side between DECK 2 and DECK 4 while the CH3/CH4 route is up, and the
screen follows: the whole half of that side switches, waveform, side column and
bottom panel together. No new gesture: a double press of SHORTCUT would change a
stock press sequence (decision D11).

CLASSIC. The stock firmware draws DECK 1 and DECK 2, so a side on DECK 3 or 4
gets a mod-drawn half in the stock look; the other side stays stock:

- Wave surface: the side's half (rows 0..131 left, 132..267 right) is the
  software deck as a whole 132-row native lane (`xz_extra_lane_draw_full`, the
  row map is one to one), drawn in the wave transaction before the one theme
  pass (`xz_native_wave_finish_pair`): the native side's layered pixels are kept
  for the native side only (`xz_layered_wave_protect_decks`), the software layers
  by `xz_two_protect`, then the native side alone takes its stem layout and rows
  (`xz_wave_layout_pixels_decks`). A software half has no stem rows and no lane
  tag: the side column beside it names the deck, as it names DECK 1 and 2. The
  right half's separator rows 132..135 stay black. The draw thread publishes the
  pair it painted (`xz_native_two_pair`, bit 0 left on DECK 3, bit 1 right on
  DECK 4), revoked with the play scene; everything else follows that one value.
- Side column: the side's half (x 0..129, y 18..159 or 160..308) is redrawn as
  the stock tag (`xz_classic_column_half_draw`): the stock tab shape and gradient
  with the deck's badge where the stock tab has its bar, DECK 3 or DECK 4,
  source, key, and the key slot that opens the key controls for that deck.
- Bottom panel: the side's 400-pixel stock panel (`xz_classic_mixer_half_draw`):
  title band with the deck's badge where the note is, DECK n, the time mode,
  QUANTIZE, TEMPO and range, source and key where TRACK is, A.CUE and MT boxes,
  the clock, what SYNC follows and what the pads do under it (END when the track
  ends), tempo, the BPM box (orange with MASTER for a master deck) and the
  whole-track overview with cue marks, position line and bar.
- The column and panel halves are drawn by `xz_classic_mixer_runtime_pair_frame`
  only for sides both the painted pair and the players' snapshot put on DECK 3
  or 4, never over MODS, a game, a native caution or a takeover. In a theme they
  go through the active theme map like the stock halves beside them, then the
  deck badges are drawn again in their own colours. A half returning to stock
  (a switch back, route loss, MODS) asks for one native repaint.
- Touch: a contact that begins on a software half (its wave rows, its column
  half, its panel) is the mod's until release, also if the half returns to stock
  under the finger; stock never sees it as a DECK 1 or 2 touch. The panel's
  overview and bar (x 10..309 of the panel, y 421..466) needle-search the
  software deck under NEEDLE LOCK, a refusal raising its line in the top bar's
  notice; the key slot opens the key controls first; the rest is inert. The
  header strip keeps its touches. The DECKS button names the pair ("DECKS 3 2").
- Route loss: the players drop both sides to DECK 1 and 2 (`deck_select` epoch);
  the next wave frame is the stock frame, the halves hide and a repaint returns
  the stock column and panel.

MODERN. The shell already draws the assigned pair from one model, so the
waveform lane, info column and bottom strip switch in the same frame; DECK 3 or
4 there now carries its badge on its tag and strip badge.

Badges in 2 DECKS: DECK 3 and DECK 4 carry their badge (also on the DECKS
button) whenever they are on screen; DECK 1 and 2 keep the plain stock number.
A 2 DECKS screen can now read 1 2, 3 2, 1 4 or 3 4, so the white badge tells at
a glance that a half shows a software deck and which one, the same badge the DJ
meets on the four-deck screens and the mixer. Keeping 1 and 2 plain leaves the
normal case byte-identical and marks only what differs from the stock screen.

Switch notice: when a side switches while 2 DECKS shows and the route stays up,
both interfaces say where it went for 2 s (`xz_deck_view_pair_notice`):
"LEFT: DECK 3", "RIGHT: DECK 2", or both sides when both changed. CLASSIC uses
the wave surface's banner (the newer of it and a player notice shows), MODERN
the shell's notice. Four-deck views say nothing (the lanes do not move), and
route loss leaves the players' "DECK 3/4 ROUTE LOST".

In a four-deck view the native side column (x 0..129, y 22..308) is the mod's:
`classic_column.c` clears it and draws four stock-style tags, one beside each
lane in the lanes' order (so mixer order reads 3, 1, 2, 4): the deck tag (pale
blue with LEFT or RIGHT for the deck a side drives), source and key, and for
DECK 1 and 2 the analysed key, a triangle, the playing key and its cents;
DECK 3 and 4 say SOFTWARE DECK, or NOT CONNECTED / SEE THE CARD while down.
A tag for DECK 1 or 2 is the stock column's own tap at (60,40) or (60,180);
DECK 3 or 4 open their detail panel as their channel title does.

The lower 169 rows (and rows 309..310 above them) are four equal 200-pixel
channels that always mirror the physical mixer, **3, 1, 2, 4**, in both
four-deck views (decision D3b); DECK VIEW reorders only the upper lanes.
While decks 3 and 4 are down their channels show a dim band with NOT
CONNECTED and "DECK n WAITS FOR THE LOOPBACK CABLE."; decks 1 and 2 stay live.
The column and band are repainted on every overlay frame, so stock text under
them never shows through.
`classic_mixer.c` draws matching blue title strips, 28-pixel clocks, BPM,
pitch, range, mode indicators and full-track overviews. Native and software
decks use the same layout. The native clock and title come from the existing
guarded `shell_model_refresh` reader at most ten times per second. AUTO CUE
is exposed per deck from the already-qualified frame bit, including when
the two native decks have different settings. Unknown fields remain blank.

The existing 300 by 34 native overview capture supplies native waveform
pixels and its original screen coordinates. Reducing it to 188 columns
approximates its markers and colours. Software overview sampling is bounded
to eight samples per column, with cue, hot-cue and loop marks. The detail
panel retains information that cannot fit in a channel panel. Tempo ranges
use a drawn plus/minus sign in every theme, including pixel fonts. The header
omits unlabeled track numbers. At native track end, the panel owns and clears
the overview rectangle and draws a complete END label instead of resampling
the stock counter text that overlaps the retained screenshot.

## DECK 3 and DECK 4 lanes

`extra_lane.c` draws a software deck as a native lane looks after it shrinks:
the 132-row lane geometry (body around row 65, beat ticks in rows 4..13 and
118..127, cue bands in the outer ten rows) resampled into 64 rows. It draws:

- the layered 3-band body (`LAYERED_WAVE.md` colours; STOCK and STEMS looks
  also draw 3-band, since the players publish PWV7 only), scaled by the
  player's `wave.normalization`;
- the native column background gradient, tinted 0xb300 inside an active loop
  with 1-pixel loop in and out lines;
- beat ticks from the grid (bar starts red and two pixels wide), hot-cue flags
  lettered A-H in the top cue band, the cue point as a triangle in the bottom
  band;
- the playhead at native columns 268-269, red while playing, white paused;
- a small deck tag. The main readouts live in the bottom channels, leaving
  waveform history visible. LOOP IN and OUT adjustment highlight the edge
  the jog moves; stored loops retain dim boundary marks. Uncoloured point
  cues are green and loop cues orange, matching the physical pad lamps.

The bottom readouts and detail panel distinguish beat lock, tempo-only SYNC,
waiting and blocked reasons. A software master reads `3/4 MASTER`; it does
not claim to drive native DECK 1 or DECK 2. HOT CUE CALL is separate from pads
off. The detail panel also shows the complete pad bank and deck modes.

Zoom matches the native lanes: the layered column hook records the first
sample the stock drew at columns 0, 268 and 535 each frame and takes the longer
side of the playhead, since a clamped track start or end only shortens one side
(`xz_layered_wave_view`). Before any native column is seen the lanes use one
PWV7 sample per column, the zoom measured on the device capture. The position
uses the existing qualified timeline with bounded extrapolation, including
reverse and loop wrap. Legacy snapshots use `elapsed_ms` advanced from
`elapsed_at_ms` at the tempo, for at most one second.

## Frame and theme

The draw thread takes an analysis-held snapshot at the wave lock (`lock_hook`),
never waiting on IO. It releases the hold on capture failure or after unlock.
The separate bottom-panel adapter also holds its snapshot through rendering
and releases it on every exit, including a false snapshot return with holds.
Ordinary snapshots intentionally contain no wave/grid array pointers. At unlock `xz_native_wave_finish_four` copies the surface,
shrinks the native lanes, draws DECK 3/4 with `xz_four_frame_draw`, then runs the theme once over the
whole surface. Protect and restore follow the `LAYERED_WAVE.md` rule for all
four lanes through the row map, so layer pixels stay exact and backgrounds,
marks and the readout are themed. The pairing, identity and transactional
rules are the stem-row path's.

Stem rows are not drawn while four decks show: there is no room for them. The
saved `stems_overlay` is untouched and returns on the first frame of 2 DECKS.
Pressing STEMS meanwhile leaves it alone and says STEM BUTTONS SHOW IN 2 DECKS
VIEW, and the STEMS lamp and its layout marks stay dark, because no stem
buttons are on screen (`wave_viewport.h` `xz_stems_press`,
`xz_classic_stems_lamp`). MODERN's four lanes do draw stem buttons; there STEMS
shows or hides them (`settings.h` `XZ_HIDE_FOUR_STEMS`).

## Touch

A tap on a DECK 3 or DECK 4 lane opens its detail panel. Tapping the same lane
closes it; tapping the other software lane switches it. A software channel's
bottom title also opens its detail. CLOSE dismisses
the panel. It covers surface columns 0 through 265. All four playheads and
future waveform data remain visible, with deck-number tabs at the right edge.

The panel shows mode descriptions, pad meanings, SYNC reasons, loop state
and a whole-track overview. The physical transport, loop, search, pad and
mode buttons continue to control these functions. HOT CUE CALL recalls pads
without moving playback. HOT CUE finishes CALL.

LEFT SIDE or RIGHT SIDE buttons select the named native or software deck.
`xz_runtime_deck_select_target` checks the requested target under the same
mutex as physical switching. Repeating a selection does not toggle it back.
The UI calls this adapter after releasing `ui_mutex`.

Touching or dragging the overview searches that software deck. NEEDLE LOCK
refuses playing, unknown playback state and released platter coast. A paused
deck or physical TOP hold can search. A refusal appears in the panel. The
overview samples at most eight source columns per screen column. Drawing
performs no file IO or allocation.

The four bottom overviews use the same needle-lock rule for software decks.
Each bottom contact freezes its painted layout and logical deck. Native
overview gestures map through the original coordinates captured with
the native waveform window. A drag retains its original logical deck when
crossing a channel boundary. Route loss or MODS ends a held native gesture
once, and consumes the remaining contact. Native readouts outside the
overview are labels; physical buttons continue to control transport and modes.

`xz_native_four_order()` publishes the lane order after a successful
four-deck frame and is revoked when the play scene goes, as for the stem rows.
A contact that begins on the surface keeps the order and lane it began on. On a
native lane its y maps back to the original native row. On a DECK 3/4 lane or a
gap the mod owns it until release: stock never sees it as a deck 1 or deck 2
touch, even if the layout is revoked mid-drag.

Each panel contact retains its original control and deck. Moving or releasing
outside a button cancels it. A release with cleared coordinates uses the last
contact position. Changing wave order cancels a pending action. Route loss,
a hidden play scene, MODS or takeover revokes the panel. Owned contacts stay
consumed through release. The header's METRICS controls retain their touch
area in either order.

The mixer adapter stops drawing over a native caution, MODS, games or takeover.
On revocation it requests a full native repaint to restore the two-deck
bottom layout. It invalidates captured native overview pixels on a track
change before accepting a newly captured overview.

The play-key popup paints after the mixer. A held mixer contact is cancelled
before popup, MODS, game, interface or takeover input can consume it. A native
overview receives exactly one synthetic release; the cancelled contact remains
owned through its physical release. The cached-render path retains the existing
popup/key/layout busy-owner guard.

## DECK VIEW button and picker

`deck_view.h` draws the button and the three-picture picker for both
interfaces from one style each. CLASSIC: the button (162,0,116,18) sits in the
empty black between VJ.TOOLS and the METRICS readout on every Classic play
screen, 2 DECKS included; its touch reaches y 39, except where an OUTER stem
row lies under it (the row keeps its touches). One tap opens the picker
(140,22,500,206); a picture applies, a tap outside or BACK closes it, and six
seconds without input close it. The encoder turns and pushes inside the open
picker. 2 DECKS frames with both sides on DECK 1 and 2 differ from the stock
screen only in the button rect.
When decks 3 and 4 come up while 2 DECKS shows, the wave surface's notice
banner says "DECKS 3 AND 4 ARE READY. TAP DECKS AT THE TOP FOR FOUR DECKS."
for 3.5 s, once per route-up; the view does not change by itself. The latch
(`xz_deck_view_ready_notice_take`) is shared with MODERN, so one route-up raises
one notice.

## Decks 3 and 4 not connected

`route_card.h` draws the card in the lanes of decks 3 and 4 only: one card in
number order, two strips in mixer order. Row A: the cable route picture and the
two steps. Row B: the state from `xz_extra_decks_route_status()` (NOT DETECTED,
SCANNING, STARTING, WAITING for decks 1 and 2 to stop, FAILED with the failed
part marked, LOST, NO ENGINE), the runtime's sentence, RESCAN or TRY AGAIN, and
TWO DECKS. RESCAN only wakes the connector (`xz_extra_decks_rescan`); it never
restarts the helper. The card is drawn inside the wave transaction: the lane
tint before the theme, the card after protect/restore, never a full-screen
blend. Its still is cached once per state; each frame copies it and draws the
pulse, the scanning ring and the sweep, which rest 60 s after the card appears
or after the last input. When the route comes up under the card it turns
READY for 700 ms and wipes away in 8 frames; the side tags and channels of
decks 3 and 4 go live after it, and the banner says "DECKS 3 AND 4 ARE READY"
for 2.5 s. The draw thread publishes the painted card
(`xz_native_four_card`) and touch acts only on that; the encoder's focus on the
card is reported back with `xz_native_four_card_input`. While the card shows,
an encoder push focuses RESCAN; turn moves across the card's buttons and the
DECK VIEW button; push presses; BACK or six idle seconds give the encoder back
to the player. Without the card the encoder stays the player's.

## Cost

`preview_four_deck.py --bench N` times the layout, the two extra lanes and the
four-lane protect/restore on the host, and with decks 3 and 4 down the native
tags with the lane tint and the card (copied from its cached still, and drawn
whole for comparison). The theme pass in four-deck mode visits fewer body
pixels than the two-deck pass (four lanes of 41 body rows against two of 104)
and runs only while a native theme is active. MODERN's `shell/bench_deck_view.c`
times PLAY frames with the card, 2 DECKS and the open picker.

## Verification

`verify.py --zig <zig>` includes lane and detail render checks, bounds,
unknown fields, pad labels, mode states and waveform noninterference.
It also checks mixer order, channel clipping, large clocks and overview
geometry. `verify_classic_mixer_runtime.py` runs the actual runtime adapter
against the existing guarded native-memory fixtures under sanitizers.
`verify_runtime_controls.py`, `verify_menu_runtime.py` and
`verify_sdk_runtime.py` exercise actual UI runtime touch routing under Linux
AddressSanitizer and UBSan. `../tests/verify_classic_selection.py --zig <zig>`
checks the actual selection adapter with a physical switch between its
snapshot and lock. `../verify_classic_load.py --zig <zig>` covers native LOAD.

`preview_classic_mixer.py --zig <zig> --screen <capture.png> --layout four --out <new-folder>`
renders the complete numeric four-channel layout through the actual C composers.
Use --layout mixer for 3-1-2-4 order.
It uses fixture data composited onto a retained native capture. These images
are offline rendering evidence. The wave-only preview's `--bench N` measures
host rendering cost only. ARM compilation and QEMU execution are separate from USB boot,
physical touch, audible playback and controller performance.
