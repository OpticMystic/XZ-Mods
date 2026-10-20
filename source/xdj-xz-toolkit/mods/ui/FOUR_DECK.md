# Four decks on the classic play screen

DECK 3 and DECK 4 are software players in the runtime, heard on mixer CH3 and
CH4 through the USB loopback route. The players publish `struct xz_extra_decks`
(`../extra_decks.h`); this page is how the classic (Pioneer-drawn) play screen
shows them. Until the players exist, `../extra_decks.c` clears the snapshot and
returns 0, and the screen keeps its two decks.

## Layout

While `route_up` is set, the 536x268 scrolling-waveform surface at screen
(131,18) holds four 64-row lanes with 4-row gaps (`wave_viewport.h`,
`xz_four_layout_*`). Native DECK 1 and DECK 2 shrink into their lanes with the
same row map the stem layouts use (`xz_wave_lane_row`: the ten cue rows at each
lane edge keep their size, the 112 interior rows are resampled). Nothing native
is relaid out, the framebuffer is not scaled, and nothing is drawn outside the
surface.

Lane order is interleaved: DECK 1, DECK 3, DECK 2, DECK 4 from the top. DECK 1
and DECK 3 sit beside the native DECK 1 column (screen y 18..159), DECK 2 and
DECK 4 beside the DECK 2 column (161..302), matching the controller's left 1/3
and right 2/4 sides. Native DECK 2 keeps its first row at surface row 136, so
when the route comes up both native lanes only shrink in place. The stacked
order (1, 2, 3, 4) was prototyped too (`xz_four_layout_get(XZ_FOUR_STACKED)`);
it puts the native DECK 2 lane beside the DECK 1 info column, so the screen
reads the wrong deck's key and source next to it. Previews of both:
`preview_four_deck.py`.

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
- the playhead at the native columns 268-269, red while playing, white paused;
- a readout at the left edge on a darkened backing: the DECK 3/4 tag (lit
  pale blue when `left_on_3`/`right_on_4`), MASTER, time remaining, BPM, tempo
  and the title clipped to the backing. A clear known bit draws dashes; an
  unloaded deck draws NO TRACK.

Zoom matches the native lanes: the layered column hook records the first
sample the stock drew at columns 0, 268 and 535 each frame and takes the longer
side of the playhead, since a clamped track start or end only shortens one side
(`xz_layered_wave_view`). Before any native column is seen the lanes use one
PWV7 sample per column, the zoom measured on the device capture. The position
is `elapsed_ms` moved on by the time since `elapsed_at_ms` at the tempo, at
most one second.

## Frame and theme

The draw thread snapshots the players at the wave lock (`lock_hook`), never
waiting on IO. At unlock `xz_native_wave_finish_four` copies the surface,
shrinks the native lanes, draws DECK 3/4, then runs the theme once over the
whole surface. Protect and restore follow the `LAYERED_WAVE.md` rule for all
four lanes through the row map, so layer pixels stay exact and backgrounds,
marks and the readout are themed. The pairing, identity and transactional
rules are the stem-row path's.

Stem rows are not drawn while four decks show: there is no room for them. The
saved `stems_overlay` is untouched and returns on the first frame after the
route goes down. Pressing STEMS meanwhile still cycles the saved layout.

## Touch

`xz_native_four_order()` publishes the lane order after a successful
four-deck frame and is revoked when the play scene goes, as for the stem rows.
A contact that begins on the surface keeps the order and lane it began on. On a
native lane its y maps back to the original native row. On a DECK 3/4 lane or a
gap the mod owns it until release: stock never sees it as a deck 1 or deck 2
touch, even if the layout is revoked mid-drag.

## Cost

`preview_four_deck.py --bench N` times the layout, the two extra lanes and the
four-lane protect/restore on the host. The theme pass in four-deck mode visits
fewer body pixels than the two-deck pass (four lanes of 41 body rows against
two of 104) and runs only while a native theme is active.
