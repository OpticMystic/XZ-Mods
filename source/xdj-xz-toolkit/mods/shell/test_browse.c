#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;
static struct bx_library lib;

static uint16_t *frame(uint16_t *buffer) { return buffer+GUARD+STRIDE; }
static void fill(uint16_t *buffer) { for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a; }
static void check_guards(uint16_t *buffer) {
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(frame(buffer)[y*STRIDE+x]==0xa55a);
}
static uint16_t at(const uint16_t *f, int x, int y) { return f[(size_t)y*STRIDE+(size_t)x]; }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static void render_checked(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *buffer) {
 fill(buffer);
 assert(xz_shell_render(u,m,frame(buffer),STRIDE));
 check_guards(buffer);
}
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static size_t send(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, int delta) {
 struct xz_shell_event e={kind,x,y,delta,0,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t press(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_input(u,m,&e,out);
}
/* Keys as the screen sees them once shell.c offers them to the screen first. */
static size_t screen_key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_browse_screen.input(u,m,&e,out);
}
static size_t down_at(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r) {
 assert(r.w>0&&r.h>0);
 return send(u,m,XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0);
}
static size_t tap(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r) {
 size_t n=down_at(u,m,r);
 struct xz_shell_action keep[XZ_SHELL_ACTIONS];memcpy(keep,out,sizeof keep);
 size_t k=send(u,m,XZ_SHELL_TOUCH_UP,r.x+r.w/2,r.y+r.h/2,0);
 if(!k)memcpy(out,keep,sizeof keep);
 return n+k;
}
static int is_op(const struct xz_shell_action *a, enum xz_shell_browse_op o) { return a->kind==XZ_SHELL_ACT_BROWSE&&a->id==(int)o; }
static void model(struct xz_shell_model *m, struct xz_shell_browse_model *b) {
 fx_model_full(m,&track);
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD;
 m->browse=b;
}
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, enum xz_shell_screen s) {
 xz_shell_init(u);xz_shell_browse_reset();
 assert(press(u,m,k)==0&&xz_shell_screen(u)==s);
}
static int key_index(const char *label) {
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(!strcmp(xz_shell_browse_key_label(i),label))return i;
 assert(0);return -1;
}

static void test_open_and_hand_off(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 static const struct { enum xz_shell_key key; enum xz_shell_screen screen; uint32_t cap; } lists[]={
  {XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE,XZ_SHELL_CAP_BROWSE},{XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_PLAYLIST,XZ_SHELL_CAP_BROWSE},
  {XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_TAG_LIST,XZ_SHELL_CAP_TAG_LIST},{XZ_SHELL_KEY_SEARCH,XZ_SHELL_SEARCH,XZ_SHELL_CAP_SEARCH}};
 for(size_t i=0;i<sizeof lists/sizeof *lists;i++){
  b.screen=lists[i].screen;
  open_screen(&u,&m,lists[i].key,lists[i].screen);
  render_checked(&u,&m,buffer_a);
  /* Missing capability or slot: the stock list takes over, the stack does not move. */
  struct xz_shell_model no=m;no.capabilities&=~lists[i].cap;
  xz_shell_init(&u);
  assert(press(&u,&no,lists[i].key)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==(int)lists[i].screen&&u.depth==1);
  no=m;no.browse=NULL;
  assert(press(&u,&no,lists[i].key)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&u.depth==1);
 }
 /* The slot vanishing while a list is open: any touch hands off, rendering still works. */
 b.screen=XZ_SHELL_BROWSE;
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 struct xz_shell_model gone=m;gone.browse=NULL;
 render_checked(&u,&gone,buffer_a);
 assert(send(&u,&gone,XZ_SHELL_TOUCH_DOWN,400,200,0)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_BROWSE);
 assert(send(&u,&gone,XZ_SHELL_ENCODER_TURN,0,0,1)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
 puts("PASS list screens open, hand off without slot or capability");
}

static void test_cursor_scroll_and_window(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 struct xz_shell_view *v=&u.stack[1];
 assert(xz_shell_browse_rows_visible(&u)==9);
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,5)==0&&v->cursor==5&&v->scroll==0);
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,5)==0&&v->cursor==10&&v->scroll==2);
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,-100)==0&&v->cursor==0&&v->scroll==0);
 /* To the end: clamped, and the rows past the window are asked for, not invented. */
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1000)==1&&v->cursor==BX_TRACKS-1&&v->scroll==BX_TRACKS-9);
 assert(is_op(&out[0],XZ_SHELL_BROWSE_REQUEST_WINDOW)&&out[0].value==BX_TRACKS-XZ_SHELL_BROWSE_WINDOW);
 render_checked(&u,&m,buffer_a);                       /* rows beyond the window draw blank */
 struct xz_shell_rect last=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,8);
 assert(at(frame(buffer_a),last.x+last.w/2,last.y+2)!=rgb565(SH_BLUE));  /* no selection drawn on an unread row */
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1)==0);   /* same request is not repeated */
 bx_window(&b,&lib,out[0].value);bx_window(&b,&lib,BX_TRACKS-XZ_SHELL_BROWSE_WINDOW);
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1)==0&&v->cursor==BX_TRACKS-1);
 render_checked(&u,&m,buffer_a);
 assert(at(frame(buffer_a),last.x+last.w/2,last.y+2)==rgb565(SH_BLUE));
 /* Back to the top asks for the first window again. */
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,-1000)==1&&is_op(&out[0],XZ_SHELL_BROWSE_REQUEST_WINDOW)&&out[0].value==0);
 bx_window(&b,&lib,0);
 /* Unknown total: the cursor stops at the window end and asks for more. */
 b.known&=~(1u<<XZ_SHELL_BF_TOTAL);
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,100)==1&&v->cursor==XZ_SHELL_BROWSE_WINDOW-1);
 assert(is_op(&out[0],XZ_SHELL_BROWSE_REQUEST_WINDOW)&&out[0].value==XZ_SHELL_BROWSE_WINDOW-9);
 render_checked(&u,&m,buffer_a);
 /* An empty list: everything stays at zero. */
 b.known|=1u<<XZ_SHELL_BF_TOTAL;b.total=0;b.window_count=0;
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,3)==0&&v->cursor==0&&v->scroll==0);
 assert(send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)==0);
 render_checked(&u,&m,buffer_a);
 puts("PASS cursor and scroll clamps, window requests");
}

