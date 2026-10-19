#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* Stock-parity workflows from the manual map (docs/research/2026-10-19-player-ui-manual-map.md):
   TAG LIST and HISTORY edits, the CUE/LOOP LIST, grid commands, linked players, deck conditions,
   FX targets, and MY SETTINGS on linked media. */
#include "browse_fixtures.h"
#include "settings_fixtures.h"
#include "shell_info_fixtures.h"
#include "shell_info.h"
#include "shell_source.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],other[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static struct xz_shell_browse_model b;

static size_t send(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, int delta) {
 struct xz_shell_event e={kind,x,y,delta,0,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static size_t key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int d) { return send(u,m,XZ_SHELL_ENCODER_TURN,0,0,d); }
static size_t push(struct xz_shell *u, const struct xz_shell_model *m) { return send(u,m,XZ_SHELL_ENCODER_PUSH,0,0,0); }
static size_t tap(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r) {
 assert(r.w>0&&r.h>0);
 size_t n=send(u,m,XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0);
 struct xz_shell_action keep[XZ_SHELL_ACTIONS];memcpy(keep,out,sizeof keep);
 send(u,m,XZ_SHELL_TOUCH_UP,r.x+r.w/2,r.y+r.h/2,0);
 memcpy(out,keep,sizeof keep);
 return n;
}
static void render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *f) { assert(xz_shell_render(u,m,f,XZ_SHELL_WIDTH)); }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static int count_color(const uint16_t *f, struct xz_shell_rect r, uint32_t color) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=f[y*XZ_SHELL_WIDTH+x]==rgb565(color);
 return n;
}
static void model(struct xz_shell_model *m) {
 fx_model_full(m,&track);
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_TAG_EDIT
  |XZ_SHELL_CAP_HISTORY_EDIT|XZ_SHELL_CAP_CUE_LIST|XZ_SHELL_CAP_GRID|XZ_SHELL_CAP_GRID_EDIT|XZ_SHELL_CAP_TAG_TRACK;
 bx_build(&lib);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 m->browse=&b;
}
static void open_list(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, enum xz_shell_screen s) {
 xz_shell_init(u);xz_shell_browse_reset();
 assert(key(u,m,k)==0&&xz_shell_screen(u)==s);
 if(s==XZ_SHELL_BROWSE)u->stack[u->depth-1].focus=1;   /* in the list, past the category rail */
}

