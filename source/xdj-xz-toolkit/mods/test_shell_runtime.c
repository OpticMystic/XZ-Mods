/* SPDX-License-Identifier: MIT */
/* Host test for the FLAGSHIP shell runtime's pure parts (shell_runtime_core.c)
   and the library lists (shell_library.c) on the generated pdb fixture.
   Usage: test_shell_runtime FIXTURE.pdb */
#ifdef NDEBUG
#error Shell runtime acceptance requires active assertions
#endif
#include "shell_runtime.h"
#include "shell_library.h"
#include "shell/fixtures.h"
#include "shell/shell_browse.h"
#include "shell/shell_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct fx_track track;
static struct xz_shell_browse_model browse_slot;
static struct xz_shell_settings_model settings_slot;

static void key_table(void) {
 static const struct { uint16_t code; unsigned channel; int rule, key, hold; } expect[]={
  {0x209,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_BROWSE_HOLD},{0x20a,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TAG_LIST,-1},
  {0x206,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_INFO,-1},{0x20b,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_MENU,XZ_SHELL_KEY_MENU_HOLD},
  {0x20e,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SHORTCUT,-1},{0x203,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE_USB1,-1},
  {0x204,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE_USB2,-1},{0x420c,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TAG_TRACK,-1},
  {0x4208,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_BACK,XZ_SHELL_KEY_BACK_HOLD},
  {0x430f,1,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_LOAD,-1},{0x430f,2,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_LOAD,-1},{0x430b,1,XZ_SHRT_STOCK,-1,-1},{0x430c,2,XZ_SHRT_STOCK,-1,-1},
 };
 for(size_t i=0;i<sizeof expect/sizeof *expect;i++){
  const struct xz_shrt_key_map *k=xz_shrt_key_lookup(expect[i].code,expect[i].channel);
  assert(k&&k->rule==expect[i].rule&&k->key==expect[i].key&&k->hold==expect[i].hold);
 }
 /* Deck keys and the jog are not in the table; LOAD on a third channel is not either. */
 assert(!xz_shrt_key_lookup(0x4305,1)&&!xz_shrt_key_lookup(0x4101,1));
 /* USB1/USB2 (0x204 is UiKey_Usb2 in the binary) and TRACK FILTER: shell keys on press. */
 for(unsigned i=0;i<3;i++){
  static const uint16_t codes[3]={0x203,0x204,0x420d};
  static const int expect[3]={XZ_SHELL_KEY_SOURCE_USB1,XZ_SHELL_KEY_SOURCE_USB2,XZ_SHELL_KEY_TRACK_FILTER};
  const struct xz_shrt_key_map *k=xz_shrt_key_lookup(codes[i],0);
  assert(k&&k->rule==XZ_SHRT_ON_PRESS&&xz_shrt_key_for_phase(k,XZ_SHRT_PRESS,0)==expect[i]);
 }
 /* LINK and rekordbox hand the screen to stock's media and connection screen (CONTROL MODE). */
 for(unsigned i=0;i<2;i++){
  const struct xz_shrt_key_map *k=xz_shrt_key_lookup(i?0x202:0x205,0);
  assert(k&&k->rule==XZ_SHRT_HAND_OFF&&xz_shrt_key_for_phase(k,XZ_SHRT_PRESS,0)==-1);
 }
 assert(!xz_shrt_key_lookup(0x430f,3));
 assert(xz_shrt_phase_of(0)==XZ_SHRT_PRESS&&xz_shrt_phase_of(1)==XZ_SHRT_HOLD&&xz_shrt_phase_of(2)==XZ_SHRT_RELEASE&&xz_shrt_phase_of(3)==XZ_SHRT_RELEASE);
 const struct xz_shrt_key_map *menu=xz_shrt_key_lookup(0x20b,0),*info=xz_shrt_key_lookup(0x206,0);
 assert(xz_shrt_key_for_phase(menu,XZ_SHRT_PRESS,0)==-1);
 assert(xz_shrt_key_for_phase(menu,XZ_SHRT_HOLD,0)==XZ_SHELL_KEY_MENU_HOLD&&xz_shrt_key_for_phase(menu,XZ_SHRT_HOLD,1)==-1);
 assert(xz_shrt_key_for_phase(menu,XZ_SHRT_RELEASE,0)==XZ_SHELL_KEY_MENU&&xz_shrt_key_for_phase(menu,XZ_SHRT_RELEASE,1)==-1);
 assert(xz_shrt_key_for_phase(info,XZ_SHRT_PRESS,0)==XZ_SHELL_KEY_INFO&&xz_shrt_key_for_phase(info,XZ_SHRT_RELEASE,0)==-1);
 const struct xz_shrt_key_map *browse=xz_shrt_key_lookup(0x209,0);
 assert(xz_shrt_key_for_phase(browse,XZ_SHRT_PRESS,0)==-1);
 assert(xz_shrt_key_for_phase(browse,XZ_SHRT_RELEASE,0)==XZ_SHELL_KEY_BROWSE);
 assert(xz_shrt_key_for_phase(browse,XZ_SHRT_HOLD,0)==XZ_SHELL_KEY_BROWSE_HOLD);
 assert(xz_shrt_key_for_phase(browse,XZ_SHRT_HOLD,1)==-1&&xz_shrt_key_for_phase(browse,XZ_SHRT_RELEASE,1)==-1);
 assert(xz_shrt_key_for_phase(xz_shrt_key_lookup(0x430f,1),XZ_SHRT_PRESS,0)==XZ_SHELL_KEY_LOAD);
 assert(xz_shrt_key_lookup(0x430f,1)->deck==0&&xz_shrt_key_lookup(0x430f,2)->deck==1);
 puts("PASS R1 key table: codes, channels, stock keys, press/hold/release phases");
}

static void verdicts(void) {
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);fx_model_full(&m,&track);
 m.browse=NULL;m.settings_screen=NULL;m.info=NULL;m.source=NULL;m.capabilities=0;
 const struct xz_shrt_key_map *browse_key=xz_shrt_key_lookup(0x209,0),*menu=xz_shrt_key_lookup(0x20b,0),*back=xz_shrt_key_lookup(0x4208,0);
 const struct xz_shrt_key_map *tag=xz_shrt_key_lookup(0x420c,0),*info=xz_shrt_key_lookup(0x206,0),*load=xz_shrt_key_lookup(0x430f,1);
 struct xz_shell before=u;
 /* Nothing the shell can open: the keys go to stock, which opens its own screens. */
 assert(xz_shrt_key_verdict(&u,&m,browse_key)==XZ_SHRT_PASS_HAND_OFF);
 assert(xz_shrt_key_verdict(&u,&m,info)==XZ_SHRT_PASS_HAND_OFF);
 /* MENU stays with the shell even without its settings slot (the runtime always publishes UTILITY). */
 assert(xz_shrt_key_verdict(&u,&m,menu)==XZ_SHRT_SWALLOW);
 /* Inert keys are swallowed, never passed to the hidden stock screen: stock would act on its own
    browse cursor or deck state behind the shell. */
 assert(xz_shrt_key_verdict(&u,&m,tag)==XZ_SHRT_PASS_HAND_OFF); /* TAG TRACK on PLAY: stock tags the loaded track */
 /* TRACK FILTER opens the shell's FILTER panel: never stock's. */
 assert(xz_shrt_key_verdict(&u,&m,xz_shrt_key_lookup(0x420d,0))!=XZ_SHRT_PASS);
 assert(xz_shrt_key_verdict(&u,&m,back)==XZ_SHRT_SWALLOW); /* BACK on the bare PLAY screen */
 assert(xz_shrt_key_verdict(&u,&m,load)==XZ_SHRT_SWALLOW); /* LOAD on PLAY would load stock's cursor */
 assert(xz_shrt_key_verdict(&u,&m,xz_shrt_key_lookup(0x430b,0))==XZ_SHRT_PASS); /* deck keys stay stock's */
 assert(!memcmp(&before,&u,sizeof u));                     /* probes never touch the shell */
 /* With BROWSE and the settings slot the shell keeps them; held MENU opens the shell's own UTILITY. */
 m.browse=&browse_slot;m.settings_screen=&settings_slot;m.capabilities=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH;
 assert(xz_shrt_key_verdict(&u,&m,browse_key)==XZ_SHRT_SWALLOW);
 assert(xz_shrt_key_verdict(&u,&m,menu)==XZ_SHRT_SWALLOW);
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_input(&u,&m,&e,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 assert(xz_shrt_key_verdict(&u,&m,back)==XZ_SHRT_SWALLOW); /* BACK below PLAY belongs to the shell */
 assert(xz_shrt_key_verdict(&u,&m,tag)==XZ_SHRT_SWALLOW);  /* TAG TRACK on a list: never stock's hidden row */
 /* On a list the short MENU opens the shell's MENU panel and the hold its UTILITY: the shell keeps the key. */
 assert(xz_shrt_key_verdict(&u,&m,menu)==XZ_SHRT_SWALLOW);
 {struct xz_shell copy=u;e.key=XZ_SHELL_KEY_MENU;size_t n=xz_shell_input(&copy,&m,&e,out);
  assert(n==0&&copy.panel.kind==XZ_SHELL_PANEL_MENU);} /* the shell's MENU panel (sort, columns) */
 e.key=XZ_SHELL_KEY_MENU_HOLD;xz_shell_input(&u,&m,&e,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_UTILITY&&xz_shrt_key_verdict(&u,&m,menu)==XZ_SHRT_SWALLOW);
 /* MENU is never passed to stock, whatever the shell's probe says. */
 {struct xz_shell copy;xz_shell_init(&copy);struct xz_shell_model empty;memset(&empty,0,sizeof empty);
  assert(xz_shrt_key_verdict(&copy,&empty,menu)==XZ_SHRT_SWALLOW);}
 puts("PASS press-time verdicts: hand-off pass-through, inert keys swallowed, MENU to the shell's UTILITY, side-effect-free probes");
}

