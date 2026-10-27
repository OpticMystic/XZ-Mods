#ifdef NDEBUG
#error Browse LOAD acceptance requires assertions
#endif
#include "browse_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>

static struct bx_library library;
static struct fx_track track;
static struct xz_shell_action actions[XZ_SHELL_ACTIONS];
static size_t event(struct xz_shell *u,const struct xz_shell_model *m,enum xz_shell_event_kind kind,int delta,int key,int deck) {
 struct xz_shell_event e={kind,0,0,delta,key,deck};return xz_shell_input(u,m,&e,actions);
}
static size_t load_key(struct xz_shell *u,const struct xz_shell_model *m,int side) { return event(u,m,XZ_SHELL_KEY,0,XZ_SHELL_KEY_LOAD,side); }
static void setup(struct xz_shell *u,struct xz_shell_model *m,struct xz_shell_browse_model *b,int decks) {
 fx_model_full(m,&track);bx_tracks(b,&library,XZ_SHELL_BROWSE);b->media_generation=7;
 m->deck_count=decks;m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;m->browse=b;
 for(int d=0;d<4;d++)m->deck[d].caps_off=0;
 xz_shell_init(u);xz_shell_browse_reset();
 event(u,m,XZ_SHELL_KEY,0,XZ_SHELL_KEY_BROWSE,-1);u->stack[u->depth-1].focus=1;
}
static void expect_load(size_t count,int deck,uint32_t id) {
 assert(count==1&&actions[0].kind==XZ_SHELL_ACT_LOAD&&actions[0].deck==deck&&actions[0].id==(int)id);
}
static void pairs(void) {
 for(int deck=0;deck<4;deck++){
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;setup(&u,&m,&b,4);
  uint32_t id=b.rows[0].id;
  expect_load(load_key(&u,&m,deck),deck,id);
  assert(xz_shell_screen(&u)==XZ_SHELL_PLAY&&!sh_browse_load_pending(&u));
 }
}
static void encoder_and_frozen_target(void) {
 for(int deck=0;deck<4;deck++){
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;setup(&u,&m,&b,4);
  uint32_t id=b.rows[0].id;assert(event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1)==0);
  assert(sh_browse_load_pending(&u));
  u.stack[u.depth-1].cursor=1; /* Selection refresh cannot retarget the pending track. */
  event(&u,&m,XZ_SHELL_ENCODER_TURN,deck,0,-1);
  expect_load(event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1),deck,id);
 }
}
static void cancellation(void) {
 for(int reason=0;reason<8;reason++){
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;setup(&u,&m,&b,4);
  assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));
  switch(reason){
   case 0:assert(!event(&u,&m,XZ_SHELL_KEY,0,XZ_SHELL_KEY_BACK,-1));assert(!sh_browse_load_pending(&u));continue;
   case 1:m.browse=NULL;break;
   case 2:m.capabilities&=~XZ_SHELL_CAP_LOAD;break;
   case 3:m.deck_count=2;break;
   case 4:b.media_generation++;break;
   case 5:b.list_id++;break;
   case 6:m.deck[1].caps_off|=XZ_SHELL_CAP_LOAD;break;
   case 7:b.source_name="USB2";break;
  }
  assert(!load_key(&u,&m,1));assert(!sh_browse_load_pending(&u));
 }
}
static void cancel_and_cue_options(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;setup(&u,&m,&b,4);
 assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));
 event(&u,&m,XZ_SHELL_ENCODER_TURN,4,0,-1);assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));
 assert(!sh_browse_load_pending(&u)&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 setup(&u,&m,&b,4);
 static const struct xz_shell_cue_entry cues[1]={{0}};
 b.known|=1u<<XZ_SHELL_BF_CUES;b.cues=cues;b.cue_count=1;b.cue_track=b.rows[0].id;
 m.capabilities|=XZ_SHELL_CAP_CUE_LIST;
 assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));assert(u.load_choice.has_cues);
 u.deck=2; /* Cue destination must stay the native deck captured when opened. */
 event(&u,&m,XZ_SHELL_ENCODER_TURN,4,0,-1);assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));
 assert(!sh_browse_load_pending(&u)&&u.panel.kind==XZ_SHELL_PANEL_MENU);
}
static void copy_safe(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;setup(&u,&m,&b,4);
 struct xz_shell probe=u;expect_load(load_key(&probe,&m,2),2,b.rows[0].id);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&!sh_browse_load_pending(&u));
 assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));probe=u;
 expect_load(load_key(&probe,&m,1),1,b.rows[0].id);assert(sh_browse_load_pending(&u));
}
static void two_decks_and_touch(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;setup(&u,&m,&b,2);
 expect_load(load_key(&u,&m,1),1,b.rows[0].id);
 /* Two decks: the rotary selector opens stock's track menu (DECK 1, DECK 2, CANCEL) on the deck in focus. */
 setup(&u,&m,&b,2);assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1)&&sh_browse_load_pending(&u));
 assert(u.load_choice.deck_count==2&&u.load_choice.cursor==u.deck);
 expect_load(event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1),u.deck,b.rows[0].id);
 setup(&u,&m,&b,2);assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));
 u.load_choice.cursor=0;event(&u,&m,XZ_SHELL_ENCODER_TURN,1,0,-1);expect_load(event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1),1,b.rows[0].id);
 setup(&u,&m,&b,2);assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));
 event(&u,&m,XZ_SHELL_ENCODER_TURN,9,0,-1);assert(u.load_choice.cursor==2);
 assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1)&&!sh_browse_load_pending(&u)&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 setup(&u,&m,&b,2);assert(!event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,-1));expect_load(load_key(&u,&m,0),0,b.rows[0].id);
 setup(&u,&m,&b,4);
 struct xz_shell_rect row=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,0);
 struct xz_shell_event down={XZ_SHELL_TOUCH_DOWN,row.x+row.w/2,row.y+row.h/2,0,0,-1};
 struct xz_shell_event up=down;up.kind=XZ_SHELL_TOUCH_UP;xz_shell_input(&u,&m,&down,actions);xz_shell_input(&u,&m,&up,actions);
 struct xz_shell_rect chip=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,4);assert(chip.w>0);
 down.x=chip.x+chip.w/2;down.y=chip.y+chip.h/2;
 expect_load(xz_shell_input(&u,&m,&down,actions),3,b.rows[0].id);assert(!sh_browse_load_pending(&u));
}
int main(void) {
 (void)&bx_folders;fx_track_build(&track,1);bx_build(&library);
 pairs();encoder_and_frozen_target();cancellation();cancel_and_cue_options();copy_safe();two_decks_and_touch();
 puts("PASS browse LOAD choices: one-press physical LOAD, all-four encoder, frozen target, cancellation/generation/routes, copy probes, two-deck track menu and explicit touch");
 return 0;
}