static void test_tag_list(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 open_list(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 const struct xz_shell_browse_row *r=xz_shell_browse_highlight(&u,&m);
 assert(r&&r->id==1000);
 /* TAG TRACK short adds the highlighted track, or removes it when it is on the list (XZ manual p.86). */
 assert(key(&u,&m,XZ_SHELL_KEY_TAG_TRACK)==1&&out[0].kind==XZ_SHELL_ACT_TAG_LIST&&out[0].id==XZ_SHELL_TAG_ADD&&out[0].value==1000);
 lib.tracks[0].tagged=1;
 assert(key(&u,&m,XZ_SHELL_KEY_TAG_TRACK)==1&&out[0].id==XZ_SHELL_TAG_REMOVE&&out[0].value==1000);
 /* Held, away from the TAG LIST, it asks nothing. */
 assert(key(&u,&m,XZ_SHELL_KEY_TAG_TRACK_HOLD)==0);
 /* The membership mark: the tagged row carries an amber check. */
 struct xz_shell_rect row=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,1);
 lib.tracks[1].tagged=0;render(&u,&m,other);
 lib.tracks[1].tagged=1;render(&u,&m,pixels);
 assert(count_color(pixels,row,SH_KEY_AMBER)>count_color(other,row,SH_KEY_AMBER));
 lib.tracks[1].tagged=0;
 /* MENU: the track's tag row, then on a folder ADD ALL TO TAG LIST. */
 lib.tracks[0].tagged=0;
 key(&u,&m,XZ_SHELL_KEY_MENU);assert(u.panel.kind==XZ_SHELL_PANEL_MENU);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_TAG_LIST&&out[0].id==XZ_SHELL_TAG_ADD&&out[0].value==1000);
 render(&u,&m,pixels);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_TAG_LIST&&out[0].id==XZ_SHELL_TAG_ADD_LIST&&out[0].value==1);
 assert(u.panel.kind==XZ_SHELL_PANEL_NONE);
 /* On the TAG LIST: held TAG removes; MENU offers CREATE PLAYLIST and REMOVE ALL TRACKS (XZ manual pp.88-89). */
 bx_tracks(&b,&lib,XZ_SHELL_TAG_LIST);b.total=12;b.window_count=12;
 open_list(&u,&m,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_TAG_LIST);
 assert(key(&u,&m,XZ_SHELL_KEY_TAG_TRACK_HOLD)==1&&out[0].id==XZ_SHELL_TAG_REMOVE&&out[0].value==1000);
 assert(key(&u,&m,XZ_SHELL_KEY_TAG_TRACK)==1&&out[0].id==XZ_SHELL_TAG_REMOVE);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_TAG_REMOVE);   /* REMOVE FROM TAG LIST first */
 /* to the TAG LIST group: CREATE PLAYLIST names it, SAVE asks for the playlist */
 for(int i=0;i<8;i++){turn(&u,&m,1);}
 turn(&u,&m,-8);
 int found=0;
 for(int i=0;i<12&&!found;i++){
  struct xz_shell probe=u;
  size_t n=push(&probe,&m);
  if(n==0&&probe.panel.page!=0&&probe.panel.kind==XZ_SHELL_PANEL_MENU){u=probe;found=1;break;}
  turn(&u,&m,1);
 }
 assert(found&&!strncmp(u.name,"TAG LIST",8));
 render(&u,&m,pixels);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_PLAYLIST&&out[0].id==XZ_SHELL_PL_FROM_TAG_LIST&&!strcmp(u.edit.name,u.name));
 /* REMOVE ALL TRACKS: the second push clears; BACK in between disarms. */
 key(&u,&m,XZ_SHELL_KEY_MENU);if(u.panel.kind!=XZ_SHELL_PANEL_MENU)key(&u,&m,XZ_SHELL_KEY_MENU);
 turn(&u,&m,-20);
 for(int i=0;i<12;i++){struct xz_shell probe=u;push(&probe,&m);if(probe.panel.armed)break;turn(&u,&m,1);}
 assert(push(&u,&m)==0&&u.panel.armed);
 render(&u,&m,pixels);
 key(&u,&m,XZ_SHELL_KEY_BACK);assert(!u.panel.armed&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 push(&u,&m);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_TAG_LIST&&out[0].id==XZ_SHELL_TAG_CLEAR);
 /* Without the capability: TAG TRACK keeps the runtime's TAG_TRACK, MENU has no tag rows. */
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_TAG_EDIT;
 open_list(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 assert(key(&u,&m,XZ_SHELL_KEY_TAG_TRACK)==1&&out[0].kind==XZ_SHELL_ACT_TAG_TRACK);
 puts("PASS TAG LIST: key add/remove, held remove, membership mark, bulk add, to playlist, armed clear");
}

static void test_history(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 /* The HISTORY category's top level: history lists as folder rows. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);b.category=XZ_SHELL_CAT_HISTORY;
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_TAG_EDIT;
 open_list(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==0&&u.panel.armed);
 render(&u,&m,pixels);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_HISTORY&&out[0].id==XZ_SHELL_HISTORY_DELETE&&out[0].value==1);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 turn(&u,&m,1);
 assert(push(&u,&m)==0&&u.panel.armed);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_HISTORY_DELETE_ALL);
 /* Inside a history: MARK PLAYED / UNPLAYED on a track (track 1 is played in the fixture). */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_HISTORY_PLAYED&&out[0].value==1000);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 turn(&u,&m,1);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_HISTORY_UNPLAYED&&out[0].value==1001);
 puts("PASS HISTORY: armed delete one and all, played and unplayed marks");
}

static const struct xz_shell_cue_entry cues[]={
 {0,32000,0,0,"INTRO"},{1,64000,72000,0x28e028,"DROP LOOP"},{0,180000,0,0xff3c3c,NULL},
};
static void test_cue_list(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 m.capabilities&=~(uint32_t)(XZ_SHELL_CAP_TAG_EDIT|XZ_SHELL_CAP_HISTORY_EDIT);
 b.cue_track=1000;b.cues=cues;b.cue_count=3;
 open_list(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==0&&u.panel.page!=0);       /* CUE/LOOP LIST opens */
 render(&u,&m,pixels);
 /* The destination row first; the points follow; push loads at the point (XZ manual pp.81-82). */
 assert(push(&u,&m)==0);                         /* destination: deck 2 */
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_LOAD_CUE&&out[0].deck==1&&out[0].id==1000&&out[0].value==0);
 turn(&u,&m,2);
 assert(push(&u,&m)==1&&out[0].value==2);
 key(&u,&m,XZ_SHELL_KEY_BACK);assert(u.panel.kind==XZ_SHELL_PANEL_MENU&&u.panel.page==0);
 /* Cues of another track: no CUE/LOOP LIST row. */
 b.cue_track=999;key(&u,&m,XZ_SHELL_KEY_MENU);key(&u,&m,XZ_SHELL_KEY_MENU);
 size_t n=push(&u,&m);assert(!(n==0&&u.panel.page!=0));
 puts("PASS CUE/LOOP LIST: destination deck, load at a memory point, only for the highlighted track");
}