static void load_verdicts(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 struct xz_shell_browse_row row={0};row.kind=XZ_SHELL_ROW_TRACK;row.id=101;
 struct xz_shell_browse_model b={0};
 b.known=(1u<<XZ_SHELL_BF_TOTAL);b.total=1;b.window_count=1;b.rows=&row;
 const enum xz_shell_screen screens[]={XZ_SHELL_BROWSE,XZ_SHELL_PLAYLIST,XZ_SHELL_TAG_LIST,XZ_SHELL_SEARCH};
 for(size_t i=0;i<sizeof screens/sizeof *screens;i++)for(unsigned channel=1;channel<=2;channel++){
  xz_shell_init(&u);xz_shell_browse_reset();fx_model_full(&m,&track);
  u.depth=2;u.stack[1]=(struct xz_shell_view){screens[i],0,0,0};b.screen=screens[i];m.browse=&b;
  const struct xz_shrt_key_map *k=xz_shrt_key_lookup(0x430f,channel);
  struct xz_shell before=u;int touched=xz_shell_browse_touched();
  /* Without the capability LOAD is inert in the shell (stock would load its own hidden cursor). */
  assert(xz_shrt_key_verdict(&u,&m,k)==XZ_SHRT_SWALLOW);
  m.capabilities|=XZ_SHELL_CAP_LOAD;
  assert(xz_shrt_key_verdict(&u,&m,k)==XZ_SHRT_SWALLOW);
  assert(!memcmp(&before,&u,sizeof u)&&xz_shell_browse_touched()==touched);
  struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,(enum xz_shell_key)xz_shrt_key_for_phase(k,XZ_SHRT_PRESS,0),k->deck};
  assert(xz_shell_input(&u,&m,&e,out)==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].id==101&&out[0].deck==(int)channel-1&&u.depth==1);
  /* Once swallowed at press, HOLD/RELEASE stay swallowed after LOAD returns to PLAY. */
  struct xz_shrt_contacts c={0};int held;
  xz_shrt_contact_press(&c,0x430f,channel,1);
  assert(xz_shrt_key_verdict(&u,&m,k)==XZ_SHRT_SWALLOW);
  assert(xz_shrt_contact_follow(&c,0x430f,channel,XZ_SHRT_HOLD,&held)==1&&xz_shrt_key_for_phase(k,XZ_SHRT_HOLD,0)==-1);
  assert(xz_shrt_contact_follow(&c,0x430f,channel,XZ_SHRT_RELEASE,&held)==1&&xz_shrt_key_for_phase(k,XZ_SHRT_RELEASE,1)==-1);
  /* A folder row or an empty window: swallowed, and the shell's own LOAD input does nothing loadable. */
  u=before;row.kind=XZ_SHELL_ROW_FOLDER;
  assert(xz_shrt_key_verdict(&u,&m,k)==XZ_SHRT_SWALLOW);
  row.kind=XZ_SHELL_ROW_TRACK;b.window_count=0;
  assert(xz_shrt_key_verdict(&u,&m,k)==XZ_SHRT_SWALLOW);
  e.kind=XZ_SHELL_KEY;size_t n=xz_shell_input(&u,&m,&e,out);
  for(size_t j=0;j<n;j++)assert(out[j].kind!=XZ_SHELL_ACT_LOAD);
  b.window_count=1;
 }
 const enum xz_shell_screen nonlists[]={XZ_SHELL_PLAY,XZ_SHELL_INFO,XZ_SHELL_SHORTCUT,XZ_SHELL_UTILITY,XZ_SHELL_SOURCE};
 for(size_t i=0;i<sizeof nonlists/sizeof *nonlists;i++){
  u.stack[u.depth-1].screen=nonlists[i];
  assert(xz_shrt_key_verdict(&u,&m,xz_shrt_key_lookup(0x430f,1))==XZ_SHRT_SWALLOW);
  assert(xz_shrt_key_verdict(&u,&m,xz_shrt_key_lookup(0x430f,2))==XZ_SHRT_SWALLOW);
  /* ...and inert there: LOAD belongs only to list input. */
  struct xz_shell copy=u;struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_LOAD,0};
  assert(xz_shell_input(&copy,&m,&e,out)==0);
 }
 puts("PASS LOAD mapping and verdicts: both decks, list-only loads, hand-off, pure probes, contact pass-through");
}
static void contacts(void) {
 struct xz_shrt_contacts c;memset(&c,0,sizeof c);int held;
 assert(xz_shrt_contact_follow(&c,0x4208,0,XZ_SHRT_RELEASE,&held)==-1); /* release without its press */
 xz_shrt_contact_press(&c,0x4208,0,1);
 assert(xz_shrt_contact_follow(&c,0x4208,0,XZ_SHRT_HOLD,&held)==1&&!held);
 assert(xz_shrt_contact_follow(&c,0x4208,0,XZ_SHRT_HOLD,&held)==1&&held);
 assert(xz_shrt_contact_follow(&c,0x4208,0,XZ_SHRT_RELEASE,&held)==1&&held);
 assert(xz_shrt_contact_follow(&c,0x4208,0,XZ_SHRT_RELEASE,&held)==-1);
 xz_shrt_contact_press(&c,0x20b,0,0);xz_shrt_contact_press(&c,0x209,0,1);
 assert(xz_shrt_contact_follow(&c,0x20b,0,XZ_SHRT_RELEASE,&held)==0);
 assert(xz_shrt_contact_follow(&c,0x209,0,XZ_SHRT_RELEASE,&held)==1);
 /* Channels are separate contacts. */
 xz_shrt_contact_press(&c,0x430f,1,1);
 assert(xz_shrt_contact_follow(&c,0x430f,2,XZ_SHRT_RELEASE,&held)==-1&&xz_shrt_contact_follow(&c,0x430f,1,XZ_SHRT_RELEASE,&held)==1);
 for(int i=0;i<20;i++)xz_shrt_contact_press(&c,(uint16_t)(0x100+i),0,1);   /* overflow reuses a slot, never writes past */
 puts("PASS key contacts: all three ops follow the press, unknown releases pass, channels and overflow");
}