static void test_touch_load_and_return(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 struct xz_shell_view *v=&u.stack[1];
 /* Touch-down highlights and shows LOAD; no action yet. */
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,0).w==0);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,4))==0&&v->cursor==4&&xz_shell_browse_touched()==4);
 struct xz_shell_rect load=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,0),deck2=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,2);
 assert(load.w>0&&load.h>=36&&deck2.x>load.x);
 render_checked(&u,&m,buffer_a);
 assert(at(frame(buffer_a),load.x+4,load.y+load.h/2)==rgb565(SH_WHITE));
 /* A second touch on the same track keeps it highlighted and loads nothing. */
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,4))==0&&u.depth==2);
 /* Without LOAD capability the chip hands the list to the stock browser. */
 struct xz_shell_model noload=m;noload.capabilities&=~(uint32_t)XZ_SHELL_CAP_LOAD;
 assert(down_at(&u,&noload,load)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_BROWSE&&u.depth==2);
 send(&u,&noload,XZ_SHELL_TOUCH_UP,0,0,0);
 /* LOAD goes to the active deck and returns to PLAY. */
 u.deck=1;
 assert(down_at(&u,&m,load)==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==1&&out[0].id==(int)lib.tracks[4].id);
 assert(xz_shell_screen(&u)==XZ_SHELL_PLAY&&u.depth==1);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 /* Reopening lands on the same row. */
 assert(press(&u,&m,XZ_SHELL_KEY_BROWSE)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect row4=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,4);
 struct xz_shell_rect title=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,2);
 assert(at(frame(buffer_a),title.x+title.w-20,row4.y+2)==rgb565(SH_BLUE));
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1)==0&&u.stack[1].cursor==5);
 /* A different list does not inherit that position. */
 press(&u,&m,XZ_SHELL_KEY_BROWSE);b.list_id=999;
 assert(press(&u,&m,XZ_SHELL_KEY_BROWSE)==0&&send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,0)==0&&u.stack[1].cursor==0);
 b.list_id=XZ_SHELL_BROWSE*100u+7u;
 /* The deck chips pick the deck; the encoder push loads to the active deck. */
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);u.deck=0;
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,1));
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,2))==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==1&&out[0].id==(int)lib.tracks[1].id);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);u.deck=1;
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 assert(send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==1&&out[0].id==(int)lib.tracks[2].id&&u.depth==1);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 assert(send(&u,&noload,XZ_SHELL_ENCODER_PUSH,0,0,0)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&u.depth==2);
 /* Encoder movement takes the LOAD chip away. */
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3));
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);
 assert(xz_shell_browse_touched()==-1&&xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,0).w==0);
 /* Touching a deck strip makes that deck the LOAD target. */
 u.deck=0;
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_STRIP,1))==1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==1&&u.deck==1);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_STRIP,1))==0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 puts("PASS touch highlight, LOAD chip, hand-off without LOAD, return and cursor memory");
}

