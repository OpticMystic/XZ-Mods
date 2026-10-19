#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The universal BACK chain, the USB buttons, the browse panels (INFO, MENU,
   FILTER), playlist editing, stem mute buttons, the absent-source list and
   the key traffic light. */
#include "browse_fixtures.h"
#include "shell_source.h"
#include "shell_settings.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;
static struct bx_library lib;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static uint16_t *frame(uint16_t *buffer) { return buffer+GUARD+STRIDE; }
static void render_checked(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *buffer) {
 for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a;
 assert(xz_shell_render(u,m,frame(buffer),STRIDE));
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(frame(buffer)[y*STRIDE+x]==0xa55a);
}
static uint16_t at(const uint16_t *f, int x, int y) { return f[(size_t)y*STRIDE+(size_t)x]; }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static int count(const uint16_t *f, struct xz_shell_rect r, uint32_t color) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=at(f,x,y)==rgb565(color);
 return n;
}
static size_t send(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, int delta) {
 struct xz_shell_event e={kind,x,y,delta,0,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static size_t press(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
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
static int is_op(const struct xz_shell_action *a, enum xz_shell_browse_op o) { return a->kind==XZ_SHELL_ACT_BROWSE&&a->id==(int)o; }

static struct xz_shell_source_model sources;
static void two_sticks(int usb2_present) {
 memset(&sources,0,sizeof sources);
 for(int i=0;i<2;i++){
  sources.device[i].known=1u<<XZ_SHELL_D_STATUS;
  sources.device[i].status=i==0||usb2_present?XZ_SHELL_DEVICE_READY:XZ_SHELL_DEVICE_NOT_CONNECTED;
 }
 sources.current_known=1;sources.current=XZ_SHELL_SRC_USB1;
}
static void model(struct xz_shell_model *m, struct xz_shell_browse_model *b) {
 fx_model_full(m,&track);
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SOURCE|
  XZ_SHELL_CAP_PLAYLIST_EDIT|XZ_SHELL_CAP_FILTER|XZ_SHELL_CAP_STEM_MUTE;
 bx_tracks(b,&lib,XZ_SHELL_BROWSE);bx_panels(b,&lib);
 m->browse=b;two_sticks(1);m->source=&sources;
}

/* Playlist editing: every op goes out as XZ_SHELL_ACT_PLAYLIST with xz_shell.edit filled. */
static size_t run_to(struct xz_shell *u, const struct xz_shell_model *m, const char *label) {
 /* Walk the panel rows until the cursor sits on label, then push it. */
 for(int i=0;i<80;i++){
  render_checked(u,m,buffer_a);
  struct xz_shell u2=*u;(void)u2;
  if(i)turn(u,m,1);
  /* the panel's own rows are not exposed: probe by pushing a copy */
  struct xz_shell probe=*u;struct xz_shell_action keep[XZ_SHELL_ACTIONS];
  size_t n=send(&probe,m,XZ_SHELL_ENCODER_PUSH,0,0,0);
  memcpy(keep,out,sizeof keep);
  int hit=0;
  if(!strcmp(label,"NEW")&&probe.panel.page==2)hit=1;
  if(!strcmp(label,"RENAME")&&probe.panel.page==3)hit=1;
  if(!strcmp(label,"ADD")&&probe.panel.page==1)hit=1;
  if(!strcmp(label,"DELETE")&&probe.panel.armed&&probe.panel.page==0&&n==0&&u->panel.kind==XZ_SHELL_PANEL_MENU&&!probe.grab.active)hit=1;
  if(!strcmp(label,"MOVE")&&probe.grab.active)hit=1;
  if(!strcmp(label,"REMOVE")&&n==1&&keep[0].kind==XZ_SHELL_ACT_PLAYLIST&&keep[0].id==XZ_SHELL_PL_REMOVE_TRACK)hit=1;
  if(hit)return send(u,m,XZ_SHELL_ENCODER_PUSH,0,0,0);
 }
 assert(!"panel row not found");
 return 0;
}
/* BACK climbs one step at a time from a sub-page of a panel over a nested list all the way to PLAY. */
static void test_back_chain(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);xz_shell_browse_reset();
 /* A USB button: that stick's browser at its top, over the picker. */
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE_USB1)==2&&out[0].kind==XZ_SHELL_ACT_SET_SOURCE&&out[0].id==XZ_SHELL_SRC_USB1&&is_op(&out[1],XZ_SHELL_BROWSE_TOP));
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.depth==3&&u.stack[1].screen==XZ_SHELL_SOURCE);
 /* The runtime opened two levels: a playlist folder, then its tracks. */
 b.depth=2;b.edit=XZ_SHELL_EDIT_TRACKS;b.edit_playlist=3;
 assert(press(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 /* ADD TO PLAYLIST is a sub-page of MENU. */
 run_to(&u,&m,"ADD");
 assert(u.panel.page==1);
 render_checked(&u,&m,buffer_a);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU&&u.panel.page==0);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==1&&is_op(&out[0],XZ_SHELL_BROWSE_UP));b.depth=1;
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==1&&is_op(&out[0],XZ_SHELL_BROWSE_UP));b.depth=0;
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE&&u.depth==2);
 render_checked(&u,&m,buffer_a);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY&&u.depth==1);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==1);
 /* The on-screen back arrow is the same BACK. */
 press(&u,&m,XZ_SHELL_KEY_BROWSE);b.depth=1;
 press(&u,&m,XZ_SHELL_KEY_INFO);assert(u.panel.kind==XZ_SHELL_PANEL_INFO);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_BACK,0))==0&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_BACK,0))==1&&is_op(&out[0],XZ_SHELL_BROWSE_UP));
 b.depth=0;
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_BACK,0))==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 /* The same rule on every other screen: SHORTCUT, UTILITY panes, INFO, TAG LIST, SEARCH. */
 xz_shell_init(&u);
 static struct xz_shell_settings_model empty;m.settings_screen=&empty;
 static struct xz_shell_info_model *none;(void)none;
 press(&u,&m,XZ_SHELL_KEY_SHORTCUT);press(&u,&m,XZ_SHELL_KEY_MENU_HOLD);
 assert(xz_shell_screen(&u)==XZ_SHELL_UTILITY);
 send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);                 /* rail -> items */
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY);   /* pane left */
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_SHORTCUT);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 static const enum xz_shell_key flat[]={XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_SEARCH};
 for(size_t i=0;i<2;i++){
  xz_shell_init(&u);b.depth=0;b.screen=i?XZ_SHELL_SEARCH:XZ_SHELL_TAG_LIST;
  press(&u,&m,flat[i]);press(&u,&m,XZ_SHELL_KEY_MENU);
  assert(u.panel.kind==XZ_SHELL_PANEL_MENU);
  assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE&&u.depth==2);
  assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==1);
 }
 puts("PASS universal BACK: panel sub-page, panel, list levels, source picker, PLAY; same on every screen");
}