static void touches(void) {
 struct xz_shrt_touch t;memset(&t,0,sizeof t);struct xz_shrt_touch_step s;
 s=xz_shrt_touch_route(&t,1,100,200,1,0);assert(s.event==XZ_SHELL_TOUCH_DOWN&&!s.to_stock&&s.x==100&&s.y==200);
 s=xz_shrt_touch_route(&t,1,110,210,1,0);assert(s.event==XZ_SHELL_TOUCH_MOVE&&s.x==110);
 /* The native release clears the coordinates; the shell gets the last point. */
 s=xz_shrt_touch_route(&t,1,0,0,0,0);assert(s.event==XZ_SHELL_TOUCH_UP&&s.x==110&&s.y==210);
 /* A shell contact stays the shell's after a hand-off, through its release. */
 s=xz_shrt_touch_route(&t,1,5,5,1,0);assert(s.event==XZ_SHELL_TOUCH_DOWN);
 s=xz_shrt_touch_route(&t,0,6,6,1,0);assert(s.event==XZ_SHELL_TOUCH_MOVE&&!s.to_stock);
 s=xz_shrt_touch_route(&t,0,6,6,0,0);assert(s.event==XZ_SHELL_TOUCH_UP);
 /* Not owning: stock gets the contact; the shell taking over mid-gesture releases it for stock once. */
 s=xz_shrt_touch_route(&t,0,50,50,1,0);assert(s.to_stock&&s.event<0);
 s=xz_shrt_touch_route(&t,1,51,51,1,1);assert(!s.to_stock&&s.release_stock&&s.event<0);
 s=xz_shrt_touch_route(&t,1,52,52,1,0);assert(!s.to_stock&&!s.release_stock&&s.event<0);
 s=xz_shrt_touch_route(&t,1,0,0,0,0);assert(!s.to_stock&&s.event<0);
 /* Shell owns but stock still has a contact down (activation mid-touch): release it, swallow to the lift. */
 s=xz_shrt_touch_route(&t,1,70,70,1,1);assert(s.release_stock&&s.event<0);
 s=xz_shrt_touch_route(&t,1,70,70,0,0);assert(s.event<0&&!s.to_stock);
 /* A stock contact ending while the shell owns ends at stock. */
 s=xz_shrt_touch_route(&t,0,80,80,1,0);assert(s.to_stock);
 s=xz_shrt_touch_route(&t,1,0,0,0,1);assert(s.to_stock&&!s.release_stock);
 s=xz_shrt_touch_route(&t,0,0,0,0,0);assert(s.to_stock);
 puts("PASS touch ownership: shell contacts, stock contacts, synthesized stock release, opening contact swallowed");
}

static void pushes(void) {
 struct xz_shrt_push p={0,0};
 assert(xz_shrt_push_event(&p,0,2)==-1);                       /* a stale pass before any press */
 assert(xz_shrt_push_event(&p,1,2)==XZ_SHELL_ENCODER_PUSH);
 assert(xz_shrt_push_event(&p,0,3)==XZ_SHELL_ENCODER_HOLD);
 assert(xz_shrt_push_event(&p,0,3)==-1&&xz_shrt_push_event(&p,0,2)==-1);
 assert(xz_shrt_push_event(&p,1,2)==XZ_SHELL_ENCODER_PUSH&&xz_shrt_push_event(&p,0,2)==-1);
 puts("PASS encoder push phases: press, one hold, repeated handler passes ignored");
}

static void hand_off(void) {
 struct xz_shrt_handoff h;memset(&h,0,sizeof h);
 assert(!xz_shrt_handoff_yields(&h));
 /* Stock never leaves its play screen (nothing to show): the grace period brings the shell back. */
 xz_shrt_handoff_begin(&h,1000);assert(xz_shrt_handoff_yields(&h)&&h.count==1);
 assert(!xz_shrt_handoff_step(&h,1100,1));
 assert(!xz_shrt_handoff_step(&h,1600,1));
 assert(!xz_shrt_handoff_step(&h,3400,1));
 assert(xz_shrt_handoff_step(&h,3500,1)&&!h.handed&&h.resumes==1);
 /* Stock opens its own screen, then returns: resume once it has been back for the stable time. */
 xz_shrt_handoff_begin(&h,10000);
 assert(!xz_shrt_handoff_step(&h,10050,0));
 assert(!xz_shrt_handoff_step(&h,11000,1));
 assert(!xz_shrt_handoff_step(&h,11300,1));
 assert(!xz_shrt_handoff_step(&h,11350,0));   /* a flicker restarts the stable time */
 assert(!xz_shrt_handoff_step(&h,11400,1));
 assert(xz_shrt_handoff_step(&h,11800,1));
 /* Unreadable play state never resumes; BACK held does. */
 xz_shrt_handoff_begin(&h,20000);
 assert(!xz_shrt_handoff_step(&h,30000,-1)&&!xz_shrt_handoff_step(&h,40000,-1)&&h.handed);
 xz_shrt_handoff_end(&h);assert(!xz_shrt_handoff_yields(&h)&&h.resumes==3);
 /* Cautions yield on their own and clear on their own; the unknown sentinel yields too. */
 xz_shrt_handoff_caution(&h,0x144);assert(xz_shrt_handoff_yields(&h));
 xz_shrt_handoff_caution(&h,0);assert(!xz_shrt_handoff_yields(&h));
 xz_shrt_handoff_caution(&h,UINT32_MAX);assert(xz_shrt_handoff_yields(&h));
 xz_shrt_handoff_caution(&h,0);
 /* Begin twice counts one hand-off. */
 xz_shrt_handoff_begin(&h,50000);xz_shrt_handoff_begin(&h,50010);assert(h.count==4);
 /* A short MENU passed to stock: stock stays on PLAY, the shell is back 300 ms after the release
    (plus the stable time), not after the full grace. Shortening never extends. */
 xz_shrt_handoff_begin(&h,60000);assert(!xz_shrt_handoff_step(&h,60100,1));
 xz_shrt_handoff_shorten(&h,60200,300);
 assert(!xz_shrt_handoff_step(&h,60400,1)&&xz_shrt_handoff_step(&h,60600,1));
 xz_shrt_handoff_begin(&h,70000);xz_shrt_handoff_shorten(&h,70000,2400);xz_shrt_handoff_shorten(&h,70010,5000);
 assert(h.since==70000-100);
 /* Stock left PLAY (UTILITY opened on a hold): shortening does nothing, the shell waits for its return. */
 xz_shrt_handoff_begin(&h,80000);assert(!xz_shrt_handoff_step(&h,80100,0));xz_shrt_handoff_shorten(&h,80200,0);
 assert(h.since==80000&&!xz_shrt_handoff_step(&h,80300,1)&&xz_shrt_handoff_step(&h,80700,1));
 /* The unknown caution sentinel passes only after three refreshes in a row; real ids pass at once. */
 struct xz_shrt_caution c;memset(&c,0,sizeof c);
 assert(xz_shrt_caution_filter(&c,UINT32_MAX)==0&&xz_shrt_caution_filter(&c,UINT32_MAX)==0&&xz_shrt_caution_filter(&c,0)==0);
 assert(xz_shrt_caution_filter(&c,UINT32_MAX)==0&&xz_shrt_caution_filter(&c,UINT32_MAX)==0&&xz_shrt_caution_filter(&c,UINT32_MAX)==UINT32_MAX);
 assert(xz_shrt_caution_filter(&c,UINT32_MAX)==UINT32_MAX&&xz_shrt_caution_filter(&c,0x144)==0x144&&xz_shrt_caution_filter(&c,UINT32_MAX)==0x144);
 /* BACK hold acknowledges a latched caution until it changes; a new caution yields again. */
 memset(&c,0,sizeof c);assert(xz_shrt_caution_filter(&c,0x13f)==0x13f);xz_shrt_caution_ack(&c);
 assert(xz_shrt_caution_filter(&c,0x13f)==0&&xz_shrt_caution_filter(&c,0x13f)==0);
 assert(xz_shrt_caution_filter(&c,0x145)==0x145&&xz_shrt_caution_filter(&c,0x13f)==0x13f);
 xz_shrt_caution_ack(&c);assert(xz_shrt_caution_filter(&c,0)==0&&xz_shrt_caution_filter(&c,0x13f)==0x13f);
 for(int i=0;i<3;i++)xz_shrt_caution_filter(&c,UINT32_MAX);
 xz_shrt_caution_ack(&c);assert(xz_shrt_caution_filter(&c,UINT32_MAX)==0);
 puts("PASS hand-off: grace resume, leave-and-return resume, flicker, unknown state, BACK hold, cautions, short MENU, caution debounce");
}

