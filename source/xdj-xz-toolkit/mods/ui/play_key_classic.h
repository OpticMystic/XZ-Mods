/* SPDX-License-Identifier: MIT */
#ifndef XZ_PLAY_KEY_CLASSIC_H
#define XZ_PLAY_KEY_CLASSIC_H
#include "play_key.h"
#include "key_strip.h"
#include "../extra_decks.h"
#include "../key/view.h"

/* The four-lane renderer changes only the 536x268 wave surface at 131,18.
 * The reserved stock key-feedback rectangles remain at their original places.
 * Their visible logical labels and their touch targets share this value copy. */
/* shown: sides whose key box takes touches. painted: sides drawn with the slot's own tile; the others show the
   native key feedback that the key strip cache can replay on a busy frame. */
struct xz_classic_key_slots { unsigned shown; int deck[2]; unsigned painted; };
static inline int xz_classic_key_target(const struct xz_extra_decks *extra,int side){
 return side+2*(extra&&(side?extra->right_on_4:extra->left_on_3));
}
static inline struct xz_key_strip_box xz_classic_key_box(int side){
 return (struct xz_key_strip_box){4,119+143*side,107,35,XZ_KEY_STRIP_PLAY};
}
static inline int xz_classic_key_slot_at(const struct xz_classic_key_slots *slots,int x,int y){
 for(int side=0;side<2;side++){
  struct xz_key_strip_box b=xz_classic_key_box(side);
  if((slots->shown&(1u<<side))&&x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h)return side;
 }
 return -1;
}
static inline const char *xz_classic_metadata(const struct xz_extra_deck *d){
 return d&&d->loaded&&(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY))&&d->key[0]?xz_extra_key_text(d):"---";
}
static inline void xz_classic_key_render(struct xz_classic_key_slots *slots,const struct xz_ui_model *m,
 const struct xz_extra_decks *extra,int play,int four,uint16_t *pixels,size_t stride){
 slots->shown=slots->painted=0;if(!play||!pixels||stride<800||(four>=0&&!extra))return;
 const struct xz_theme_palette *pal=xz_theme_palette(m->theme);
 struct xz_canvas c={pixels,stride,800,480,m->theme};
 for(int side=0;side<2;side++){
  struct xz_key_strip_box b;
  if(!xz_key_strip_area(pixels,stride,m->theme,side,&b)||b.kind!=XZ_KEY_STRIP_PLAY)continue;
  int deck=xz_classic_key_target(extra,side);slots->deck[side]=deck;slots->shown|=1u<<side;
  /* badge() already painted the native feedback. Keep its applied cents,
   * SYNC/master role, colours and two-line errors intact. */
  if(deck<2&&m->key_feedback)continue;
  char label[32];snprintf(label,sizeof label,"D%d KEY >",deck+1);
  const char *key=m->key_feedback?m->deck[deck].key_readout:deck>1?xz_classic_metadata(&extra->deck[deck-2]):"OPEN CONTROLS";
  if(!key||!*key)key="---";
  slots->painted|=1u<<side;
  xz_rect(c,b.x,b.y,b.w,b.h,pal->bg);
  xz_ui_chrome_fit(c,b.x+3,b.y+2,b.w-6,label,1,pal->accent);
  xz_ui_chrome_fit(c,b.x+3,b.y+17,b.w-6,key,1,m->key_feedback&&m->browse_traffic?m->deck[deck].key_color:pal->ink);
 }
}
#endif
