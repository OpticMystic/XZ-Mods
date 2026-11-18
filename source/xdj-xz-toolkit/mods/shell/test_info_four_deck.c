#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "four_deck_fixtures.h"
#include "shell_info.h"
#include "shell_info_chrome.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct fx_track track;
static uint16_t pixels[800*480];
static size_t touch(struct xz_shell *u,const struct xz_shell_model *m,struct xz_shell_rect r){
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 struct xz_shell_event e={XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0,0,-1};
 return xz_shell_input(u,m,&e,out);
}
int main(void){
 fx_track_build(&track,7);
 struct xz_shell_model m;fx_model_four(&m,&track);
 struct xz_shell_info_model im={0};m.info=&im;
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_INFO,-1};
 assert(!xz_shell_input(&u,&m,&e,out));
 assert(!touch(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,3)));
 assert(u.stack[u.depth-1].focus==4);
 assert(xz_shell_render(&u,&m,pixels,800));
 int focus=u.deck;uint8_t sides=m.sides_extra;
 for(int d=2;d<4;d++){
  m.deck[d].track_bpm_x100=12345;m.deck[d].bpm_x100=14000;
  m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TRACK_BPM);
  xz_shell_info_player(&im.deck[d],&m.deck[d]);
  assert(im.deck[d].loaded&&im.deck[d].title==m.deck[d].title);
  assert(im.deck[d].bpm_x100==12345&&im.deck[d].duration_ms==m.deck[d].total_ms);
  assert(!im.deck[d].links&&!im.deck[d].rating_writable);
  assert(!(im.deck[d].known&XZ_SHELL_KNOWN(XZ_SHELL_I_TRACK_ID)));
  assert(!touch(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,d)));
  assert(u.stack[u.depth-1].focus==d+1&&u.deck==focus&&m.sides_extra==sides);
  assert(xz_shell_render(&u,&m,pixels,800));
 }
 im.has_highlight=1;im.linked_count=XZ_SHELL_INFO_LINKED;
 for(int i=0;i<XZ_SHELL_INFO_DECKS+1+XZ_SHELL_INFO_LINKED;i++){
  struct xz_shell_rect r=xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,i);
  assert(r.w>=44&&r.h>=44&&r.x+r.w<=si_close_rect().x);
  assert(!touch(&u,&m,r)&&u.stack[u.depth-1].focus==i+1);
  assert(xz_shell_render(&u,&m,pixels,800));
 }
 /* Unknown and unloaded players clear their former title; no stale metadata or writes. */
 struct xz_shell_deck unknown={0};
 xz_shell_info_player(&im.deck[2],&unknown);assert(!im.deck[2].known&&!im.deck[2].title);
 unknown.known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED);
 xz_shell_info_player(&im.deck[2],&unknown);
 assert(im.deck[2].known==XZ_SHELL_KNOWN(XZ_SHELL_I_LOADED)&&!im.deck[2].loaded);
 /* A route drop removes extra tabs even while the previous INFO stack remains open. */
 im.has_highlight=0;im.linked_count=0;m.deck_count=2;
 u.stack[u.depth-1].focus=4;u.deck=3;
 assert(xz_shell_render(&u,&m,pixels,800));
 assert(!touch(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,3))&&u.stack[u.depth-1].focus==4);
 puts("PASS INFO extra player metadata, seven distinct tabs, independent focus and route loss");
}