static void test_physical_load(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 const enum xz_shell_screen screens[]={XZ_SHELL_BROWSE,XZ_SHELL_PLAYLIST,XZ_SHELL_TAG_LIST,XZ_SHELL_SEARCH};
 const enum xz_shell_key keys[]={XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_SEARCH};
 for(size_t i=0;i<sizeof screens/sizeof *screens;i++)for(int deck=0;deck<2;deck++){
  model(&m,&b);bx_tracks(&b,&lib,screens[i]);
  open_screen(&u,&m,keys[i],screens[i]);u.deck=1-deck;
  send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
  struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_LOAD,deck};
  struct xz_shell_model no=m;no.capabilities&=~(uint32_t)XZ_SHELL_CAP_LOAD;
  assert(xz_shell_input(&u,&no,&e,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==(int)screens[i]&&u.depth==2);
  assert(xz_shell_input(&u,&m,&e,out)==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==deck&&out[0].id==(int)lib.tracks[2].id);
  assert(u.depth==1&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
  /* Reopen and load the remembered highlight before any encoder or touch input. */
  assert(press(&u,&m,keys[i])==0);
  assert(xz_shell_input(&u,&m,&e,out)==1&&out[0].id==(int)lib.tracks[2].id&&out[0].deck==deck);
  bx_folders(&b,&lib,screens[i]);open_screen(&u,&m,keys[i],screens[i]);
  struct xz_shell before=u;
  assert(xz_shell_input(&u,&m,&e,out)==0&&!memcmp(&before,&u,sizeof u));
  assert(xz_shell_input(&u,&no,&e,out)==0&&!memcmp(&before,&u,sizeof u));
  bx_tracks(&b,&lib,screens[i]);b.window_count=0;
  assert(xz_shell_input(&u,&m,&e,out)==0&&!memcmp(&before,&u,sizeof u));
  b.total=0;
  assert(xz_shell_input(&u,&no,&e,out)==0&&!memcmp(&before,&u,sizeof u));
 }
 puts("PASS physical LOAD: all list screens and decks, hand-off, remembered highlight, folders and no highlight");
}
static void test_folders_and_back(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_folders(&b,&lib,XZ_SHELL_PLAYLIST);
 open_screen(&u,&m,XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_PLAYLIST);
 struct xz_shell_view *v=&u.stack[1];
 /* No rail on PLAYLIST: the list starts at the left edge. */
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,0).w==0&&xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0).x==0);
 /* First touch highlights a folder, the second opens it. */
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3))==0&&v->cursor==3);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,0).w==0);  /* folders have no LOAD */
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3))==1&&is_op(&out[0],XZ_SHELL_BROWSE_OPEN)&&out[0].value==(int32_t)lib.folders[3].id);
 assert(v->cursor==0&&v->scroll==0);
 /* The runtime publishes the playlist; the cursor moves in it. */
 bx_tracks(&b,&lib,XZ_SHELL_PLAYLIST);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,12);
 render_checked(&u,&m,buffer_a);
 assert(xz_shell_browse_back(&u,&m)==1);
 /* BACK climbs one level and restores the folder row; at the top it is not consumed. */
 assert(screen_key(&u,&m,XZ_SHELL_KEY_BACK)==1&&is_op(&out[0],XZ_SHELL_BROWSE_UP)&&v->cursor==3);
 bx_folders(&b,&lib,XZ_SHELL_PLAYLIST);
 assert(screen_key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==2);
 assert(xz_shell_browse_back(&u,&m)==0);
 /* The encoder push opens a folder; the header arrow climbs. */
 assert(send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)==1&&is_op(&out[0],XZ_SHELL_BROWSE_OPEN)&&out[0].value==(int32_t)lib.folders[3].id);
 bx_tracks(&b,&lib,XZ_SHELL_PLAYLIST);
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_BACK,0))==1&&is_op(&out[0],XZ_SHELL_BROWSE_UP)&&v->cursor==3);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 /* BACK_HOLD at depth goes to the category top. */
 b.depth=2;
 assert(screen_key(&u,&m,XZ_SHELL_KEY_BACK_HOLD)==1&&is_op(&out[0],XZ_SHELL_BROWSE_TOP)&&v->cursor==0);
 /* The arrow at the top level leaves the list, through the navigation table. */
 bx_folders(&b,&lib,XZ_SHELL_PLAYLIST);
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_BACK,0))==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 /* The source tile opens SOURCE through the same table (hand-off when SOURCE is not available). */
 open_screen(&u,&m,XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_PLAYLIST);
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_SOURCE,0))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SOURCE);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 puts("PASS folders open on confirm, BACK and the arrow climb, cursor restored");
}