static void test_play_states(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 struct xz_shell_rect lane0=xz_shell_play_rect(XZ_SHELL_R_LANE,0);
 /* ON AIR */
 render(&u,&m,other);
 m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ON_AIR);m.deck[0].on_air=1;
 render(&u,&m,pixels);
 struct xz_shell_rect corner={lane0.x+lane0.w-80,lane0.y,80,22};
 assert(count_color(pixels,corner,SH_RED)>count_color(other,corner,SH_RED)+100);
 /* Loop point under the jog: the time box in loop orange. */
 m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP_EDIT);m.deck[0].loop_edit=(struct xz_shell_loop_edit){1,m.deck[0].elapsed_ms+500};
 render(&u,&m,other);
 assert(count_color(other,lane0,SH_LOOP)>count_color(pixels,lane0,SH_LOOP));
 /* Hot cues waiting for CALL draw as outlines. */
 struct xz_shell_rect pads=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,0);
 render(&u,&m,pixels);
 m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_HOT_CUE_CALL);m.deck[0].hot_cue_called=0;
 render(&u,&m,other);
 assert(memcmp(pixels+pads.y*XZ_SHELL_WIDTH,other+pads.y*XZ_SHELL_WIDTH,sizeof(uint16_t)*XZ_SHELL_WIDTH*pads.h)!=0);
 /* HOT CUE AUTO LOAD: three values on the chip; ON lights red, LIBRARY white (XZ manual p.55). */
 m.settings.writable|=1u<<XZ_SHELL_SET_HOT_CUE_AUTO_LOAD;
 m.settings.hot_cue_auto_load=XZ_SHELL_HCAL_ON;
 struct xz_shell_rect top=xz_shell_play_rect(XZ_SHELL_R_TOP_BAR,0);
 int chip_x=-1;
 for(int x=0;x<300&&chip_x<0;x+=2){
  struct xz_shell probe=u;
  if(send(&probe,&m,XZ_SHELL_TOUCH_DOWN,x,10,0)==1&&out[0].id==XZ_SHELL_SET_HOT_CUE_AUTO_LOAD)chip_x=x;
 }
 assert(chip_x>=0&&out[0].value==XZ_SHELL_HCAL_LIBRARY);
 m.settings.hot_cue_auto_load=XZ_SHELL_HCAL_LIBRARY;
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,chip_x,10,0)==1&&out[0].value==XZ_SHELL_HCAL_OFF);
 send(&u,&m,XZ_SHELL_TOUCH_UP,chip_x,10,0);
 render(&u,&m,pixels);
 int library_red=count_color(pixels,top,SH_RED);
 m.settings.hot_cue_auto_load=XZ_SHELL_HCAL_ON;render(&u,&m,other);
 assert(count_color(other,top,SH_RED)>library_red);
 /* NEEDLE LOCK: a playing deck refuses the overview search and says why; paused, it seeks. */
 m.settings.needle_lock_known=1;m.settings.needle_lock=1;
 struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,0);
 assert(sh_needle_locked(&m,&m.deck[0]));
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,ov.x+ov.w/2,ov.y+ov.h/2,0)==0&&u.notice);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,ov.x+ov.w/3,ov.y+ov.h/2,0)==0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 render(&u,&m,pixels);
 m.deck[0].playing=0;
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,ov.x+ov.w/2,ov.y+ov.h/2,0)==1&&out[0].kind==XZ_SHELL_ACT_SEEK);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 m.deck[0].playing=1;
 /* Grid mode: RESET, SNAP, SHIFT on the active deck's lane. */
 send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);assert(u.grid_mode);
 render(&u,&m,pixels);
 for(int i=0;i<3;i++){
  struct xz_shell_rect r=xz_shell_play_rect((enum xz_shell_play_region)(XZ_SHELL_R_GRID_RESET+i),u.deck);
  assert(r.w>=44&&r.h>=44&&r.y>=lane0.y&&r.y+r.h<=lane0.y+lane0.h);
  assert(tap(&u,&m,r)==1&&out[0].kind==XZ_SHELL_ACT_GRID_EDIT&&out[0].id==i&&out[0].deck==u.deck);
 }
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_GRID_EDIT;
 struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_GRID_RESET,u.deck);
 size_t n=tap(&u,&m,r);
 for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_GRID_EDIT);
 send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 /* A deck condition stays until cleared, with one button into BROWSE (XZ manual pp.50, 131). */
 m.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);
 m.deck[1].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302",NULL};
 m.clock_ms+=60000;
 render(&u,&m,pixels);
 struct xz_shell_rect lane1=xz_shell_play_rect(XZ_SHELL_R_LANE,1);
 assert(count_color(pixels,lane1,SH_RED)>100);
 struct xz_shell_rect btn=xz_shell_play_rect(XZ_SHELL_R_ALERT_BUTTON,1);
 assert(btn.w>=44&&btn.h>=44);
 tap(&u,&m,btn);
 assert(u.deck==1&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 m.deck[1].alert.kind=(enum xz_shell_alert_kind)17;xz_shell_init(&u);render(&u,&m,pixels);   /* unknown kind: no banner */
 puts("PASS PLAY states: ON AIR, loop point time, hot cues awaiting CALL, three-state auto load, needle lock, grid commands, deck alert");
}

