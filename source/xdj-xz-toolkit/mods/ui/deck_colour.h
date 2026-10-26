/* SPDX-License-Identifier: MIT */
#ifndef XZ_DECK_COLOUR_H
#define XZ_DECK_COLOUR_H
/* Deck identity colours. In a four-deck DECK VIEW the bottom channels always follow the mixer (3 1 2 4)
   while the lanes above follow DECK VIEW, so the lane over a channel is not always that channel's deck.
   Every place a four-deck screen names a deck draws its number in a box of the deck's colour, the same in
   both interfaces, so a lane and its channel match at a glance in either order.

   One fixed colour per logical deck, always with the deck's digit on it, never colour alone: warm for the
   left side's decks (1 yellow, 3 rose), cool for the right side's (2 aqua, 4 violet). They stay apart under
   simulated protanopia, deuteranopia and tritanopia (smallest CIEDE2000 between two decks: 22) and away
   from the colours that already mean something on these screens: blue and pale blue (the deck a side
   drives), amber (waiting), red (master, playhead, end), green (ready, sync), the waveform bands. A deck
   that is not live (3 and 4 waiting for the cable) keeps its colour, dimmed.

   The box carries its own fill and a dark digit, so it reads on any theme. 2 DECKS screens stay as they
   were: with two decks nothing is reordered. Header only; no state. */
#include "ui_skin.h"

#define XZ_DECK_COLOUR_INK 0x101010u   /* the digit on a lit box */
static inline uint32_t xz_deck_colour(int deck) {
 static const uint32_t colour[4]={0xffe23a,0x48eef0,0xe87c92,0xad78ff};
 return (unsigned)deck<4u?colour[deck]:0x8c8c8c;
}
/* A deck that is not live, over the background bg: the box's fill, its digit, and plain text. */
static inline uint32_t xz_deck_colour_dim_fill(int deck, uint32_t bg) { return xz_blend(bg,xz_deck_colour(deck),64); }
static inline uint32_t xz_deck_colour_dim_ink(int deck, uint32_t bg) { return xz_blend(bg,xz_deck_colour(deck),150); }
static inline uint32_t xz_deck_colour_text(int deck, int live, uint32_t bg) {
 return live?xz_deck_colour(deck):xz_blend(bg,xz_deck_colour(deck),112);
}
/* The number box's fill, and the colour its digit is drawn in. */
static inline uint32_t xz_deck_box_fill(int deck, int live, uint32_t bg) { return live?xz_deck_colour(deck):xz_deck_colour_dim_fill(deck,bg); }
static inline uint32_t xz_deck_box_ink(int deck, int live, uint32_t bg) { return live?XZ_DECK_COLOUR_INK:xz_deck_colour_dim_ink(deck,bg); }
/* The box with an atlas digit (CLASSIC tags and channels, the menus): scale 1 (12 px) or 2 (20 px), the
   digit centred in w x h. edge, when not 0, outlines it (for light backgrounds). */
static inline void xz_deck_box(struct xz_canvas c, int x, int y, int w, int h, int deck, int live, uint32_t bg, int scale, uint32_t edge) {
 char digit[2]={(char)('1'+((unsigned)deck<4u?deck:0)),0};
 int cap=scale>1?14:xz_theme_pixel_text(c.theme)?7:9;
 xz_rect(c,x,y,w,h,xz_deck_box_fill(deck,live,bg));
 if(edge)xz_border(c,x,y,w,h,edge);
 xz_text(c,x+(w-xz_text_width(c,digit,scale,2)+1)/2,y+(h-cap+1)/2,digit,scale,2,xz_deck_box_ink(deck,live,bg));
}
#endif