static void test_drag_sort_jump(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 struct xz_shell_view *v=&u.stack[1];
 /* Drag scrolls by whole rows and leaves the cursor where it was. */
 struct xz_shell_rect r=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,6);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,r.x+300,r.y+10,0);
 assert(v->cursor==6);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,r.x+300,r.y+5,0);
 assert(v->scroll==0&&v->cursor==6);                     /* under the drag threshold */
 for(int dy=-10;dy>=-100;dy-=10)send(&u,&m,XZ_SHELL_TOUCH_MOVE,r.x+300,r.y+10+dy,0);  /* slow drag */
 assert(v->scroll==3&&v->cursor==0&&xz_shell_browse_touched()==-1);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,r.x+300,r.y+10-100,0)==0&&v->scroll==3);
 /* A fast last step flicks further; never past the end. */
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,r.x+300,r.y,0);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,r.x+300,r.y-60,0);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,r.x+300,r.y-100,0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,r.x+300,r.y-100,0);
 assert(v->scroll>3+3);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,r.x+300,r.y,0);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,r.x+300,r.y-10000,0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,r.x+300,r.y-10000,0);
 assert(v->scroll==BX_TRACKS-9);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,r.x+300,r.y,0);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,r.x+300,r.y+10000,0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,r.x+300,r.y+10000,0);
 assert(v->scroll==0);
 /* Column headers sort; the sorted one flips direction. Hit band is 44 px. */
 struct xz_shell_rect bpm=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,3),title=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,2);
 assert(bpm.h>=44&&title.h>=44);
 assert(down_at(&u,&m,bpm)==1&&is_op(&out[0],XZ_SHELL_BROWSE_SORT)&&out[0].value==XZ_SHELL_COL_BPM);
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 assert(down_at(&u,&m,title)==1&&out[0].value==(XZ_SHELL_COL_TITLE|1<<8));
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0))==0);  /* PREVIEW does not sort */
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 /* Encoder hold: letter jump through the index, overlay drawn, push ends it without loading. */
 v->cursor=0;v->scroll=0;
 assert(send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0)==0);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);
 assert(v->cursor==lib.letters[1].row&&v->scroll==v->cursor);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,100);
 assert(v->cursor==lib.letters[lib.letter_count-1].row);
 bx_window(&b,&lib,v->scroll);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect list=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0);
 assert(at(frame(buffer_a),list.x+list.w/2-60,list.y+list.h/2)==rgb565(0x282828));  /* overlay box */
 assert(send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)==0&&u.depth==2);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);
 assert(v->cursor==lib.letters[lib.letter_count-1].row+1);  /* jump over: plain cursor again */
 /* Without an index the hold jumps a page. */
 b.letters=NULL;b.letter_count=0;bx_window(&b,&lib,0);v->cursor=v->scroll=0;
 send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 assert(v->cursor==18&&v->scroll==18);
 render_checked(&u,&m,buffer_a);
 send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);
 assert(v->cursor==19);
 puts("PASS drag and flick scroll, sort, letter and page jump");
}

