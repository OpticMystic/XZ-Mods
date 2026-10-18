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
#include <string.h>

static struct fx_track track;
static struct xz_shell_browse_model browse_slot;
static struct xz_shell_settings_model settings_slot;

static void key_table(void) {
 static const struct { uint16_t code; unsigned channel; int rule, key, hold; } expect[]={
  {0x209,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_BROWSE,-1},{0x20a,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TAG_LIST,-1},
  {0x206,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_INFO,-1},{0x20b,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_MENU,XZ_SHELL_KEY_MENU_HOLD},
  {0x20e,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SHORTCUT,-1},{0x203,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE,-1},
  {0x204,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE,-1},{0x420c,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TAG_TRACK,-1},
  {0x4208,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_BACK,XZ_SHELL_KEY_BACK_HOLD},
  {0x430f,1,XZ_SHRT_STOCK,-1,-1},{0x430f,2,XZ_SHRT_STOCK,-1,-1},{0x430b,1,XZ_SHRT_STOCK,-1,-1},{0x430c,2,XZ_SHRT_STOCK,-1,-1},
 };
 for(size_t i=0;i<sizeof expect/sizeof *expect;i++){
  const struct xz_shrt_key_map *k=xz_shrt_key_lookup(expect[i].code,expect[i].channel);
  assert(k&&k->rule==expect[i].rule&&k->key==expect[i].key&&k->hold==expect[i].hold);
 }
 /* Deck keys, jog, LINK and REKORDBOX are not in the table; LOAD on a third channel is not either. */
 assert(!xz_shrt_key_lookup(0x4305,1)&&!xz_shrt_key_lookup(0x4101,1)&&!xz_shrt_key_lookup(0x205,0)&&!xz_shrt_key_lookup(0x202,0));
 assert(!xz_shrt_key_lookup(0x430f,3));
 assert(xz_shrt_phase_of(0)==XZ_SHRT_PRESS&&xz_shrt_phase_of(1)==XZ_SHRT_HOLD&&xz_shrt_phase_of(2)==XZ_SHRT_RELEASE&&xz_shrt_phase_of(3)==XZ_SHRT_RELEASE);
 const struct xz_shrt_key_map *menu=xz_shrt_key_lookup(0x20b,0),*info=xz_shrt_key_lookup(0x206,0);
 assert(xz_shrt_key_for_phase(menu,XZ_SHRT_PRESS,0)==-1);
 assert(xz_shrt_key_for_phase(menu,XZ_SHRT_HOLD,0)==XZ_SHELL_KEY_MENU_HOLD&&xz_shrt_key_for_phase(menu,XZ_SHRT_HOLD,1)==-1);
 assert(xz_shrt_key_for_phase(menu,XZ_SHRT_RELEASE,0)==XZ_SHELL_KEY_MENU&&xz_shrt_key_for_phase(menu,XZ_SHRT_RELEASE,1)==-1);
 assert(xz_shrt_key_for_phase(info,XZ_SHRT_PRESS,0)==XZ_SHELL_KEY_INFO&&xz_shrt_key_for_phase(info,XZ_SHRT_RELEASE,0)==-1);
 assert(xz_shrt_key_for_phase(xz_shrt_key_lookup(0x430f,1),XZ_SHRT_PRESS,0)==-1);
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
 assert(xz_shrt_key_verdict(&u,&m,menu)==XZ_SHRT_PASS_HAND_OFF);
 assert(xz_shrt_key_verdict(&u,&m,tag)==XZ_SHRT_PASS);   /* TAG TRACK without the capability is stock's */
 assert(xz_shrt_key_verdict(&u,&m,back)==XZ_SHRT_PASS);  /* BACK on the bare PLAY screen */
 assert(xz_shrt_key_verdict(&u,&m,load)==XZ_SHRT_PASS);
 assert(!memcmp(&before,&u,sizeof u));                     /* probes never touch the shell */
 /* With BROWSE and the settings slot the shell keeps them; MENU release still hands stock its menu popup. */
 m.browse=&browse_slot;m.settings_screen=&settings_slot;m.capabilities=XZ_SHELL_CAP_BROWSE;
 assert(xz_shrt_key_verdict(&u,&m,browse_key)==XZ_SHRT_SWALLOW);
 assert(xz_shrt_key_verdict(&u,&m,menu)==XZ_SHRT_SWALLOW);
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_input(&u,&m,&e,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 assert(xz_shrt_key_verdict(&u,&m,back)==XZ_SHRT_SWALLOW); /* BACK below PLAY belongs to the shell */
 puts("PASS press-time verdicts: hand-off pass-through, inert keys to stock, swallowed keys, side-effect-free probes");
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
 puts("PASS hand-off: grace resume, leave-and-return resume, flicker, unknown state, BACK hold, cautions");
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
 }
 a=(struct xz_shell_action){XZ_SHELL_ACT_LOAD,0,0,5};assert(!xz_shrt_setting_command(&a,0,&c));
 /* Queue: FIFO, a repeated setting replaces its queued value, LOADs never merge, overflow drops. */
 c=(struct xz_shrt_command){XZ_SHRT_CMD_TIME_MODE,0,1,0,0,1};assert(xz_shrt_queue_push(&q,&c));
 c.value=0;assert(xz_shrt_queue_push(&q,&c)&&q.count==1&&q.coalesced==1);
 c=(struct xz_shrt_command){XZ_SHRT_CMD_LOAD,0,0,101,2,2};assert(xz_shrt_queue_push(&q,&c)&&xz_shrt_queue_push(&q,&c)&&q.count==3);
 assert(xz_shrt_queue_pop(&q,&out)&&out.kind==XZ_SHRT_CMD_TIME_MODE&&out.value==0);
 assert(xz_shrt_queue_pop(&q,&out)&&out.kind==XZ_SHRT_CMD_LOAD&&xz_shrt_queue_pop(&q,&out)&&!xz_shrt_queue_pop(&q,&out));
 for(int i=0;i<XZ_SHRT_QUEUE;i++){c=(struct xz_shrt_command){XZ_SHRT_CMD_LOAD,0,0,(uint32_t)i,2,0};assert(xz_shrt_queue_push(&q,&c));}
 assert(!xz_shrt_queue_push(&q,&c)&&q.dropped==1);
 for(int i=0;i<XZ_SHRT_QUEUE;i++){assert(xz_shrt_queue_pop(&q,&out)&&out.track_id==(uint32_t)i);}
 assert(xz_shrt_frame_period_ms(XZ_SHELL_PLAY,1)==33&&xz_shrt_frame_period_ms(XZ_SHELL_PLAY,0)==100&&xz_shrt_frame_period_ms(XZ_SHELL_BROWSE,1)==100);
 puts("PASS commands: C3 setting mapping, refusals, coalescing queue, overflow, frame periods");
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
 /* Unsupported categories and paths fail without a list. */
 memset(&r,0,sizeof r);r.screen=XZ_SHELL_BROWSE;r.category=XZ_SHELL_CAT_FOLDER;l=NULL;
 assert(xz_shlib_build(L,&r,&l)&&!l);
 r.category=XZ_SHELL_CAT_TRACK;r.depth=1;assert(xz_shlib_build(L,&r,&l)&&!l);
 r.category=XZ_SHELL_CAT_PLAYLIST;r.depth=1;r.path[0]=77;assert(xz_shlib_build(L,&r,&l)&&!l);
 assert(!(xz_shlib_categories()&(1u<<XZ_SHELL_CAT_FOLDER))&&(xz_shlib_categories()&(1u<<XZ_SHELL_CAT_PLAYLIST)));
 /* INFO: the full record, with jump links for the catalog fields. */
 struct xz_shell_info_track info;
 assert(xz_shlib_info(L,101,XZ_SHELL_SRC_USB1,&info));
 assert(info.track_id==101&&!strcmp(info.title,"Alpha Night")&&!strcmp(info.album,"First Album")&&!strcmp(info.genre,"House"));
 assert(info.bpm_x100==12800&&info.duration_ms==240000&&info.rating==5&&info.color==XZ_SHELL_COLOR_PINK&&info.source==XZ_SHELL_SRC_USB1);
 assert(info.artist_id==1&&(info.links&XZ_SHELL_KNOWN(XZ_SHELL_I_ARTIST))&&(info.links&XZ_SHELL_KNOWN(XZ_SHELL_I_BPM)));
 assert(!strcmp(info.file_name,"Alpha Night.mp3")&&(info.known&XZ_SHELL_KNOWN(XZ_SHELL_I_FILE_NAME)));
 assert(xz_shlib_info(L,404,XZ_SHELL_SRC_USB2,&info)&&!(info.links&XZ_SHELL_KNOWN(XZ_SHELL_I_ALBUM))&&!(info.known&XZ_SHELL_KNOWN(XZ_SHELL_I_KEY)));
 assert(!xz_shlib_info(L,1,XZ_SHELL_SRC_USB1,&info)&&!info.known);
 /* A too-small arena fails cleanly rather than truncating. */
 struct xz_shlib *tiny=NULL;assert(xz_shlib_open(path,4096,&tiny)&&!tiny);
 xz_shlib_free(L);
 puts("PASS library: tracks, TRACK/ARTIST/BPM/PLAYLIST/HISTORY/SEARCH lists, windows, on-deck, sorting, INFO records, failures");
}

int main(int argc, char **argv) {
 assert(argc==2);
 fx_track_build(&track,1);
 key_table();verdicts();contacts();touches();pushes();hand_off();commands();library(argv[1]);
 puts("PASS shell runtime");
 return 0;
}