/* The USB buttons: a missing stick opens the picker with its slot pinned and named. */
static void test_stick_keys(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);two_sticks(0);
 xz_shell_init(&u);
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE_USB2)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE&&u.stack[1].cursor==XZ_SHELL_SRC_USB2);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect tile=xz_shell_source_rect(1);
 assert(count(frame(buffer_a),(struct xz_shell_rect){tile.x,tile.y+136,tile.w,44},SH_BLUE)>500);   /* its name bar is the highlighted one */
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* A ready stick: straight into its browser. */
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE_USB1)==2&&out[0].id==XZ_SHELL_SRC_USB1&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 /* Without a source slot the stock screen takes over. */
 struct xz_shell_model bare=m;bare.source=NULL;
 xz_shell_init(&u);
 assert(press(&u,&bare,XZ_SHELL_KEY_SOURCE_USB1)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SOURCE&&u.depth==1);
 /* The list of a stick known to be gone shows no stale rows, one line and the way to the picker. */
 two_sticks(1);sources.current=XZ_SHELL_SRC_USB2;sources.device[1].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect row0=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,0);
 assert(count(frame(buffer_a),row0,SH_BLUE)==0);                 /* no highlighted stale row */
 assert(turn(&u,&m,3)==0&&push(&u,&m)==0);                        /* nothing to move or load */
 assert(tap(&u,&m,(struct xz_shell_rect){row0.x+row0.w/2-100,80+38*3,200,44})==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 puts("PASS USB buttons: ready stick opens its browser, missing stick pins the picker slot, absent list shows no rows");
}