static void test_rail_and_toggles(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 struct xz_shell_view *v=&u.stack[1];
 for(int i=0;i<8;i++)assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,i).h>=44);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,8).w==0);
 /* The current category is lit. */
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect cur=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,XZ_SHELL_CAT_TRACK);
 assert(at(frame(buffer_a),cur.x+3,cur.y+3)==rgb565(XZ_FLAGSHIP_RAIL_SELECTED));
 /* A category opens on release when the finger did not drag. */
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,4);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,XZ_SHELL_CAT_ALBUM))==1&&is_op(&out[0],XZ_SHELL_BROWSE_CATEGORY)&&out[0].value==XZ_SHELL_CAT_ALBUM&&v->cursor==0);
 /* Twelve entries: the rail scrolls by drag to reach the blocked ones, which hand off. */
 struct xz_shell_rect r7=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,7);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,r7.x+20,r7.y+10,0);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,r7.x+20,r7.y+10-400,0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,r7.x+20,r7.y+10-400,0)==0);
 /* first visible is now FOLDER-4 = entry 4; HISTORY is slot 6, MATCHING slot 7 */
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,6))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_BROWSE);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,7))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,4))==1&&is_op(&out[0],XZ_SHELL_BROWSE_CATEGORY)&&out[0].value==XZ_SHELL_CAT_PLAYLIST);
 render_checked(&u,&m,buffer_a);
 /* Unknown categories are not drawn and not touchable. */
 b.category_known=1u<<XZ_SHELL_CAT_TRACK;
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,3))==0);
 render_checked(&u,&m,buffer_a);
 b.category_known=bx_all_categories();
 /* PREVIEW and INFO are shell toggles; they reshape the columns and the list. */
 int list_w=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0).w;
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO,0))==0);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO_PANE,0).w>0&&xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0).w<list_w);
 for(int i=0;xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,i).w;i++){
  struct xz_shell_rect c=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,i);
  assert(c.x+c.w<=XZ_SHELL_WIDTH-264);
 }
 render_checked(&u,&m,buffer_a);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO_PANE,0))==0);
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO,0));
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO_PANE,0).w==0);
 struct xz_shell_rect first=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0);
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_PREVIEW,0));
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0).w!=first.w);
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_PREVIEW,0));
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0).w==first.w);
 puts("PASS category rail, blocked categories hand off, PREVIEW and INFO toggles");
}

static void test_search_keyboard(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_SEARCH);
 open_screen(&u,&m,XZ_SHELL_KEY_SEARCH,XZ_SHELL_SEARCH);
 assert(xz_shell_browse_rows_visible(&u)==4);
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++){
  struct xz_shell_rect k=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,i);
  assert(k.h>=44&&k.x>=0&&k.x+k.w<=XZ_SHELL_WIDTH&&k.y>=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0).y+4*38&&k.y+k.h<=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_STRIP,0).y);
  for(int j=0;j<i;j++){
   struct xz_shell_rect o=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,j);
   assert(k.x>=o.x+o.w||o.x>=k.x+k.w||k.y>=o.y+o.h||o.y>=k.y+k.h);   /* keys never overlap */
  }
 }
 xz_shell_browse_query();
 assert(!strcmp(xz_shell_browse_query(),""));
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("N")))==1&&is_op(&out[0],XZ_SHELL_BROWSE_SEARCH_QUERY)&&out[0].value==1);
 assert(u.stack[1].cursor==0);   /* new results start at the top */
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("E")));
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("SPACE")));
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("7")));
 assert(!strcmp(xz_shell_browse_query(),"NE 7")&&out[0].value==4);
 /* The key highlights while held. */
 down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("DELETE")));
 assert(!strcmp(xz_shell_browse_query(),"NE ")&&is_op(&out[0],XZ_SHELL_BROWSE_SEARCH_QUERY)&&out[0].value==3);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect del=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("DELETE"));
 assert(at(frame(buffer_a),del.x+2,del.y+2)==rgb565(SH_BLUE));
 send(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 render_checked(&u,&m,buffer_b);
 assert(at(frame(buffer_b),del.x+2,del.y+2)!=rgb565(SH_BLUE));
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("CLEAR")))==1&&out[0].value==0&&!strcmp(xz_shell_browse_query(),""));
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("CLEAR")))==0);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("DELETE")))==0);
 /* The buffer is bounded. */
 for(int i=0;i<200;i++)tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("W")));
 assert(strlen(xz_shell_browse_query())==XZ_SHELL_BROWSE_QUERY-1);
 render_checked(&u,&m,buffer_a);
 /* SEARCH puts the keyboard away: results take the height; the field brings it back. */
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,key_index("SEARCH")))==0);
 assert(xz_shell_browse_rows_visible(&u)==9&&xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,0).w==0);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,8);
 render_checked(&u,&m,buffer_a);
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_FIELD,0));
 assert(xz_shell_browse_rows_visible(&u)==4&&u.stack[1].scroll==u.stack[1].cursor-3);
 /* Results load like any list. */
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,0));
 assert(down_at(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,0))==1&&out[0].kind==XZ_SHELL_ACT_LOAD);
 /* The query survives leaving and reopening SEARCH. */
 assert(press(&u,&m,XZ_SHELL_KEY_SEARCH)==0&&strlen(xz_shell_browse_query())==XZ_SHELL_BROWSE_QUERY-1);
 puts("PASS search keyboard, query buffer, SEARCH_QUERY actions");
}

