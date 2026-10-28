/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_NOTICE_H
#define XZ_RAM_NOTICE_H
/* Calm cards drawn into the play screen in both interfaces (MODERN shell lanes, CLASSIC wave surface and the
   player's own E-8307 window). Same primitives, same palette: a dark teal card with a teal edge and glyph, white
   headline, pale teal second line. Teal is not a deck, side, band, cue or error colour (deck_colour.h). */
#include "ui_skin.h"

#define XZ_CALM_FILL 0x0b2523u
#define XZ_CALM_EDGE 0x2cc4b4u
#define XZ_CALM_LINE 0x1d4f4au
#define XZ_CALM_INK 0xffffffu
#define XZ_CALM_SOFT 0xb5e4dcu

/* A deck playing its whole track from RAM after the music USB went out, with no waveform to draw. The headline
   says the track is safe; the second line says how the waveform comes back (owner's wording, 2026-10-27). */
#define XZ_RAM_NOTICE_HEAD "WHOLE TRACK IN RAM \xc2\xb7 PLAYABLE"
#define XZ_RAM_NOTICE_FULL "REINSERT THE SAME USB OR INSERT ANOTHER, THEN LOAD A TRACK FROM IT TO GET THE WAVEFORM BACK"
#define XZ_RAM_NOTICE_SHORT "LOAD A TRACK FROM THE SAME USB OR ANOTHER TO GET THE WAVEFORM BACK"
/* The card's height in a lane band lane_h tall that keeps edge rows (cue flags, beat ticks) clear above and
   below: at most 52, at least 30. */
int xz_ram_notice_height(int lane_h, int edge_rows);
/* The card at x,y, w x h: glyph, headline, and the longest second line that fits (none below 38 rows). */
void xz_ram_notice_draw(struct xz_canvas c, int x, int y, int w, int h);
/* CLASSIC's wave surface (536 wide, native_wave_runtime.c): the card in a two-deck lane lane_h rows tall at lane_y,
   its ten cue rows at each edge kept clear, and in a 64-row four-deck lane below the lane's deck tag. */
void xz_ram_notice_two_lane(struct xz_canvas surface, int lane_y, int lane_h);
void xz_ram_notice_four_lane(struct xz_canvas surface, int lane_y);
/* The RAM chip glyph, 30 x 22, its top left at x,y. */
void xz_ram_glyph(struct xz_canvas c, int x, int y, uint32_t ink, uint32_t fill);

/* The player's E-8307 window while the loopback cable explains it (../hub_cause.h): the whole window becomes
   this calm card in its own place and size. */
#define XZ_HUB_CARD_HEAD "LOOPBACK CABLE CONNECTED"
#define XZ_HUB_CARD_LINE "USED BY DECKS 3 AND 4 \xc2\xb7 NOT AN ERROR"
void xz_hub_card_draw(struct xz_canvas c);
#endif
