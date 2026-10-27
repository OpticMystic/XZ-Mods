/* SPDX-License-Identifier: MIT */
#ifndef XZ_DECK_COLOUR_H
#define XZ_DECK_COLOUR_H
/* Deck badges. In a four-deck DECK VIEW the bottom channels always follow the mixer (3 1 2 4) while the lanes
   above follow DECK VIEW, so the lane over a channel is not always that channel's deck. Every place a
   four-deck screen names a deck draws its number in a badge, the same in both interfaces.

   The badge follows the stock deck indicator on the jog displays (owner, 2026-10-26): decks 1 and 2 are a
   white digit on grey, decks 3 and 4 are inverted, a black digit on white. The digit tells the deck, the
   polarity tells the pair, and no deck owns a hue, so the badges never compete with the colours that already
   mean something on these screens (blue and pale blue for the deck a side drives, amber, red, green, the
   waveform bands, USB stick colours). A deck that is not live (3 and 4 waiting for the cable) keeps its
   badge dimmed toward what it sits on, polarity kept. A light badge on a light ground gets a dark edge.

   Contrast: white on grey 6.9 : 1, black on white 21 : 1; dimmed over black 5.3 : 1 and 7.0 : 1.
   2 DECKS screens with both sides on decks 1 and 2 stay as they were. Header only; no state. */
#include "ui_skin.h"

#define XZ_DECK_BADGE_GREY  0x5a5a5au   /* decks 1 and 2: the stock outline grey */
#define XZ_DECK_BADGE_WHITE 0xffffffu
#define XZ_DECK_BADGE_BLACK 0x000000u
#define XZ_DECK_BADGE_EDGE  0x101010u   /* near black: an edge of 0 means none */
/* Decks 3 and 4 (index 2 and 3) are drawn inverted. */
static inline int xz_deck_badge_inverted(int deck) { return deck==2||deck==3; }
static inline unsigned xz_deck_badge_luma(uint32_t c) { return (77*((c>>16)&255)+150*((c>>8)&255)+29*(c&255))>>8; }
/* The badge's fill and the colour its digit is drawn in; a deck that is not live is blended toward bg. */
static inline uint32_t xz_deck_box_fill(int deck, int live, uint32_t bg) {
 uint32_t fill=xz_deck_badge_inverted(deck)?XZ_DECK_BADGE_WHITE:XZ_DECK_BADGE_GREY;
 return live?fill:xz_blend(bg,fill,150);
}
static inline uint32_t xz_deck_box_ink(int deck, int live, uint32_t bg) {
 uint32_t ink=xz_deck_badge_inverted(deck)?XZ_DECK_BADGE_BLACK:XZ_DECK_BADGE_WHITE;
 return live?ink:xz_blend(bg,ink,170);
}
/* The dark edge a light badge needs on a light ground (a pale blue driven tab, a light theme), else 0. */
static inline uint32_t xz_deck_box_edge(int deck, int live, uint32_t bg) {
 return xz_deck_badge_luma(xz_deck_box_fill(deck,live,bg))>140&&xz_deck_badge_luma(bg)>140?XZ_DECK_BADGE_EDGE:0;
}
/* The badge with an atlas digit (CLASSIC tags and channels, the menus): scale 1 (12 px) or 2 (20 px), the
   digit centred in w x h. edge, when not 0, outlines it; else a light badge on a light bg gets the dark edge. */
static inline void xz_deck_box(struct xz_canvas c, int x, int y, int w, int h, int deck, int live, uint32_t bg, int scale, uint32_t edge) {
 char digit[2]={(char)('1'+((unsigned)deck<4u?deck:0)),0};
 int cap=scale>1?14:xz_theme_pixel_text(c.theme)?7:9;
 if(!edge)edge=xz_deck_box_edge(deck,live,bg);
 xz_rect(c,x,y,w,h,xz_deck_box_fill(deck,live,bg));
 if(edge)xz_border(c,x,y,w,h,edge);
 xz_text(c,x+(w-xz_text_width(c,digit,scale,2)+1)/2,y+(h-cap+1)/2,digit,scale,2,xz_deck_box_ink(deck,live,bg));
}
#endif