/* The playhead the shell publishes must advance smoothly between and across readings. */
static void playhead(void) {
 struct xz_shrt_motion mo;memset(&mo,0,sizeof mo);
 assert(!xz_shrt_motion_step(&mo,1000,0)&&!xz_shrt_motion_step(&mo,1000,33)); /* first readings: unknown -> stopped */
 assert(xz_shrt_motion_step(&mo,1030,66)&&xz_shrt_motion_step(&mo,1030,300)&&!xz_shrt_motion_step(&mo,1030,366));
 struct xz_shrt_position p;xz_shrt_position_reset(&p);int32_t ms;uint32_t at;
 /* Paused: the raw reading. */
 xz_shrt_position_step(&p,0,5000,100,0,0,&ms,&at);assert(ms==5000&&at==100);
 /* Playing at +2.00 %: readings come every 33 ms but advance in 10 ms steps with up to 9 ms of lag.
    The published line must stay within 10 ms of the truth, never step back between frames, and every
    frame-to-frame advance must stay within 3 ms of the true 16.67 ms x 1.02. */
 const int32_t tempo=xz_shrt_rate_from_tempo(200);double truth=5000;int64_t last_shown=-1;double worst=0,worst_step=0;
 for(int frame=1;frame<=600;frame++){
  uint32_t now=100+(uint32_t)(frame*50/3);   /* 60 fps clock in ms */
  truth=5000+(now-100)*1.02;
  if(frame%2==0){                            /* the model refreshes every other frame */
   int32_t reading=(int32_t)(truth/10)*10;   /* coarse stat-frame value */
   xz_shrt_position_step(&p,1,reading,now,tempo,0,&ms,&at);
  }
  double shown=ms+(double)(int32_t)(now-at)*1.02;
  double error=shown-truth;if(error<0)error=-error;
  if(frame>60&&error>worst)worst=error;
  if(last_shown>=0&&frame>60){
   double step=shown-(double)last_shown/1000;assert(step>0);
   double off=step-16.67*1.02;if(off<0)off=-off;if(off>worst_step)worst_step=off;
  }
  last_shown=(int64_t)(shown*1000);
 }
 printf("  playhead: worst error %.1f ms, worst frame step deviation %.2f ms\n",worst,worst_step);
 assert(worst<10&&worst_step<3);
 /* A cue jump snaps at once; a held (repeated) reading changes nothing. */
 xz_shrt_position_step(&p,1,1000,20000,tempo,0,&ms,&at);assert(ms==1000&&at==20000);
 xz_shrt_position_step(&p,1,1000,20000,tempo,0,&ms,&at);assert(ms==1000&&at==20000);
 /* Jog hold while the deck plays at -27 % pitch (the owner's log: the engine position stays at 374804
    while the speed field says 730000 ppm). From the second frozen reading on, the published playhead is
    the reading itself with elapsed_at_ms 0: no extrapolation, so no forward jitter and no snap back. */
 xz_shrt_position_reset(&p);uint32_t t=1000;
 for(int i=0;i<5;i++,t+=33){int32_t pos=374000+(int32_t)(i*33*73/100);xz_shrt_position_step(&p,1,pos,t,730000,1,&ms,&at);assert(at);}
 int following=0;
 for(int i=0;i<20;i++,t+=33){
  following=xz_shrt_position_step(&p,1,374804,t,730000,1,&ms,&at);
  if(i>=1)assert(following&&ms==374804&&at==0);
 }
 /* Released: two regular readings later it smooths and extrapolates again. */
 int32_t pos=374804;
 for(int i=0;i<3;i++){t+=33;pos+=24;following=xz_shrt_position_step(&p,1,pos,t,730000,1,&ms,&at);}
 assert(!following&&at&&ms>=pos-2&&ms<=pos+2);
 /* Scratch backwards and a vinyl-brake slowdown: the playhead follows the readings exactly. */
 for(int i=0;i<10;i++){t+=33;pos-=20;following=xz_shrt_position_step(&p,1,pos,t,1000000,1,&ms,&at);if(i>=1)assert(following&&ms==pos&&at==0);}
 for(int i=0;i<4;i++){t+=33;pos+=33;xz_shrt_position_step(&p,1,pos,t,1000000,1,&ms,&at);}
 assert(at); /* normal speed again */
 for(int i=0;i<6;i++){t+=33;pos+=33*(5-i)/10;following=xz_shrt_position_step(&p,1,pos,t,1000000,1,&ms,&at);if(i>=1)assert(following&&ms==pos&&!at);}
 /* An imprecise source (the stat frame, 10 ms steps) never switches to following. */
 xz_shrt_position_reset(&p);t=0;
 for(int i=0;i<60;i++,t+=33)assert(!xz_shrt_position_step(&p,1,(int32_t)(t/10)*10,t,1000000,0,&ms,&at)&&at);
 struct xz_shrt_frames f;memset(&f,0,sizeof f);
 xz_shrt_frame_mark(&f,1000000);xz_shrt_frame_mark(&f,1016667);xz_shrt_frame_mark(&f,1050000);xz_shrt_frame_mark(&f,3000000);
 assert(f.frames==4&&f.gap_max_us==33333&&f.late==1);
 assert(f.hist[0]==0&&f.hist[1]==1&&f.hist[2]==0&&f.hist[3]==1&&f.hist[4]==0); /* the 1.95 s idle gap is not counted */
 puts("PASS playhead: motion window, smoothed tempo-predicted position, snaps, held readings, jog hold, scratch, brake, frame gap statistics");
}