/* INFO and MENU are keys the runtime probes on a copy of the shell struct: the state they change must
   live in the struct, so applying a key to a copy and to the original ends identical. */
static void test_panel_keys_are_probe_safe(void) {
 struct xz_shell u,copy;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);
 static const enum xz_shell_key keys[]={XZ_SHELL_KEY_INFO,XZ_SHELL_KEY_MENU,XZ_SHELL_KEY_INFO,XZ_SHELL_KEY_MENU,XZ_SHELL_KEY_MENU};
 for(size_t i=0;i<sizeof keys/sizeof *keys;i++){
  copy=u;press(&copy,&m,keys[i]);   /* the probe */
  press(&u,&m,keys[i]);              /* the real key */
  assert(!memcmp(&copy,&u,sizeof u));
 }
 /* The INFO panel follows the cursor. */
 press(&u,&m,XZ_SHELL_KEY_INFO);assert(u.panel.kind==XZ_SHELL_PANEL_INFO);
 render_checked(&u,&m,buffer_a);
 turn(&u,&m,5);
 render_checked(&u,&m,buffer_b);
 struct xz_shell_rect pane=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO_PANE,0);
 assert(pane.w>0);
 int differs=0;
 for(int y=pane.y;y<pane.y+60;y++)for(int x=pane.x;x<pane.x+pane.w;x++)differs|=at(frame(buffer_a),x,y)!=at(frame(buffer_b),x,y);
 assert(differs);
 puts("PASS INFO and MENU keys keep their state in the shell struct; the INFO panel follows the cursor");
}

static int find_row(struct xz_shell *u, const struct xz_shell_model *m, int max) {
 (void)u;(void)m;return max;
}
/* MENU: SORT and COLUMNS go out as requests; the list is the runtime's to sort. */
static void test_menu_panel(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);
 press(&u,&m,XZ_SHELL_KEY_MENU);
 render_checked(&u,&m,buffer_a);
 /* The first row is SORT BY TRACK No.; the current sort (TITLE) flips direction when chosen again. */
 assert(push(&u,&m)==1&&is_op(&out[0],XZ_SHELL_BROWSE_SORT)&&out[0].value==XZ_SHELL_COL_NUMBER);
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&is_op(&out[0],XZ_SHELL_BROWSE_SORT)&&out[0].value==(XZ_SHELL_COL_TITLE|1<<8));
 /* COLUMNS: past the eleven sort keys and the header; NUMBER is shown, so its row hides it. */
 turn(&u,&m,10);
 assert(push(&u,&m)==1&&is_op(&out[0],XZ_SHELL_BROWSE_SET_COLUMNS));
 uint32_t mask=(uint32_t)out[0].value;
 assert((mask&1u<<XZ_SHELL_COL_TITLE)&&!(mask&1u<<XZ_SHELL_COL_NUMBER)&&(mask&1u<<XZ_SHELL_COL_BPM)&&(mask&1u<<XZ_SHELL_COL_KEY));
 /* Touch: a row acts at once. */
 struct xz_shell_rect row=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_PANEL_ROW,3);
 assert(row.w>0&&tap(&u,&m,row)==1&&out[0].kind==XZ_SHELL_ACT_BROWSE);
 /* The encoder belongs to the panel while it is open; MENU closes it and gives it back. */
 int cursor=u.stack[u.depth-1].cursor;
 turn(&u,&m,2);assert(u.stack[u.depth-1].cursor==cursor);
 press(&u,&m,XZ_SHELL_KEY_MENU);assert(u.panel.kind==XZ_SHELL_PANEL_NONE);
 turn(&u,&m,2);assert(u.stack[u.depth-1].cursor==cursor+2);
 /* A list with nothing to sort or edit offers only what it has; with nothing at all, it says so. */
 b.known&=~((1u<<XZ_SHELL_BF_SORT_KEYS)|(1u<<XZ_SHELL_BF_COLUMN_CHOICES));
 m.capabilities&=~(uint32_t)(XZ_SHELL_CAP_PLAYLIST_EDIT|XZ_SHELL_CAP_FILTER);
 press(&u,&m,XZ_SHELL_KEY_MENU);render_checked(&u,&m,buffer_a);
 assert(push(&u,&m)==0&&turn(&u,&m,3)==0);
 (void)find_row;
 puts("PASS MENU panel: sort requests and direction, column mask, touch rows, encoder hand-over");
}