static void test_tag_list(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_TAG_LIST);b.total=12;b.window_count=12;
 open_screen(&u,&m,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_TAG_LIST);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,0).w==0);
 render_checked(&u,&m,buffer_a);
 b.total=100;render_checked(&u,&m,buffer_b);
 assert(memcmp(buffer_a,buffer_b,sizeof buffer_a));
 assert(send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,500)==1&&u.stack[1].cursor==99);
 struct xz_shell_model notag=m;notag.capabilities&=~(uint32_t)XZ_SHELL_CAP_TAG_LIST;
 xz_shell_init(&u);
 assert(press(&u,&notag,XZ_SHELL_KEY_TAG_LIST)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_TAG_LIST);
 puts("PASS tag list count, no rail, hand-off without capability");
}

static void test_unknown_and_bounds(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b,zero;
 model(&m,&b);
 /* Garbage everywhere but nothing known and every pointer NULL renders like a zeroed model. */
 memset(&b,0xa5,sizeof b);
 b.known=0;b.category_known=0;b.rows=NULL;b.letters=NULL;b.source_name=b.level_name=b.parent_name=NULL;
 xz_shell_init(&u);xz_shell_browse_reset();
 u.stack[1]=(struct xz_shell_view){XZ_SHELL_BROWSE,0,0,0};u.depth=2;
 render_checked(&u,&m,buffer_a);
 memset(&zero,0,sizeof zero);m.browse=&zero;
 render_checked(&u,&m,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* A zeroed slot (screen PLAY) on every list screen: no rows, no crash, input works. */
 static const enum xz_shell_key keys[]={XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_SEARCH};
 for(size_t i=0;i<4;i++){
  xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,keys[i]);
  render_checked(&u,&m,buffer_a);
  send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,5);send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
  send(&u,&m,XZ_SHELL_TOUCH_DOWN,400,200,0);send(&u,&m,XZ_SHELL_TOUCH_UP,400,200,0);
  assert(u.stack[u.depth-1].cursor==0||xz_shell_screen(&u)==XZ_SHELL_PLAY);
  render_checked(&u,&m,buffer_a);
 }
 /* Rows from another list never draw: a BROWSE snapshot under TAG LIST shows nothing. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);m.browse=&b;
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_TAG_LIST);
 render_checked(&u,&m,buffer_a);
 bx_tracks(&zero,&lib,XZ_SHELL_BROWSE);zero.rows=NULL;zero.window_count=0;zero.known=0;m.browse=&zero;
 render_checked(&u,&m,buffer_b);
 struct xz_shell_rect list=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0);
 for(int y=list.y;y<list.y+list.h;y++)for(int x=list.x;x<list.x+list.w;x++)assert(at(frame(buffer_a),x,y)==at(frame(buffer_b),x,y));
 assert(send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)==0);
 /* Known fields with NULL pointers, unknown rows, wild values, long strings: all in bounds. */
 m.browse=&b;bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 static struct xz_shell_browse_row odd[XZ_SHELL_BROWSE_WINDOW];
 memcpy(odd,lib.tracks,sizeof odd);
 static char wide[400];memset(wide,'W',sizeof wide-1);
 odd[0].title=wide;odd[0].artist=wide;odd[0].key=wide;
 odd[1].title=NULL;odd[1].artist=NULL;odd[1].key=NULL;odd[1].preview=NULL;
 odd[2].known=0;odd[3].bpm_x100=-5;odd[3].rating=99;odd[3].duration_ms=-1;odd[3].preview_count=0;
 odd[4].bpm_x100=INT32_MAX;odd[4].duration_ms=INT32_MAX;odd[4].on_deck=0xff;odd[4].kind=(enum xz_shell_browse_row_kind)7;
 odd[5].title="\xc3\x89t\xc3\xa9 \xe2\x99\xaa \xf0\x9f\x8e\xb5 odd bytes \xff\xfe";
 b.rows=odd;b.level_name=wide;b.parent_name=wide;b.source_name=wide;b.depth=3;b.known|=1u<<XZ_SHELL_BF_PARENT;
 b.total=INT32_MAX;b.column_count=99;
 for(int i=0;i<XZ_SHELL_BROWSE_COLUMNS;i++)b.columns[i]=(enum xz_shell_browse_column)(i*3);
 b.sort_column=(enum xz_shell_browse_column)42;
 xz_shell_init(&u);xz_shell_browse_reset();press(&u,&m,XZ_SHELL_KEY_BROWSE);
 render_checked(&u,&m,buffer_a);
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,0));
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO,0));
 for(int i=0;i<6;i++){send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);render_checked(&u,&m,buffer_a);}
 /* The title column never runs into the header tiles, and columns tile the list. */
 b.column_count=6;
 static const enum xz_shell_browse_column all[]={XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_TITLE,XZ_SHELL_COL_ARTIST,XZ_SHELL_COL_BPM,XZ_SHELL_COL_KEY,XZ_SHELL_COL_RATING};
 memcpy(b.columns,all,sizeof all);
 for(int info=0;info<2;info++){
  struct xz_shell_rect l=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0);
  int x=l.x,i=0;
  for(;xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,i).w;i++){
   struct xz_shell_rect c=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,i);
   assert(c.x==x);x+=c.w;
  }
  assert(x==l.x+l.w&&i>=2);
  render_checked(&u,&m,buffer_a);
  tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO,0));
 }
 /* Deep cursor at the far end of a huge list, jump overlay over it. */
 b.total=100000;b.window_start=99990;b.window_count=10;b.rows=lib.tracks;
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,200000);
 assert(u.stack[1].cursor==99999&&u.stack[1].scroll==99991);
 send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 render_checked(&u,&m,buffer_a);
 /* Stale stack depth is refused, not drawn. */
 u.depth=0;assert(!xz_shell_render(&u,&m,frame(buffer_a),STRIDE));
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LIST,0).w==0);
 puts("PASS unknown and garbage models, long strings, deep cursor, bounds");
}