static void commands(void) {
 struct xz_shrt_queue q;memset(&q,0,sizeof q);struct xz_shrt_command c,out;
 struct xz_shell_action a={XZ_SHELL_ACT_SET_SETTING,1,XZ_SHELL_TIME_ELAPSED,XZ_SHELL_SET_TIME_MODE};
 assert(xz_shrt_setting_command(&a,5,&c)&&c.kind==XZ_SHRT_CMD_TIME_MODE&&c.deck==1&&c.value==0&&c.at_ms==5);
 a.value=XZ_SHELL_TIME_REMAIN;a.deck=-1;
 assert(xz_shrt_setting_command(&a,5,&c)&&c.deck==-1&&c.value==1);
 a.value=7;assert(!xz_shrt_setting_command(&a,5,&c));
 /* QUANTIZE BEAT VALUE choices 1/8, 1/4, 1/2, 1 -> UiSetUtilQuantizeValue 3, 2, 1, 0. */
 for(int i=0;i<4;i++){
  a=(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,i,XZ_SHELL_ITEM_QUANTIZE_BEAT};
  assert(xz_shrt_setting_command(&a,0,&c)&&c.kind==XZ_SHRT_CMD_QUANTIZE_BEAT&&c.value==3-i);
 }
 a.value=4;assert(!xz_shrt_setting_command(&a,0,&c));
 assert(xz_shrt_quantize_index(8)==0&&xz_shrt_quantize_index(1)==3&&xz_shrt_quantize_index(3)==-1);
 assert(!strcmp(xz_shrt_quantize_choices[0],"1/8")&&!strcmp(xz_shrt_quantize_choices[3],"1"));
 static const int refused[]={XZ_SHELL_SET_QUANTIZE,XZ_SHELL_SET_PLAY_MODE,XZ_SHELL_SET_AUTO_CUE,XZ_SHELL_SET_HOT_CUE_AUTO_LOAD,
  XZ_SHELL_SET_WAVE_COLOR,XZ_SHELL_SET_WAVE_POSITION,XZ_SHELL_ITEM_LOAD_LOCK,XZ_SHELL_ITEM_LCD_BRIGHTNESS};
 for(size_t i=0;i<sizeof refused/sizeof *refused;i++){
  a=(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,0,1,refused[i]};assert(!xz_shrt_setting_command(&a,0,&c));
  if(xz_shrt_setting_writable(a.id,1,1)&&xz_shrt_setting_command(&a,0,&c))assert(xz_shrt_queue_push(&q,&c));
  assert(!q.count&&!xz_shrt_queue_pop(&q,&out));
 }
 a=(struct xz_shell_action){XZ_SHELL_ACT_LOAD,0,0,5};assert(!xz_shrt_setting_command(&a,0,&c));
 /* Shell-owned display settings are stored and restored, never stock commands. */
 assert(xz_shrt_shell_owned_setting(XZ_SHELL_SET_WAVE_COLOR)&&xz_shrt_shell_owned_setting(XZ_SHELL_SET_WAVE_POSITION)&&
        xz_shrt_shell_owned_setting(XZ_SHELL_ITEM_KEY_DISPLAY)&&xz_shrt_shell_owned_setting(XZ_SHELL_ITEM_WAVE_ZOOM));
 assert(!xz_shrt_shell_owned_setting(XZ_SHELL_SET_TIME_MODE)&&!xz_shrt_shell_owned_setting(XZ_SHELL_ITEM_QUANTIZE_BEAT));
 {
  struct xz_shell s;xz_shell_init(&s);
  assert(xz_shell_restore_setting(&s,XZ_SHELL_SET_WAVE_COLOR,XZ_SHELL_WAVE_RGB)&&s.display.wave_color==XZ_SHELL_WAVE_RGB);
  assert(xz_shell_restore_setting(&s,XZ_SHELL_ITEM_WAVE_ZOOM,5)&&s.zoom==5&&!xz_shell_restore_setting(&s,XZ_SHELL_SET_TIME_MODE,0));
 }
 /* Queue: FIFO, a repeated setting replaces its queued value, LOADs never merge, overflow drops. */
 c=(struct xz_shrt_command){XZ_SHRT_CMD_TIME_MODE,0,1,0,0,1};assert(xz_shrt_queue_push(&q,&c));
 c.value=0;assert(xz_shrt_queue_push(&q,&c)&&q.count==1&&q.coalesced==1);
 c=(struct xz_shrt_command){XZ_SHRT_CMD_LOAD,0,0,101,2,2};assert(xz_shrt_queue_push(&q,&c)&&xz_shrt_queue_push(&q,&c)&&q.count==3);
 assert(xz_shrt_queue_pop(&q,&out)&&out.kind==XZ_SHRT_CMD_TIME_MODE&&out.value==0);
 assert(xz_shrt_queue_pop(&q,&out)&&out.kind==XZ_SHRT_CMD_LOAD&&xz_shrt_queue_pop(&q,&out)&&!xz_shrt_queue_pop(&q,&out));
 for(int i=0;i<XZ_SHRT_QUEUE;i++){c=(struct xz_shrt_command){XZ_SHRT_CMD_LOAD,0,0,(uint32_t)i,2,0};assert(xz_shrt_queue_push(&q,&c));}
 assert(!xz_shrt_queue_push(&q,&c)&&q.dropped==1);
 for(int i=0;i<XZ_SHRT_QUEUE;i++){assert(xz_shrt_queue_pop(&q,&out)&&out.track_id==(uint32_t)i);}
 assert(xz_shrt_frame_period_ms(XZ_SHELL_PLAY,1)==12&&xz_shrt_frame_period_ms(XZ_SHELL_PLAY,0)==100&&xz_shrt_frame_period_ms(XZ_SHELL_BROWSE,1)==100);
 /* A 60 Hz caller with 2 ms of jitter is due on every tick at the live period. */
 assert(XZ_SHRT_FRAME_LIVE_MS<=16-2&&XZ_SHRT_FRAME_LIVE_MS>1000/90);
 puts("PASS commands: C3 setting mapping, refusals, coalescing queue, overflow, frame periods");
}