static void test_playlist_editing(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 /* The playlist list: NEW, RENAME, DELETE. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);b.known|=1u<<XZ_SHELL_BF_EDIT;b.edit=XZ_SHELL_EDIT_PLAYLISTS;
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);turn(&u,&m,2);
 press(&u,&m,XZ_SHELL_KEY_MENU);
 run_to(&u,&m,"NEW");
 assert(u.panel.page==2&&!strcmp(u.name,"PLAYLIST 10"));
 render_checked(&u,&m,buffer_a);
 /* Type on the keyboard, then SAVE. */
 struct xz_shell_rect clear={0,0,0,0},save={0,0,0,0},x_key={0,0,0,0};
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++){
  if(!strcmp(xz_shell_browse_key_label(i),"CLEAR"))clear=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,i);
  if(!strcmp(xz_shell_browse_key_label(i),"SEARCH"))save=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,i);
  if(!strcmp(xz_shell_browse_key_label(i),"X"))x_key=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,i);
 }
 tap(&u,&m,clear);tap(&u,&m,x_key);tap(&u,&m,x_key);
 assert(!strcmp(u.name,"XX"));
 assert(tap(&u,&m,save)==1&&out[0].kind==XZ_SHELL_ACT_PLAYLIST&&out[0].id==XZ_SHELL_PL_CREATE&&!strcmp(u.edit.name,"XX"));
 assert(u.panel.kind==XZ_SHELL_PANEL_NONE);
 /* RENAME starts from the playlist's name; push saves it. */
 press(&u,&m,XZ_SHELL_KEY_MENU);run_to(&u,&m,"RENAME");
 assert(!strcmp(u.name,lib.folders[2].title));
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_PL_RENAME&&out[0].value==(int32_t)lib.folders[2].id&&u.edit.playlist==lib.folders[2].id);
 /* DELETE asks twice; BACK in between disarms it. */
 press(&u,&m,XZ_SHELL_KEY_MENU);
 run_to(&u,&m,"DELETE");
 assert(u.panel.armed);
 render_checked(&u,&m,buffer_a);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&!u.panel.armed&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 run_to(&u,&m,"DELETE");
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_PL_DELETE&&u.edit.playlist==lib.folders[2].id);
 /* A playlist's tracks: ADD, REMOVE and MOVE. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);b.edit=XZ_SHELL_EDIT_TRACKS;b.edit_playlist=7;b.depth=1;
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);turn(&u,&m,4);
 press(&u,&m,XZ_SHELL_KEY_MENU);run_to(&u,&m,"ADD");
 assert(u.panel.page==1);
 turn(&u,&m,2);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_PL_ADD_TRACK&&u.edit.playlist==bx_refs[2].id&&u.edit.track==lib.tracks[4].id);
 press(&u,&m,XZ_SHELL_KEY_MENU);
 assert(run_to(&u,&m,"REMOVE")==1&&out[0].id==XZ_SHELL_PL_REMOVE_TRACK&&u.edit.playlist==7&&u.edit.from==4&&u.edit.track==lib.tracks[4].id);
 /* MOVE: pick up, turn, drop. BACK puts it back. */
 press(&u,&m,XZ_SHELL_KEY_MENU);run_to(&u,&m,"MOVE");
 assert(u.grab.active&&u.grab.from==4&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 turn(&u,&m,3);assert(u.grab.to==7&&u.stack[u.depth-1].cursor==7);
 render_checked(&u,&m,buffer_a);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==0&&!u.grab.active&&u.stack[u.depth-1].cursor==4);
 press(&u,&m,XZ_SHELL_KEY_MENU);run_to(&u,&m,"MOVE");
 turn(&u,&m,-2);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_PLAYLIST&&out[0].id==XZ_SHELL_PL_MOVE_TRACK&&u.edit.from==4&&u.edit.to==2&&u.edit.track==lib.tracks[4].id&&!u.grab.active);
 /* Touch: a row moves the track there, the same row again drops it. */
 press(&u,&m,XZ_SHELL_KEY_MENU);run_to(&u,&m,"MOVE");
 struct xz_shell_rect r6=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,6);
 assert(tap(&u,&m,r6)==0&&u.grab.to==6);
 assert(tap(&u,&m,r6)==1&&out[0].id==XZ_SHELL_PL_MOVE_TRACK&&u.edit.to==6);
 /* Without the capability there is no playlist section at all. */
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_PLAYLIST_EDIT;
 press(&u,&m,XZ_SHELL_KEY_MENU);
 for(int i=0;i<40;i++){struct xz_shell probe=u;send(&probe,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);assert(!probe.grab.active&&probe.panel.page==0&&out[0].kind!=XZ_SHELL_ACT_PLAYLIST);turn(&u,&m,1);}
 puts("PASS playlist editing: NEW with the keyboard, RENAME, two-step DELETE, ADD, REMOVE, MOVE by encoder and touch");
}

