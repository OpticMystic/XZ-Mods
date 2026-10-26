# Four decks on the Classic play screen

This interface belongs to the USB-loaded RAM runtime. Powering off removes
the runtime changes. It requires no permanent controller or PC app install.

DECK 3 and DECK 4 are software players in the runtime, heard on mixer CH3 and
CH4 through the USB loopback route. The players publish `struct xz_extra_decks`
(`../extra_decks.h`); this page is how the classic (Pioneer-drawn) play screen
shows them. When the route is down, or TWO is selected, the screen uses the
unchanged native two-deck path with the saved stem layout.

## Layout

While `route_up` is set and the shared layout is FOUR or MIXER, the 536x268 scrolling-waveform surface at screen
(131,18) holds four 64-row lanes with 4-row gaps (`wave_viewport.h`,
`xz_four_layout_*`). Native DECK 1 and DECK 2 shrink into their lanes with the
same row map the stem layouts use (`xz_wave_lane_row`: the ten cue rows at each
lane edge keep their size, the 112 interior rows are resampled). The wave
transformation stays inside that surface. The framebuffer is not scaled.

The shared `xz_deck_layout_get()` preference selects TWO, FOUR (1 2 3 4),
or MIXER (3 1 2 4). The wave lock freezes it in `four_frame_layout`.
`xz_deck_layout_order(layout)` in `deck_layout.h` is the pure order function
used by upper-lane geometry, bottom rendering and bottom touch geometry.
Changing preference storage belongs in `xz_deck_layout_get()`, not another
mixer-order state. Every
lane names its deck. A pale blue tag shows which deck the left or right
physical controls drive. The native side columns still describe DECK 1 and
DECK 2. `preview_four_deck.py` checks the upper waveform layout in both orders.

The lower 169 rows are four equal 200-pixel channels in the same order as
the upper lanes: **1, 2, 3, 4** in FOUR, **3, 1, 2, 4** in MIXER.
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
saved `stems_overlay` is untouched and returns on the first frame after the
route goes down. Pressing STEMS meanwhile still cycles the saved layout.

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

## Cost

`preview_four_deck.py --bench N` times the layout, the two extra lanes and the
four-lane protect/restore on the host. The theme pass in four-deck mode visits
fewer body pixels than the two-deck pass (four lanes of 41 body rows against
two of 104) and runs only while a native theme is active.

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