static void writable_settings(void) {
 for(int time=0;time<2;time++)for(int quantize=0;quantize<2;quantize++){
  for(int id=0;id<XZ_SHELL_ITEM_ID_END;id++)
   assert(xz_shrt_setting_writable(id,time,quantize)==(id==XZ_SHELL_ITEM_TIME_MODE?time:id==XZ_SHELL_ITEM_QUANTIZE_BEAT?quantize:0));
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_action actions[XZ_SHELL_ACTIONS];
  struct xz_shrt_queue q={0};struct xz_shrt_command c;
  xz_shell_init(&u);fx_model_full(&m,&track);m.settings.writable=0;
  for(int id=0;id<XZ_SHELL_SETTING_COUNT;id++)
   if(xz_shrt_setting_writable(id,time,quantize))m.settings.writable|=(uint64_t)1<<id;
  assert(m.settings.writable==(time?(uint64_t)1<<XZ_SHELL_SET_TIME_MODE:0));
  const struct xz_shell_event events[]={
   {XZ_SHELL_TOUCH_DOWN,20,12,0,0,-1}, /* PLAY MODE has no setter. */
   {XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_QUANTIZE,0},
   {XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_TIME_MODE,0}};
  for(size_t i=0;i<sizeof events/sizeof *events;i++){
   size_t n=xz_shell_input(&u,&m,&events[i],actions);
   assert(n==(size_t)(i==2&&time));
   for(size_t j=0;j<n;j++){
    assert(xz_shrt_setting_writable(actions[j].id,time,quantize));
    assert(xz_shrt_setting_command(&actions[j],0,&c)&&xz_shrt_queue_push(&q,&c));
   }
  }
  assert(q.count==(uint32_t)time);
  if(time)assert(xz_shrt_queue_pop(&q,&c)&&c.kind==XZ_SHRT_CMD_TIME_MODE);
  assert(!xz_shrt_queue_pop(&q,&c));
 }
 puts("PASS per-setting writable rule: shared IDs, runtime guard combinations, refused settings never queued");
}
static const char *title_of(const struct xz_shlib *L, uint32_t id) {
 const struct xz_shlib_track *t=xz_shlib_track(L,id);assert(t);return xz_shlib_text(L,t->title);
}
static struct xz_shlib_list *build(struct xz_shlib *L, enum xz_shell_screen s, enum xz_shell_browse_category c, int depth, const uint32_t *path, const char *query) {
 struct xz_shlib_request r;memset(&r,0,sizeof r);
 r.screen=s;r.category=c;r.depth=depth;
 for(int i=0;i<depth;i++)r.path[i]=path[i];
 if(query)snprintf(r.query,sizeof r.query,"%s",query);
 struct xz_shlib_list *l=NULL;
 assert(!xz_shlib_build(L,&r,&l)&&l);
 return l;
}
static void library(const char *path) {
 struct xz_shlib *L=NULL;
 assert(xz_shlib_open("no-such-file.pdb",1u<<20,&L)&&!L);
 assert(!xz_shlib_open(path,1u<<20,&L)&&L);
 assert(xz_shlib_track_count(L)==4&&xz_shlib_track(L,101)&&xz_shlib_track(L,404)&&!xz_shlib_track(L,1));
 assert(!strcmp(title_of(L,101),"Alpha Night"));
 const struct xz_shlib_track *t=xz_shlib_track(L,101);
 assert(t->bpm_x100==12800&&t->duration_ms==240000&&t->rating==5&&t->color_id==1&&!strcmp(xz_shlib_text(L,t->file_name),"Alpha Night.mp3"));
 assert(!strcmp(xz_shlib_text(L,t->file_path),"/Contents/Alpha Night.mp3")); /* the wave prefetch resolves by it */
 assert(!strcmp(xz_shlib_text(L,t->artist),"Night Artist")&&!strcmp(xz_shlib_text(L,t->key),"8A"));
 /* TRACK: every track, title order, with an alphabet index; window rows carry the readouts. */
 struct xz_shlib_list *l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_TRACK,0,NULL,NULL);
 assert(l->total==4&&!l->folders&&l->depth==0&&!strcmp(l->names+l->level_name,"TRACK")&&l->sort_column==XZ_SHELL_COL_TITLE);
 assert(l->letter_count>=2&&l->letters[0].row==0);
 struct xz_shell_browse_row rows[XZ_SHELL_BROWSE_WINDOW];uint32_t on_deck[2]={101,0};
 int n=xz_shlib_window(L,l,0,on_deck,3,rows);
 assert(n==4);
 int found=0;
 for(int i=0;i<n;i++){
  assert(rows[i].kind==XZ_SHELL_ROW_TRACK&&(rows[i].known&(1u<<XZ_SHELL_RF_TITLE)));
  if(rows[i].id==101){found=1;assert(rows[i].bpm_x100==12800&&rows[i].duration_ms==240000&&rows[i].on_deck==1&&
   rows[i].color==xz_shlib_color_rgb(1)&&(rows[i].known&(1u<<XZ_SHELL_RF_ON_DECK))&&!strcmp(rows[i].key,"8A"));}
  else assert(!rows[i].on_deck);
 }
 assert(found);
 assert(xz_shlib_window(L,l,3,on_deck,1,rows)==1&&!(rows[0].known&(1u<<XZ_SHELL_RF_ON_DECK)));
 assert(xz_shlib_window(L,l,4,NULL,0,rows)==0);
 uint32_t first=l->id[0];
 xz_shlib_list_free(l);
 /* Sorting: BPM ascending, then title descending reverses the title order. */
 struct xz_shlib_request r;memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_TRACK;
 r.sort_set=1;r.sort_column=XZ_SHELL_COL_BPM;
 assert(!xz_shlib_build(L,&r,&l));
 assert(l->id[0]==303&&l->sort_column==XZ_SHELL_COL_BPM&&l->letter_count==0);
 for(int i=1;i<l->total;i++)assert(xz_shlib_track(L,l->id[i-1])->bpm_x100<=xz_shlib_track(L,l->id[i])->bpm_x100);
 xz_shlib_list_free(l);
 r.sort_column=XZ_SHELL_COL_TITLE;r.sort_descending=1;
 assert(!xz_shlib_build(L,&r,&l)&&l->id[3]==first&&l->sort_descending);
 xz_shlib_list_free(l);
 /* ARTIST: groups at the top, the group's tracks one level down. */
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_ARTIST,0,NULL,NULL);
 assert(l->folders&&l->total==3);
 int artist_row=-1;
 for(int i=0;i<l->total;i++){assert(l->kind[i]==XZ_SHELL_ROW_FOLDER);if(l->id[i]==1){artist_row=i;assert(!strcmp(l->names+l->name[i],"Night Artist"));}}
 assert(artist_row>=0);
 n=xz_shlib_window(L,l,0,NULL,0,rows);assert(n==3&&rows[artist_row].known==(1u<<XZ_SHELL_RF_TITLE));
 uint32_t list_top=l->list_id;
 xz_shlib_list_free(l);
 uint32_t artist=1;
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_ARTIST,1,&artist,NULL);
 assert(!l->folders&&l->total==2&&l->depth==1&&!strcmp(l->names+l->level_name,"Night Artist")&&l->has_parent&&
        !strcmp(l->names+l->parent_name,"ARTIST")&&l->list_id!=list_top);
 for(int i=0;i<l->total;i++)assert(xz_shlib_track(L,l->id[i])->artist_id==1);
 xz_shlib_list_free(l);
 /* BPM groups are numeric and named by value. */
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_BPM,0,NULL,NULL);
 assert(l->total==3&&!strcmp(l->names+l->name[0],"90.00"));
 xz_shlib_list_free(l);
 /* PLAYLIST tree: folders by sort order, a playlist's entries in their order (repeats kept). */
 l=build(L,XZ_SHELL_PLAYLIST,XZ_SHELL_CAT_TRACK,0,NULL,NULL);
 assert(l->category==XZ_SHELL_CAT_PLAYLIST&&l->folders&&l->total==2&&l->id[0]==1000&&l->id[1]==2002);
 assert(!strcmp(l->names+l->name[0],"Sets")&&!strcmp(l->names+l->level_name,"PLAYLIST"));
 xz_shlib_list_free(l);
 uint32_t sets[2]={1000,2000};
 l=build(L,XZ_SHELL_PLAYLIST,XZ_SHELL_CAT_PLAYLIST,1,sets,NULL);
 assert(l->folders&&l->total==2&&l->id[0]==1001&&l->id[1]==2000&&!strcmp(l->names+l->level_name,"Sets"));
 xz_shlib_list_free(l);
 l=build(L,XZ_SHELL_PLAYLIST,XZ_SHELL_CAT_PLAYLIST,2,sets,NULL);
 assert(!l->folders&&l->total==4&&l->id[0]==202&&l->id[1]==303&&l->id[2]==101&&l->id[3]==101);
 assert(!strcmp(l->names+l->level_name,"Opening")&&!strcmp(l->names+l->parent_name,"Sets")&&l->sort_column==XZ_SHELL_COL_NUMBER);
 xz_shlib_list_free(l);
 uint32_t empty=2002;
 l=build(L,XZ_SHELL_PLAYLIST,XZ_SHELL_CAT_PLAYLIST,1,&empty,NULL);assert(l->total==0);xz_shlib_list_free(l);
 /* HISTORY: the sessions, then one session's tracks in play order. */
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_HISTORY,0,NULL,NULL);
 assert(l->folders&&l->total==1&&l->id[0]==3000);xz_shlib_list_free(l);
 uint32_t session=3000;
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_HISTORY,1,&session,NULL);
 assert(l->total==2&&l->id[0]==101&&l->id[1]==202);xz_shlib_list_free(l);
 /* SEARCH: casefolded terms; an empty query lists nothing. */
 l=build(L,XZ_SHELL_SEARCH,XZ_SHELL_CAT_TRACK,0,NULL,"alpha");assert(l->total==1&&l->id[0]==101);xz_shlib_list_free(l);
 l=build(L,XZ_SHELL_SEARCH,XZ_SHELL_CAT_TRACK,0,NULL,"strasse");assert(l->total==1&&l->id[0]==404);xz_shlib_list_free(l);
 l=build(L,XZ_SHELL_SEARCH,XZ_SHELL_CAT_TRACK,0,NULL,"");assert(l->total==0);xz_shlib_list_free(l);
 /* FOLDER: the stick's folders that hold library tracks; subfolders first, then the folder's tracks by title. */
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_FOLDER,0,NULL,NULL);
 assert(l->folders&&l->total==1&&l->kind[0]==XZ_SHELL_ROW_FOLDER&&!strcmp(l->names+l->name[0],"Contents"));
 uint32_t folder[3]={l->id[0],0,0};xz_shlib_list_free(l);
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_FOLDER,1,folder,NULL);
 assert(l->folders&&l->total==4&&l->kind[0]==XZ_SHELL_ROW_FOLDER&&!strcmp(l->names+l->name[0],"Deep"));
 assert(!strcmp(l->names+l->level_name,"Contents")&&l->has_parent&&!strcmp(l->names+l->parent_name,"FOLDER"));
 for(int i=1;i<4;i++)assert(l->kind[i]==XZ_SHELL_ROW_TRACK&&l->id[i]!=404);
 folder[1]=l->id[0];xz_shlib_list_free(l);
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_FOLDER,2,folder,NULL);
 assert(l->total==1&&!strcmp(l->names+l->name[0],"Cuts")&&!strcmp(l->names+l->parent_name,"Contents"));
 folder[2]=l->id[0];xz_shlib_list_free(l);
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_FOLDER,3,folder,NULL);
 assert(!l->folders&&l->total==1&&l->id[0]==404&&!strcmp(l->names+l->level_name,"Cuts")&&!strcmp(l->names+l->parent_name,"Deep"));
 xz_shlib_list_free(l);
 /* A folder that is not inside the one before it fails. */
 {uint32_t bad[2]={folder[0],folder[2]};memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_FOLDER;r.depth=2;
  r.path[0]=bad[0];r.path[1]=bad[1];l=NULL;assert(xz_shlib_build(L,&r,&l)&&!l);
  r.depth=1;r.path[0]=0;assert(xz_shlib_build(L,&r,&l)&&!l);r.path[0]=999;assert(xz_shlib_build(L,&r,&l)&&!l);}
 /* MATCHING: empty without a master deck; else BPM within the range and a compatible key. */
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_MATCHING,0,NULL,NULL);assert(l->total==0&&!strcmp(l->names+l->level_name,"MATCHING"));xz_shlib_list_free(l);
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_MATCHING;r.matching.on=1;
 r.matching.bpm_min_x100=12032;r.matching.bpm_max_x100=13568;l=NULL;
 assert(!xz_shlib_build(L,&r,&l)&&l&&l->total==3);for(int i=0;i<3;i++)assert(l->id[i]!=303);xz_shlib_list_free(l);
 snprintf(r.matching.master_key,sizeof r.matching.master_key,"%s","8A");l=NULL;
 assert(!xz_shlib_build(L,&r,&l)&&l&&l->total==1&&l->id[0]==101);xz_shlib_list_free(l);
 /* Unsupported paths fail without a list. */
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;l=NULL;
 r.category=XZ_SHELL_CAT_TRACK;r.depth=1;assert(xz_shlib_build(L,&r,&l)&&!l);
 r.category=XZ_SHELL_CAT_MATCHING;r.depth=1;assert(xz_shlib_build(L,&r,&l)&&!l);
 r.category=XZ_SHELL_CAT_PLAYLIST;r.depth=1;r.path[0]=77;assert(xz_shlib_build(L,&r,&l)&&!l);
 assert(xz_shlib_categories()==(1u<<XZ_SHELL_CAT_COUNT)-1u);
 /* NEW lands in an open folder: folder nodes are told from playlists. */
 assert(xz_shlib_playlist_is_folder(L,1000)==1&&xz_shlib_playlist_is_folder(L,2000)==0&&xz_shlib_playlist_is_folder(L,77)==-1);
 /* INFO: the full record, with jump links for the catalog fields. */
 struct xz_shell_info_track info;
 assert(xz_shlib_info(L,101,XZ_SHELL_SRC_USB1,&info));
 assert(info.track_id==101&&!strcmp(info.title,"Alpha Night")&&!strcmp(info.album,"First Album")&&!strcmp(info.genre,"House"));
 assert(info.bpm_x100==12800&&info.duration_ms==240000&&info.rating==5&&info.color==XZ_SHELL_COLOR_PINK&&info.source==XZ_SHELL_SRC_USB1);
 assert(info.artist_id==1&&(info.links&XZ_SHELL_KNOWN(XZ_SHELL_I_ARTIST))&&(info.links&XZ_SHELL_KNOWN(XZ_SHELL_I_BPM)));
 assert(!strcmp(info.file_name,"Alpha Night.mp3")&&(info.known&XZ_SHELL_KNOWN(XZ_SHELL_I_FILE_NAME)));
 assert(xz_shlib_info(L,404,XZ_SHELL_SRC_USB2,&info)&&!(info.links&XZ_SHELL_KNOWN(XZ_SHELL_I_ALBUM))&&!(info.known&XZ_SHELL_KNOWN(XZ_SHELL_I_KEY)));
 assert(!xz_shlib_info(L,1,XZ_SHELL_SRC_USB1,&info)&&!info.known);
 /* Filter on the worker over the whole list: BPM range, rating, colour, key. */
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_TRACK;
 r.filter.on=1;r.filter.bpm_min_x100=12000;r.filter.bpm_max_x100=13000;
 assert(!xz_shlib_build(L,&r,&l)&&l->total==3);
 for(int i=0;i<l->total;i++){uint32_t b=xz_shlib_track(L,l->id[i])->bpm_x100;assert(b>=12000&&b<=13000);}
 xz_shlib_list_free(l);
 r.filter.rating_min=4;assert(!xz_shlib_build(L,&r,&l)&&l->total==2);xz_shlib_list_free(l);
 r.filter.rating_min=0;r.filter.colors=1u<<XZ_SHELL_TAG_COLOR_PINK;
 assert(!xz_shlib_build(L,&r,&l)&&l->total==1&&l->id[0]==101);xz_shlib_list_free(l);
 memset(&r.filter,0,sizeof r.filter);r.filter.on=1;snprintf(r.filter.master_key,sizeof r.filter.master_key,"8A");
 assert(!xz_shlib_build(L,&r,&l));
 {int has101=0,has404=0;for(int i=0;i<l->total;i++){has101|=l->id[i]==101;has404|=l->id[i]==404;}assert(has101&&!has404);}
 xz_shlib_list_free(l);
 /* Rows carry album, genre and date added; every column sorts. */
 l=build(L,XZ_SHELL_BROWSE,XZ_SHELL_CAT_TRACK,0,NULL,NULL);
 n=xz_shlib_window(L,l,0,NULL,0,rows);
 for(int i=0;i<n;i++)if(rows[i].id==101)assert((rows[i].known&(1u<<XZ_SHELL_RF_ALBUM))&&!strcmp(rows[i].album,"First Album")&&
  !strcmp(rows[i].genre,"House")&&(rows[i].known&(1u<<XZ_SHELL_RF_DATE_ADDED))&&rows[i].date_added==20261018);
 xz_shlib_list_free(l);
 for(int c=0;c<XZ_SHELL_COL_COUNT;c++)if((xz_shlib_sort_keys()>>c)&1u){
  memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_TRACK;r.sort_set=1;r.sort_column=c;
  assert(!xz_shlib_build(L,&r,&l)&&l->total==4);xz_shlib_list_free(l);
 }
 /* Playlist edits merged into the stick's tree: rename, delete, create with a track, a moved order. */
 struct xz_shpl_set e;xz_shpl_init(&e);uint32_t made=0;
 struct xz_shell_playlist_edit ed={XZ_SHELL_PL_RENAME,2000,0,0,0,"Warm Up"};
 assert(!xz_shpl_apply(&e,&ed,NULL,0,"Opening",NULL));
 ed=(struct xz_shell_playlist_edit){XZ_SHELL_PL_CREATE,0,0,0,0,"New List"};
 assert(!xz_shpl_apply(&e,&ed,NULL,0,NULL,&made)&&made==XZ_SHPL_FIRST_CREATED);
 ed=(struct xz_shell_playlist_edit){XZ_SHELL_PL_ADD_TRACK,made,404,0,0,""};assert(!xz_shpl_apply(&e,&ed,NULL,0,NULL,NULL));
 ed=(struct xz_shell_playlist_edit){XZ_SHELL_PL_DELETE,2002,0,0,0,""};assert(!xz_shpl_apply(&e,&ed,NULL,0,NULL,NULL));
 uint32_t *base;uint32_t bn;assert(!xz_shlib_playlist_tracks(L,2000,&base,&bn)&&bn==4&&base[0]==202);
 ed=(struct xz_shell_playlist_edit){XZ_SHELL_PL_MOVE_TRACK,2000,101,3,0,""};
 assert(!xz_shpl_apply(&e,&ed,base,bn,"Opening",NULL));free(base);
 ed=(struct xz_shell_playlist_edit){XZ_SHELL_PL_REMOVE_TRACK,2000,999,0,0,""};assert(xz_shpl_apply(&e,&ed,NULL,0,NULL,NULL));
 ed=(struct xz_shell_playlist_edit){XZ_SHELL_PL_RENAME,2002,0,0,0,"Back"};assert(xz_shpl_apply(&e,&ed,NULL,0,NULL,NULL));
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_PLAYLIST;r.category=XZ_SHELL_CAT_PLAYLIST;r.edits=&e;
 assert(!xz_shlib_build(L,&r,&l)&&l->total==2&&l->id[0]==1000&&l->id[1]==made&&!strcmp(l->names+l->name[1],"New List"));
 xz_shlib_list_free(l);
 r.depth=2;r.path[0]=1000;r.path[1]=2000;
 assert(!xz_shlib_build(L,&r,&l)&&!strcmp(l->names+l->level_name,"Warm Up")&&l->total==4&&
        l->id[0]==101&&l->id[1]==202&&l->id[2]==303&&l->id[3]==101);
 xz_shlib_list_free(l);
 r.depth=1;r.path[0]=made;assert(!xz_shlib_build(L,&r,&l)&&l->total==1&&l->id[0]==404);xz_shlib_list_free(l);
 r.path[0]=2002;assert(xz_shlib_build(L,&r,&l)&&!l);
 struct xz_shlib_playlist_ref refs[8];int nr=xz_shlib_playlist_refs(L,&e,refs,8);
 assert(nr==3);
 for(int i=0;i<nr;i++){
  if(refs[i].id==2000)assert(!strcmp(refs[i].name,"Warm Up")&&refs[i].tracks==4);
  else if(refs[i].id==made)assert(refs[i].tracks==1);
  else assert(refs[i].id==2001);
 }
 /* The sidecar: text round trip, then an atomic save and load. */
 size_t len=0;char *text=xz_shpl_serialize(&e,&len);
 struct xz_shpl_set back;assert(text&&!xz_shpl_parse(&back,text,len)&&back.n==e.n&&back.next_id==e.next_id);
 const struct xz_shpl *x=xz_shpl_find(&back,2000);
 assert(x&&x->renamed&&x->has_tracks&&!strcmp(x->name,"Warm Up")&&x->count==4&&x->tracks[3]==101);
 assert(xz_shpl_find(&back,2002)->deleted&&xz_shpl_find(&back,made)->created&&!strcmp(xz_shpl_find(&back,made)->name,"New List"));
 free(text);xz_shpl_free(&back);
 assert(!xz_shpl_save(&e,"."));
 assert(!xz_shpl_load(&back,".")&&back.n==e.n&&xz_shpl_find(&back,made)->count==1);xz_shpl_free(&back);
 assert(!xz_shpl_load(&back,"no-such-root")&&back.n==0);
 /* Reserved shell lists in the same set: TAG LIST order, no duplicates, the 100-track limit, played marks,
    deleted histories; never listed as playlists; round trip through the sidecar text. */
 assert(xz_shpl_reserved(XZ_SHPL_TAG_LIST)&&!xz_shpl_reserved(made)&&!xz_shpl_reserved(2000));
 assert(xz_shpl_list_add(&e,XZ_SHPL_TAG_LIST,303,100)==1&&xz_shpl_list_add(&e,XZ_SHPL_TAG_LIST,101,100)==1);
 assert(xz_shpl_list_add(&e,XZ_SHPL_TAG_LIST,303,100)==0&&xz_shpl_list_has(&e,XZ_SHPL_TAG_LIST,101));
 assert(xz_shpl_list_add(&e,XZ_SHPL_TAG_LIST,404,2)==-1);
 {uint32_t n;const uint32_t *t=xz_shpl_list(&e,XZ_SHPL_TAG_LIST,&n);assert(n==2&&t[0]==303&&t[1]==101);}
 assert(xz_shpl_list_add(&e,XZ_SHPL_HISTORY_DELETED,3000,0)==1&&xz_shpl_list_add(&e,XZ_SHPL_UNPLAYED,202,0)==1);
 nr=xz_shlib_playlist_refs(L,&e,refs,8);for(int i=0;i<nr;i++)assert(!xz_shpl_reserved(refs[i].id));
 /* The TAG LIST screen lists it in order; deleted histories drop out of HISTORY. */
 r.edits=&e;
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_TAG_LIST;r.edits=&e;l=NULL;
 assert(!xz_shlib_build(L,&r,&l)&&l&&l->total==2&&l->id[0]==303&&l->id[1]==101&&!strcmp(l->names+l->level_name,"TAG LIST"));xz_shlib_list_free(l);
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_HISTORY;r.edits=&e;l=NULL;
 assert(!xz_shlib_build(L,&r,&l)&&l&&l->total==0);xz_shlib_list_free(l);
 r.depth=1;r.path[0]=3000;l=NULL;assert(xz_shlib_build(L,&r,&l)&&!l);
 {uint32_t h[4];assert(xz_shlib_history_ids(L,h,4)==1&&h[0]==3000);}
 /* A playlist from the TAG LIST: the created playlist takes exactly those tracks. */
 {uint32_t pl=0;struct xz_shell_playlist_edit c={XZ_SHELL_PL_CREATE,0,0,0,0,"From Tags"};assert(!xz_shpl_apply(&e,&c,NULL,0,NULL,&pl));
  uint32_t n;const uint32_t *t=xz_shpl_list(&e,XZ_SHPL_TAG_LIST,&n);assert(!xz_shpl_set_tracks(&e,pl,t,n));
  assert(xz_shpl_find(&e,pl)->count==2&&xz_shpl_find(&e,pl)->tracks[0]==303);
  assert(xz_shpl_set_tracks(&e,2000,t,n));}
 xz_shpl_list_remove(&e,XZ_SHPL_TAG_LIST,303);
 {uint32_t n;const uint32_t *t=xz_shpl_list(&e,XZ_SHPL_TAG_LIST,&n);assert(n==1&&t[0]==101);}
 text=xz_shpl_serialize(&e,&len);assert(text&&!xz_shpl_parse(&back,text,len));
 assert(xz_shpl_list_has(&back,XZ_SHPL_TAG_LIST,101)&&xz_shpl_list_has(&back,XZ_SHPL_HISTORY_DELETED,3000)&&xz_shpl_list_has(&back,XZ_SHPL_UNPLAYED,202));
 free(text);xz_shpl_free(&back);
 xz_shpl_list_clear(&e,XZ_SHPL_TAG_LIST);{uint32_t n;assert(!xz_shpl_list(&e,XZ_SHPL_TAG_LIST,&n)&&!n);}
 xz_shpl_free(&e);
 /* A too-small arena fails cleanly rather than truncating. */
 struct xz_shlib *tiny=NULL;assert(xz_shlib_open(path,4096,&tiny)&&!tiny);
 xz_shlib_free(L);
 puts("PASS library: tracks, TRACK/ARTIST/BPM/PLAYLIST/HISTORY/SEARCH lists, windows, on-deck, sorting, INFO records, failures");
 puts("PASS filter (BPM, rating, colour, key), album/genre/date rows, every sort key, merged playlist edits, sidecar save/load");
 puts("PASS TAG LIST, played marks and deleted histories in the sidecar; TAG LIST screen, HISTORY filtering, playlist from tags");
}

int main(int argc, char **argv) {
 assert(argc==2);
 fx_track_build(&track,1);
 key_table();verdicts();load_verdicts();contacts();touches();pushes();hand_off();playhead();commands();writable_settings();library(argv[1]);
 puts("PASS shell runtime");
 return 0;
}
