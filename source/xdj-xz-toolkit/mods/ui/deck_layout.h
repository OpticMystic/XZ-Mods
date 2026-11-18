/* SPDX-License-Identifier: MIT */
#ifndef XZ_DECK_LAYOUT_H
#define XZ_DECK_LAYOUT_H
enum xz_deck_layout { XZ_LAYOUT_TWO, XZ_LAYOUT_FOUR, XZ_LAYOUT_MIXER, XZ_LAYOUT_COUNT };
/* Pure presentation order, shared by upper lanes, bottom panels and hit geometry.
   TWO retains native 1/2 first; callers decide whether four panels are visible. */
static inline const unsigned char *xz_deck_layout_order(int layout){
 static const unsigned char numeric[4]={0,1,2,3},mixer[4]={2,0,1,3};
 return layout==XZ_LAYOUT_MIXER?mixer:numeric;
}
#endif