static void test_fx(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 struct xz_shell_rect fx=xz_shell_play_rect(XZ_SHELL_R_FX,0);
 m.fx.known|=1u<<XZ_SHELL_FX_F_CHANNEL;
 for(int ch=0;ch<=XZ_SHELL_FX_CH_XF_B+3;ch++){m.fx.channel=(enum xz_shell_fx_channel)ch;render(&u,&m,pixels);}
 m.fx.channel=XZ_SHELL_FX_CH_XF_A;render(&u,&m,other);
 m.fx.channel=XZ_SHELL_FX_CH_MASTER;render(&u,&m,pixels);
 assert(memcmp(pixels+fx.y*XZ_SHELL_WIDTH,other+fx.y*XZ_SHELL_WIDTH,sizeof(uint16_t)*XZ_SHELL_WIDTH*fx.h)!=0);
 m.fx.known|=1u<<XZ_SHELL_FX_F_QUANTIZE;m.fx.quantize=1;render(&u,&m,other);
 assert(memcmp(pixels+fx.y*XZ_SHELL_WIDTH,other+fx.y*XZ_SHELL_WIDTH,sizeof(uint16_t)*XZ_SHELL_WIDTH*fx.h)!=0);
 puts("PASS FX: every mixer target named, unknown targets blank, FX QUANTIZE lamp");
}

static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];
static void test_linked_info(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 struct xz_shell_info_model im;fxi_artwork(art);fxi_info_full(&im,art);
 im.linked_count=2;
 im.linked[0].player=3;im.linked[0].track=im.deck[0];im.linked[0].track.rating_writable=1;
 im.linked[1].player=4;im.linked[1].track=im.deck[1];im.linked[1].track.loaded=0;
 m.info=&im;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_INFO);
 assert(xz_shell_screen(&u)==XZ_SHELL_INFO);
 int tabs=2+im.has_highlight+2;
 for(int i=0;i<tabs;i++)render(&u,&m,pixels);
 /* PLAYER 3 is the tab after LIST (or after DECK 2): read only, never the active deck. */
 int deck=u.deck;
 tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,tabs-2));
 render(&u,&m,pixels);
 assert(turn(&u,&m,3)==0&&push(&u,&m)==0&&u.deck==deck);
 tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,tabs-1));
 render(&u,&m,other);
 assert(memcmp(pixels,other,sizeof pixels)!=0);
 im.linked_count=9;render(&u,&m,pixels);   /* a count past the array stays inside it */
 puts("PASS linked players' INFO: PLAYER tabs after LIST, read only, never the active deck");
}

