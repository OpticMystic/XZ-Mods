/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_CLASSIC_COLUMN_H
#define XZ_UI_CLASSIC_COLUMN_H
/* CLASSIC four-deck side column (design 6.2): in a four-deck DECK VIEW the native side column x 0..129,
   y 22..308 is the mod's. It is cleared and redrawn as four stock-style tags, one beside each lane, in the
   lanes' order, so every lane has its deck's source and key beside it. 2 DECKS leaves the stock column
   alone. Pure renderer and header-only hit geometry; no IO, no allocation. */
#include "deck_layout.h"
#include <stddef.h>
#include <stdint.h>

#define XZ_CLASSIC_COLUMN_X 0
#define XZ_CLASSIC_COLUMN_Y 22
#define XZ_CLASSIC_COLUMN_W 130
#define XZ_CLASSIC_COLUMN_H 287           /* y 22..308 */
#define XZ_CLASSIC_COLUMN_TAG_H 60        /* tag k spans y 22+68k .. +60, beside lane k */
struct xz_classic_column_deck {
 int live;                                /* the deck plays now (decks 3 and 4: their route is up) */
 int driven;                              /* a physical side drives it */
 int source;                              /* enum xz_shell_source; 0 unknown */
 char key[8], original[8];                /* playing key and the analysed one; "" unknown */
 uint32_t key_color;
 int cents, cents_known;                  /* native decks: tuning left after the semitone */
};
struct xz_classic_column_view { int layout; struct xz_classic_column_deck deck[4]; };
/* The tag row k (0..3) of a lane order; -1 outside the column or between tags. */
static inline int xz_classic_column_row_at(int x,int y){
 if(x<XZ_CLASSIC_COLUMN_X||x>=XZ_CLASSIC_COLUMN_X+XZ_CLASSIC_COLUMN_W||y<22||y>=XZ_CLASSIC_COLUMN_Y+XZ_CLASSIC_COLUMN_H)return -1;
 int k=(y-22)/68;return k<4&&(y-22)%68<64?k:-1;
}
/* The logical deck (0..3) whose tag is at x,y, or -1. */
static inline int xz_classic_column_deck_at(int layout,int x,int y){
 int k=xz_classic_column_row_at(x,y);
 return layout==XZ_LAYOUT_TWO||k<0?-1:xz_deck_layout_order(layout)[k];
}
/* Writes only x 0..129, y 22..308. 1 when drawn. */
int xz_classic_column_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_classic_column_view *);

/* 2 DECKS while a side controls DECK 3 or 4 (FOUR_DECK.md "2 DECKS follows the sides"): that side's half of the
   stock column is the mod's, redrawn in the stock look with the deck in place of DECK 1 or 2: the tab (its
   number box where the stock tab has its bar, then DECK and the number), source, key, and the key slot that
   opens the key controls (play_key_classic.h xz_classic_key_box, the stock key strip's place). Stock elements
   repeat 143 rows apart; the left half also clears the rows under the top bar. */
#define XZ_CLASSIC_HALF_STEP 143
static inline int xz_classic_column_half_y(int side){return side?160:18;}
static inline int xz_classic_column_half_h(int side){return side?149:142;}   /* y 18..159, 160..308 */
/* The side whose half holds x,y, or -1. */
static inline int xz_classic_column_half_at(int x,int y){
 if(x<XZ_CLASSIC_COLUMN_X||x>=XZ_CLASSIC_COLUMN_X+XZ_CLASSIC_COLUMN_W)return -1;
 for(int side=0;side<2;side++)if(y>=xz_classic_column_half_y(side)&&y<xz_classic_column_half_y(side)+xz_classic_column_half_h(side))return side;
 return -1;
}
/* deck: 2 (DECK 3, left) or 3 (DECK 4, right). Writes only that half, x 0..129. 1 when drawn. */
int xz_classic_column_half_draw(uint16_t *pixels,size_t stride,size_t count,int deck,const struct xz_classic_column_deck *);
/* After a theme map over the half: the number box again, in its own colours. Writes only the box. */
void xz_classic_column_half_identity(uint16_t *pixels,size_t stride,size_t count,int deck);
#endif
