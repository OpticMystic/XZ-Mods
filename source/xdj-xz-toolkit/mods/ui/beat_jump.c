#include "beat_jump.h"
int xz_jump_event(struct xz_jump_state *s,const struct xz_jump_settings *cfg,const struct xz_cue_event *e,int *beats) {
 *beats=0;
 if(e->deck<0||e->deck>1)return 0;
 int d=e->deck;
 if(e->mode_button>=0&&e->mode_button<4){
  unsigned bit=1u<<e->mode_button;
  if(e->operation==2||e->operation==3)s->mode_down[d]&=~bit;
  else if(e->operation==0&&!(s->mode_down[d]&bit)){
   s->mode_down[d]|=bit;
   s->page[d]=cfg->enabled&&cfg->shift_page&&e->shift&&e->mode_button==3?1:0;
  }
  return 0; /* Stock selects BEAT JUMP, including when SHIFT is held. */
 }
 if(e->pad<0||e->pad>=8)return 0;
 unsigned bit=1u<<e->pad;
 int owned=!!(s->owned[d]&bit);
 if(e->operation==2||e->operation==3){s->down[d]&=~bit;s->owned[d]&=~bit;return owned;}
 if(e->operation!=0||s->down[d]&bit)return owned;
 s->down[d]|=bit;
 if(!cfg->enabled||e->pad_page!=3||e->shift)return 0;
 int page=cfg->shift_page?s->page[d]:0;
 int amount=xz_jump_beats(cfg->sizes[page*4+e->pad/2]);
 if(!amount)return 0;
 s->owned[d]|=bit;
 *beats=e->pad%2?amount:-amount;
 return 1;
}
