#include "beat_jump.h"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error Beat jump acceptance requires active assertions
#endif
int main(void){
 struct xz_jump_state state={0};struct xz_jump_settings cfg=xz_jump_defaults();int beats;
 assert(xz_jump_beats(cfg.sizes[4])==16&&xz_jump_beats(cfg.sizes[5])==32&&xz_jump_beats(cfg.sizes[6])==64&&xz_jump_beats(cfg.sizes[7])==128);
 assert(cfg.enabled&&cfg.shift_page);cfg.enabled=0;
 struct xz_cue_event e={.deck=0,.pad=0,.pad_page=3,.mode_button=-1};
 assert(!xz_jump_event(&state,&cfg,&e,&beats)&&!beats);e.operation=2;assert(!xz_jump_event(&state,&cfg,&e,&beats));
 cfg.enabled=1;
 for(int deck=0;deck<2;deck++)for(int page=0;page<2;page++){
  e=(struct xz_cue_event){.deck=deck,.pad=-1,.pad_page=0,.mode_button=3,.shift=page};
  assert(!xz_jump_event(&state,&cfg,&e,&beats)&&state.page[deck]==page);
  e.operation=2;assert(!xz_jump_event(&state,&cfg,&e,&beats));
  for(int size=0;size<XZ_JUMP_SIZES;size++)for(int pair=0;pair<4;pair++)for(int direction=0;direction<2;direction++){
   cfg.sizes[page*4+pair]=size;
   e=(struct xz_cue_event){.deck=deck,.pad=pair*2+direction,.pad_page=3,.mode_button=-1};
   assert(xz_jump_event(&state,&cfg,&e,&beats)&&beats==(direction?1:-1)*xz_jump_beats(size));
   assert(xz_jump_event(&state,&cfg,&e,&beats)&&!beats);
   e.operation=1;assert(xz_jump_event(&state,&cfg,&e,&beats)&&!beats);
   e.operation=2+direction;assert(xz_jump_event(&state,&cfg,&e,&beats)&&!beats);
   assert(!xz_jump_event(&state,&cfg,&e,&beats));
  }
 }
 assert(xz_jump_beats(4)==16&&xz_jump_beats(5)==32&&xz_jump_beats(6)==64);
 assert(xz_jump_beats(7)==64&&xz_jump_beats(8)==128&&xz_jump_beats(9)==256);
 assert(xz_jump_step_size(6,1)==8&&xz_jump_step_size(8,-1)==6);
 assert(xz_jump_step_size(7,1)==8&&xz_jump_step_size(7,-1)==5);
 assert(xz_jump_step_size(9,1)==9&&xz_jump_step_size(0,-1)==0);
 assert(!xz_jump_beats(-1)&&!xz_jump_beats(10));
 e=(struct xz_cue_event){.deck=0,.pad=0,.pad_page=3,.mode_button=-1};
 assert(xz_jump_event(&state,&cfg,&e,&beats));cfg.enabled=0;e.pad_page=0;e.shift=1;e.operation=3;
 assert(xz_jump_event(&state,&cfg,&e,&beats)&&!beats);
 cfg.enabled=1;e.operation=0;e.pad_page=3;assert(!xz_jump_event(&state,&cfg,&e,&beats)&&!beats);
 e.shift=0;assert(!xz_jump_event(&state,&cfg,&e,&beats));e.operation=2;xz_jump_event(&state,&cfg,&e,&beats);
 cfg.shift_page=0;cfg.sizes[0]=1;e.operation=0;assert(xz_jump_event(&state,&cfg,&e,&beats)&&beats==-2);
 e.operation=2;xz_jump_event(&state,&cfg,&e,&beats);
 e.deck=2;e.operation=0;assert(!xz_jump_event(&state,&cfg,&e,&beats));
 puts("PASS both pages, decks, all pair sizes/directions, duplicate presses, repeat suppression, shift and release ownership");
}