static void test_deterministic(void) {
 struct xz_shell u;struct xz_shell_model m,copy;struct xz_shell_browse_model b,bcopy;
 model(&m,&b);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 copy=m;bcopy=b;
 render_checked(&u,&m,buffer_a);
 render_checked(&u,&m,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,2));
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,30);
 send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 render_checked(&u,&m,buffer_a);
 assert(!memcmp(&m,&copy,sizeof m)&&!memcmp(&b,&bcopy,sizeof b));
 puts("PASS deterministic render, model untouched");
}

/* The physical BACK key, through the shell, climbs a folder level; at the top it leaves the list. */
static void test_back_key_through_shell(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 model(&m,&b);bx_folders(&b,&lib,XZ_SHELL_PLAYLIST);
 open_screen(&u,&m,XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_PLAYLIST);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3))==0);
 assert(tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3))==1&&is_op(&out[0],XZ_SHELL_BROWSE_OPEN));
 bx_tracks(&b,&lib,XZ_SHELL_PLAYLIST);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK)==1&&is_op(&out[0],XZ_SHELL_BROWSE_UP)&&xz_shell_screen(&u)==XZ_SHELL_PLAYLIST);
 b.depth=2;
 assert(press(&u,&m,XZ_SHELL_KEY_BACK_HOLD)==1&&is_op(&out[0],XZ_SHELL_BROWSE_TOP)&&xz_shell_screen(&u)==XZ_SHELL_PLAYLIST);
 bx_folders(&b,&lib,XZ_SHELL_PLAYLIST);
 press(&u,&m,XZ_SHELL_KEY_BACK);
 assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 puts("PASS physical BACK climbs a folder, BACK_HOLD goes to the top, and BACK at the top leaves the list");
}
int main(void) {
 fx_track_build(&track,7);
 bx_build(&lib);
 test_open_and_hand_off();
 test_cursor_scroll_and_window();
 test_touch_load_and_return();
 test_physical_load();
 test_folders_and_back();
 test_drag_sort_jump();
 test_rail_and_toggles();
 test_search_keyboard();
 test_tag_list();
 test_unknown_and_bounds();
 test_deterministic();
 test_back_key_through_shell();
 puts("PASS browse");
 return 0;
}