/* FILTER: panel rows build the request; the chip shows it and clears it with one touch. */
static void test_filter(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FILTER_CHIP,0).w==0);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FILTER,0))==0&&u.panel.kind==XZ_SHELL_PANEL_FILTER);
 /* BPM: OFF -> MASTER +-2 -> +-4 ... -> RANGE -> OFF; each step applies. */
 assert(push(&u,&m)==1&&is_op(&out[0],XZ_SHELL_BROWSE_FILTER)&&out[0].value==1&&u.filter.bpm==XZ_SHELL_FILTER_BPM_MASTER&&u.filter.bpm_spread==2);
 push(&u,&m);push(&u,&m);assert(u.filter.bpm_spread==6);
 push(&u,&m);push(&u,&m);assert(u.filter.bpm==XZ_SHELL_FILTER_BPM_RANGE&&u.filter.bpm_min_x100==12400&&u.filter.bpm_max_x100==13400);
 /* RANGE bounds: push arms, turn dials 1 BPM, push disarms; the bounds stay ordered. */
 turn(&u,&m,1);assert(push(&u,&m)==0&&u.panel.armed);
 assert(turn(&u,&m,3)==1&&u.filter.bpm_min_x100==12700);
 turn(&u,&m,20);assert(u.filter.bpm_min_x100==u.filter.bpm_max_x100);
 push(&u,&m);assert(!u.panel.armed);
 /* KEY, RATING, a colour. */
 turn(&u,&m,2);assert(push(&u,&m)==1&&u.filter.key_compatible==1);
 turn(&u,&m,1);push(&u,&m);push(&u,&m);push(&u,&m);assert(u.filter.rating_min==3);
 turn(&u,&m,2);assert(push(&u,&m)==1&&u.filter.colors==1u<<XZ_SHELL_TAG_COLOR_RED);
 render_checked(&u,&m,buffer_a);
 /* The chip appears beside the tiles, amber; one touch clears everything and says so. */
 press(&u,&m,XZ_SHELL_KEY_BACK);assert(u.panel.kind==XZ_SHELL_PANEL_NONE);
 struct xz_shell_rect chip=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FILTER_CHIP,0);
 assert(chip.w>60&&chip.x+chip.w<=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FILTER,0).x);
 render_checked(&u,&m,buffer_a);
 assert(count(frame(buffer_a),chip,SH_KEY_AMBER)>chip.w*chip.h/3);
 assert(tap(&u,&m,chip)==1&&is_op(&out[0],XZ_SHELL_BROWSE_FILTER)&&out[0].value==0);
 struct xz_shell_filter zero;memset(&zero,0,sizeof zero);
 assert(!memcmp(&u.filter,&zero,sizeof zero)&&xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FILTER_CHIP,0).w==0);
 /* MY TAG rows come from the model; without the capability the FILTER tile is gone. */
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_FILTER;
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FILTER,0).w==0);
 render_checked(&u,&m,buffer_a);
 puts("PASS FILTER panel: BPM master/range with dialled bounds, key, rating, colour, chip and one-touch clear");
}

