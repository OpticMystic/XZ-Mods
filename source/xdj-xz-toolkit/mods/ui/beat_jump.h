#ifndef XZ_BEAT_JUMP_H
#define XZ_BEAT_JUMP_H
#include "../cue/cue.h"
#define XZ_JUMP_SIZES 10
/* enabled controls page 1 only; shift_page is a retired version-1 wire slot. */
struct xz_jump_settings { int enabled, shift_page, sizes[8]; };
struct xz_jump_state { unsigned down[2], owned[2], mode_down[2]; int page[2]; };
static inline struct xz_jump_settings xz_jump_defaults(void) {
 return (struct xz_jump_settings){1,1,{0,1,2,3,4,5,6,8}};
}
static inline int xz_jump_beats(int size) {
 static const int beats[XZ_JUMP_SIZES]={1,2,4,8,16,32,64,64,128,256};
 return size>=0&&size<XZ_JUMP_SIZES?beats[size]:0;
}
static inline const char *xz_jump_label(int size) {
 static const char *labels[XZ_JUMP_SIZES]={"1 BEAT","2 BEATS","4 BEATS","8 BEATS","16 BEATS","32 BEATS","64 BEATS","16 BARS","32 BARS","64 BARS"};
 return size>=0&&size<XZ_JUMP_SIZES?labels[size]:"INVALID";
}
static inline int xz_jump_step_size(int size,int delta) {
 static const int order[]={0,1,2,3,4,5,6,8,9};
 int position=0;
 for(int i=0;i<9;i++)if(xz_jump_beats(order[i])==xz_jump_beats(size))position=i;
 int step=delta>8?8:delta < -8?-8:delta;
 position+=step;if(position<0)position=0;if(position>8)position=8;
 return order[position];
}
/* One pure resolver; each caller owns one state per logical deck. No device calls. */
struct xz_jump_deck_state { unsigned down,owned,mode_down; int page; };
struct xz_jump_input { int mode_button,pad,pad_page,shift,operation; };
enum xz_jump_decision { XZ_JUMP_FORWARD, XZ_JUMP_OWNED, XZ_JUMP_COMMAND };
struct xz_jump_result { enum xz_jump_decision decision; int signed_beats; unsigned page,pair; };
struct xz_jump_snapshot { unsigned page; int signed_beats[8]; };
/* Internal publication only. The persistent settings wire record is unchanged. */
struct xz_performance_settings { uint64_t revision; struct xz_jump_settings jump; int gate,smart; };
static inline struct xz_jump_snapshot xz_jump_snapshot(const struct xz_jump_settings *cfg,int page) {
 struct xz_jump_snapshot out={.page=page?1u:0u};
 struct xz_jump_settings defaults=xz_jump_defaults();
 const struct xz_jump_settings *use=!page&&!cfg->enabled?&defaults:cfg;
 for(int pad=0;pad<8;pad++)out.signed_beats[pad]=(pad%2?1:-1)*xz_jump_beats(use->sizes[out.page*4+pad/2]);
 return out;
}
static inline struct xz_jump_result xz_jump_resolve(struct xz_jump_deck_state *s,const struct xz_jump_settings *cfg,const struct xz_jump_input *e) {
 struct xz_jump_result out={.page=(unsigned)s->page};
 if(e->mode_button>=0&&e->mode_button<4){
  unsigned bit=1u<<e->mode_button;
  if(e->operation==2||e->operation==3)s->mode_down&=~bit;
  else if(e->operation==0&&!(s->mode_down&bit)){
   s->mode_down|=bit;s->page=e->shift&&e->mode_button==3?1:0;
  }
  out.page=(unsigned)s->page;return out;
 }
 if(e->pad<0||e->pad>=8)return out;
 out.pair=(unsigned)e->pad/2;
 unsigned bit=1u<<e->pad;
 if(s->owned&bit)out.decision=XZ_JUMP_OWNED;
 if(e->operation==2||e->operation==3){s->down&=~bit;s->owned&=~bit;return out;}
 if(e->operation!=0||s->down&bit)return out;
 s->down|=bit;
 if((!s->page&&!cfg->enabled)||e->pad_page!=3||e->shift)return out;
 int amount=xz_jump_beats(cfg->sizes[s->page*4+e->pad/2]);
 if(!amount)return out;
 s->owned|=bit;out.decision=XZ_JUMP_COMMAND;out.signed_beats=e->pad%2?amount:-amount;
 return out;
}
/* Native adapter: FORWARD goes to firmware, never a second jump on refusal. */
int xz_jump_event(struct xz_jump_state *,const struct xz_jump_settings *,const struct xz_cue_event *,int *beats);
#endif
