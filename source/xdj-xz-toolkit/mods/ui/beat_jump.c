#include "beat_jump.h"
int xz_jump_event(struct xz_jump_state *s,const struct xz_jump_settings *cfg,const struct xz_cue_event *e,int *beats) {
 *beats=0;
 if(e->deck<0||e->deck>1)return 0;
 int d=e->deck;
 struct xz_jump_deck_state state={s->down[d],s->owned[d],s->mode_down[d],s->page[d]};
 struct xz_jump_input input={e->mode_button,e->pad,e->pad_page,e->shift,e->operation};
 struct xz_jump_result result=xz_jump_resolve(&state,cfg,&input);
 s->down[d]=state.down;s->owned[d]=state.owned;s->mode_down[d]=state.mode_down;s->page[d]=state.page;
 *beats=result.signed_beats;return result.decision!=XZ_JUMP_FORWARD;
}