/* Stem mute buttons: only on a deck playing local stems with the capability; a touch asks to flip. */
static void test_stem_buttons(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);
 for(int d=0;d<2;d++)m.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);
 for(int d=0;d<2;d++)for(int s=0;s<3;s++)assert(xz_shell_stem_button(&m,d,s).w==0);   /* mute state not read: no buttons */
 m.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);m.deck[1].stem_muted=2;           /* INST muted */
 for(int s=0;s<3;s++){
  struct xz_shell_rect r=xz_shell_stem_button(&m,1,s);
  assert(r.w>=44&&r.h>=44&&xz_shell_stem_button(&m,0,s).w==0);
 }
 /* DRUMS, VOCAL, INST from the left. */
 assert(xz_shell_stem_button(&m,1,0).x<xz_shell_stem_button(&m,1,2).x&&xz_shell_stem_button(&m,1,2).x<xz_shell_stem_button(&m,1,1).x);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect drums=xz_shell_stem_button(&m,1,0),inst=xz_shell_stem_button(&m,1,1);
 struct xz_shell_rect pad=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,1);
 drums.y=inst.y=pad.y;drums.h=inst.h=pad.h;
 assert(count(frame(buffer_a),drums,0x2a6cf0)>40);            /* lit: full-colour outline */
 assert(count(frame(buffer_a),inst,0xe8302c)==0);             /* muted: dimmed */
 /* Runtime colours win. */
 m.stem_rgb_known=1;m.stem_rgb[0]=0x00ffff;m.stem_rgb[1]=0xff00ff;m.stem_rgb[2]=0xffff00;
 render_checked(&u,&m,buffer_a);assert(count(frame(buffer_a),drums,0x00ffff)>40);
 /* Touch flips that stem. */
 assert(tap(&u,&m,xz_shell_stem_button(&m,1,1))==1&&out[0].kind==XZ_SHELL_ACT_STEM_MUTE&&out[0].deck==1&&out[0].id==1&&out[0].value==0);
 assert(tap(&u,&m,xz_shell_stem_button(&m,1,2))==1&&out[0].id==2&&out[0].value==1);
 /* The padded target does not steal the ZOOM/GRID toggle. */
 struct xz_shell_rect zoom=xz_shell_play_rect(XZ_SHELL_R_ZOOM,0);
 for(int s=0;s<3;s++){struct xz_shell_rect r=xz_shell_stem_button(&m,1,s);assert(r.x+r.w<=zoom.x||r.y>=zoom.y+zoom.h);}
 /* No capability: no buttons, the row shows the hot-cue lamps only. */
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_STEM_MUTE;
 assert(xz_shell_stem_button(&m,1,0).w==0);
 render_checked(&u,&m,buffer_a);
 puts("PASS stem mute buttons: shown only when they work, lit and muted looks, runtime colours, touch flips");
}

/* The key wheel: all 24 keys against all 24 masters. */
static void test_key_wheel(void) {
 static const char *const minor[12]={"Abm","Ebm","Bbm","Fm","Cm","Gm","Dm","Am","Em","Bm","F#m","Dbm"};   /* 1A..12A */
 static const char *const major[12]={"B","F#","Db","Ab","Eb","Bb","F","C","G","D","A","E"};            /* 1B..12B */
 for(int n=0;n<12;n++)for(int ml=0;ml<2;ml++)for(int k=0;k<12;k++)for(int kl=0;kl<2;kl++){
  const char *master=ml?major[n]:minor[n],*key=kl?major[k]:minor[k];
  enum xz_shell_key_match want=XZ_SHELL_KEY_MATCH_NONE;
  if(k==n)want=XZ_SHELL_KEY_MATCH_GREEN;
  else if(kl==ml&&((k+1)%12==n||(n+1)%12==k))want=XZ_SHELL_KEY_MATCH_AMBER;
  assert(xz_shell_key_match(key,master)==want);
  /* The same answer in alphanumeric notation. */
  char a[8],b2[8];
  snprintf(a,sizeof a,"%d%c",k+1,kl?'B':'A');snprintf(b2,sizeof b2,"%d%c",n+1,ml?'B':'A');
  assert(xz_shell_key_match(a,b2)==want&&xz_shell_key_match(a,master)==want);
 }
 assert(xz_shell_key_match(NULL,"Am")==XZ_SHELL_KEY_MATCH_UNKNOWN&&xz_shell_key_match("Am","")==XZ_SHELL_KEY_MATCH_UNKNOWN);
 assert(xz_shell_key_match("G#m","Abm")==XZ_SHELL_KEY_MATCH_GREEN);
 /* The master is the deck flagged master with a known key; rows and the other deck colour against it. */
 struct xz_shell_model m;fx_model_full(&m,&track);
 assert(!strcmp(xz_shell_master_key(&m),"Fm"));
 assert(sh_key_ink(&m,"Cm",0,SH_WHITE)==SH_KEY_AMBER&&sh_key_ink(&m,"Ab",0,SH_WHITE)==SH_KEY_GREEN&&sh_key_ink(&m,"E",1,SH_WHITE)==SH_WHITE);
 m.deck[0].master=0;
 assert(xz_shell_master_key(&m)==NULL&&sh_key_ink(&m,"E",1,SH_WHITE)==SH_GREEN&&sh_key_ink(&m,"Cm",0,SH_WHITE)==SH_WHITE);
 puts("PASS key wheel: 24 x 24 keys, both notations, master lookup, runtime fallback");
}