static const struct xz_shell_link_media media[]={
 {3,XZ_SHELL_LINK_USB,"PARTY STICK",0xff8c1a,1},{3,XZ_SHELL_LINK_SD,NULL,0,0},{4,XZ_SHELL_LINK_USB,"BACKUP",0x3c64ff,1},
};
static void test_link_picker(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 struct xz_shell_source_model sm;fxi_source_full(&sm);
 sm.device[2].status=XZ_SHELL_DEVICE_READY;
 sm.link_media=media;sm.link_media_count=3;sm.link_current=-1;
 m.source=&sm;m.capabilities|=XZ_SHELL_CAP_SOURCE;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SOURCE);
 assert(xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 /* The LINK tile opens the picker; the encoder walks the media. */
 struct xz_shell_rect link=xz_shell_source_rect(2);
 tap(&u,&m,link);
 assert(tap(&u,&m,link)==0&&u.stack[u.depth-1].scroll==1);
 render(&u,&m,pixels);
 turn(&u,&m,1);
 assert(push(&u,&m)==0);                         /* not ready yet: nothing */
 turn(&u,&m,1);
 size_t n=push(&u,&m);
 assert(n>=1&&out[0].kind==XZ_SHELL_ACT_SET_SOURCE&&out[0].id==XZ_SHELL_SRC_LINK&&out[0].value==3);
 /* BACK shuts the picker before it leaves SOURCE. */
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SOURCE);
 tap(&u,&m,link);tap(&u,&m,link);
 assert(u.stack[u.depth-1].scroll>0);
 key(&u,&m,XZ_SHELL_KEY_BACK);
 assert(xz_shell_screen(&u)==XZ_SHELL_SOURCE&&u.stack[u.depth-1].scroll==0);
 /* Touch: first highlights, second picks. */
 tap(&u,&m,link);tap(&u,&m,link);
 struct xz_shell_rect row=xz_shell_source_link_rect(2);
 assert(tap(&u,&m,row)==0);
 assert(tap(&u,&m,row)>=1&&out[0].value==3);
 /* No media listed: LINK stays a plain device. */
 sm.link_media_count=0;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SOURCE);
 tap(&u,&m,link);
 assert(tap(&u,&m,link)>=1&&out[0].kind==XZ_SHELL_ACT_SET_SOURCE&&out[0].id==XZ_SHELL_SRC_LINK&&out[0].value==0);
 puts("PASS LINK picker: player and medium list, ready ones open, BACK shuts it, plain LINK without media");
}

static void test_my_settings_link(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 struct xz_shell_source_model sm;fxi_source_full(&sm);m.source=&sm;m.capabilities|=XZ_SHELL_CAP_SOURCE;
 m.my_settings=fxm_my_reset();
 fxm_my.source=XZ_SHELL_SRC_LINK;fxm_my.link_player=3;fxm_my.source_name=NULL;
 xz_shell_init(&u);xz_shell_browse_reset();key(&u,&m,XZ_SHELL_KEY_SOURCE_USB1);
 assert(sh_browse_at_root(&u));
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MY_SETTINGS);
 /* A playing deck: LOAD waits for PAUSE and asks nothing. */
 m.deck[0].playing=1;
 render(&u,&m,pixels);
 assert(push(&u,&m)==0);
 /* Paused: LOAD asks; then APPLYING greys it again; DONE shows the result line. */
 m.deck[0].playing=0;
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_MY_SETTINGS&&out[0].value==XZ_SHELL_SRC_LINK);
 fxm_my.state=XZ_SHELL_MY_APPLYING;
 assert(push(&u,&m)==0);
 fxm_my.state=XZ_SHELL_MY_NEEDS_PAUSE;assert(push(&u,&m)==0);
 fxm_my.state=XZ_SHELL_MY_DONE;fxm_my.result="31 APPLIED, 7 NOT ON THIS DECK";
 render(&u,&m,other);
 assert(memcmp(pixels,other,sizeof pixels)!=0);
 fxm_my.state=XZ_SHELL_MY_FAILED;fxm_my.result=NULL;render(&u,&m,pixels);
 /* A LINK model without its player number keeps the list MENU. */
 fxm_my.link_player=0;key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 puts("PASS MY SETTINGS from a linked player: pause first, applying, result line");
}

int main(void) {
 fx_track_build(&track,7);
 (void)fxs_item;(void)fxs_reset;   /* shared fixtures this test does not need */
 test_tag_list();
 test_history();
 test_cue_list();
 test_play_states();
 test_fx();
 test_linked_info();
 test_link_picker();
 test_my_settings_link();
 puts("PASS parity");
 return 0;
}