/* No dead keys: TRACK FILTER, the LINK and computer buttons, TAG TRACK all act or say why. */
static void test_keys_never_dead(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);xz_shell_browse_reset();
 assert(press(&u,&m,XZ_SHELL_KEY_TRACK_FILTER)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.panel.kind==XZ_SHELL_PANEL_FILTER);
 assert(press(&u,&m,XZ_SHELL_KEY_TRACK_FILTER)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 struct xz_shell_model nofilter=m;nofilter.capabilities&=~(uint32_t)XZ_SHELL_CAP_FILTER;
 assert(press(&u,&nofilter,XZ_SHELL_KEY_TRACK_FILTER)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&u.notice&&u.notice_at_ms==m.clock_ms);
 xz_shell_init(&u);
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE_LINK)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE&&u.stack[1].cursor==XZ_SHELL_SRC_LINK);
 xz_shell_init(&u);
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE_COMPUTER)==0&&u.stack[1].cursor==XZ_SHELL_SRC_COMPUTER);
 /* TAG TRACK without the capability changes nothing, so the runtime's probe sees an inert key and routes it
    (stock tags the loaded track from PLAY). With the capability it acts. */
 xz_shell_init(&u);
 struct xz_shell before=u;
 assert(press(&u,&m,XZ_SHELL_KEY_TAG_TRACK)==0&&!memcmp(&before,&u,sizeof u));
 /* A notice the shell raises itself (TRACK FILTER without the capability) shows like the runtime's. */
 press(&u,&nofilter,XZ_SHELL_KEY_TRACK_FILTER);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect toast=sh_notice_rect(u.notice,NULL);
 assert(count(frame(buffer_a),toast,SH_ROW_ALT)>toast.w*toast.h/3);
 m.clock_ms+=XZ_SHELL_NOTICE_MS;render_checked(&u,&m,buffer_b);
 assert(count(frame(buffer_b),toast,SH_ROW_ALT)<toast.w*toast.h/3);
 m.capabilities|=XZ_SHELL_CAP_TAG_TRACK;
 assert(press(&u,&m,XZ_SHELL_KEY_TAG_TRACK)==1&&out[0].kind==XZ_SHELL_ACT_TAG_TRACK);
 puts("PASS no dead keys: TRACK FILTER, LINK and computer buttons act or say why; TAG TRACK left to the runtime's probe");
}
/* SHORTCUT jumps and the item a read-only hand-off asks the stock screen to open. */
static void test_shortcut_jumps(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 static struct xz_shell_settings_model empty;m.settings_screen=&empty;
 b.category_available|=1u<<XZ_SHELL_CAT_MATCHING;
 for(int j=0;j<XZ_SHELL_JUMP_COUNT;j++){
  struct xz_shell_rect r=xz_shell_shortcut_jump(j);
  assert(r.w>=44&&r.h>=44);
  xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_SHORTCUT);
  render_checked(&u,&m,buffer_a);
  size_t n=tap(&u,&m,r);
  switch(j){
  case XZ_SHELL_JUMP_TRACK:assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&n==1&&is_op(&out[0],XZ_SHELL_BROWSE_CATEGORY)&&out[0].value==XZ_SHELL_CAT_TRACK);break;
  case XZ_SHELL_JUMP_PLAYLIST:assert(xz_shell_screen(&u)==XZ_SHELL_PLAYLIST);break;
  case XZ_SHELL_JUMP_SEARCH:assert(xz_shell_screen(&u)==XZ_SHELL_SEARCH);break;
  default:assert(n==1&&is_op(&out[0],XZ_SHELL_BROWSE_CATEGORY)&&out[0].value==XZ_SHELL_CAT_MATCHING);
  }
 }
 /* MATCHING the runtime cannot open: the stock browser, with a reason. */
 b.category_available&=~(1u<<XZ_SHELL_CAT_MATCHING);
 xz_shell_init(&u);press(&u,&m,XZ_SHELL_KEY_SHORTCUT);
 assert(tap(&u,&m,xz_shell_shortcut_jump(XZ_SHELL_JUMP_MATCHING))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&u.notice);
 /* A read-only item hands off at that item: value = 1 + its id. */
 xz_shell_init(&u);press(&u,&m,XZ_SHELL_KEY_SHORTCUT);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SHORTCUT&&out[0].value==XZ_SHELL_ITEM_QUANTIZE_BEAT+1);
 /* MENU on a list offers SEARCH too. */
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);press(&u,&m,XZ_SHELL_KEY_MENU);
 int found=0;
 for(int i=0;i<60&&!found;i++){struct xz_shell probe=u;send(&probe,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);found=xz_shell_screen(&probe)==XZ_SHELL_SEARCH;if(!found)turn(&u,&m,1);}
 assert(found);
 puts("PASS SHORTCUT jumps (TRACK, PLAYLIST, SEARCH, MATCHING), item-level hand-off, SEARCH in MENU");
}
/* Play: artist under the title, SLIP and VINYL readouts, the slip ghost. */
static void test_play_readouts(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);
 struct xz_shell_rect strip=xz_shell_play_rect(XZ_SHELL_R_STRIP,0),title={strip.x+60,strip.y+18,200,11};
 render_checked(&u,&m,buffer_a);
 int with_artist=count(frame(buffer_a),title,SH_DIM);
 m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_ARTIST);
 render_checked(&u,&m,buffer_b);
 assert(with_artist>20&&count(frame(buffer_b),title,SH_DIM)<with_artist);
 m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ARTIST);
 /* SLIP on with a known position: an amber ghost in the lane, ahead of the playhead. */
 m.deck[0].slip=1;m.deck[0].slip_ms=m.deck[0].elapsed_ms+2000;m.deck[0].slip_at_ms=0;
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect lane=xz_shell_play_rect(XZ_SHELL_R_LANE,0);
 assert(count(frame(buffer_a),lane,SH_KEY_AMBER)>30);
 m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_SLIP_POSITION);
 render_checked(&u,&m,buffer_b);
 assert(count(frame(buffer_b),lane,SH_KEY_AMBER)<count(frame(buffer_a),lane,SH_KEY_AMBER));
 m.deck[0].vinyl=1;render_checked(&u,&m,buffer_a);
 puts("PASS play readouts: artist under the title, SLIP ghost only with its position, VINYL");
}
/* Browse rows carry the artist under the title while no ARTIST column shows. */
static void test_two_line_rows(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect row=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3);
 struct xz_shell_rect below={row.x,row.y+24,row.w,10};
 int artist=count(frame(buffer_a),below,SH_DIM);
 b.columns[4]=XZ_SHELL_COL_ARTIST;b.column_count=5;
 render_checked(&u,&m,buffer_b);
 assert(artist>8&&count(frame(buffer_b),below,SH_DIM)<artist);
 puts("PASS browse rows: artist under the title unless the ARTIST column shows");
}
int main(void) {
 fx_track_build(&track,7);
 bx_build(&lib);
 test_back_chain();
 test_stick_keys();
 test_panel_keys_are_probe_safe();
 test_menu_panel();
 test_playlist_editing();
 test_filter();
 test_stem_buttons();
 test_key_wheel();
 test_keys_never_dead();
 test_shortcut_jumps();
 test_play_readouts();
 test_two_line_rows();
 puts("PASS panels, back chain, sticks, playlists, filter, stems, key wheel");
 return 0;
}
