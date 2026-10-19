#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "fixtures.h"
#include "settings_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static uint16_t *frame(uint16_t *buffer) { return buffer+GUARD+STRIDE; }
static void fill(uint16_t *buffer) { for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a; }
static void check_guards(uint16_t *buffer) {
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(frame(buffer)[y*STRIDE+x]==0xa55a);
}
static uint16_t at(const uint16_t *f, int x, int y) { return f[(size_t)y*STRIDE+(size_t)x]; }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static const uint16_t *render(const struct xz_shell *u, const struct xz_shell_model *m) {
 fill(buffer_a);
 assert(xz_shell_render(u,m,frame(buffer_a),STRIDE));
 check_guards(buffer_a);
 return frame(buffer_a);
}
static size_t send(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, int delta) {
 struct xz_shell_event e={kind,x,y,delta,0,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static size_t key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) { return send(u,m,XZ_SHELL_ENCODER_TURN,0,0,delta); }
static size_t push(struct xz_shell *u, const struct xz_shell_model *m) { return send(u,m,XZ_SHELL_ENCODER_PUSH,0,0,0); }
static size_t hold(struct xz_shell *u, const struct xz_shell_model *m) { return send(u,m,XZ_SHELL_ENCODER_HOLD,0,0,0); }
/* A tap: touch down then up at one point. Returns the actions of both. */
static size_t tap(struct xz_shell *u, const struct xz_shell_model *m, int x, int y) {
 struct xz_shell_action both[XZ_SHELL_ACTIONS*2];size_t n=send(u,m,XZ_SHELL_TOUCH_DOWN,x,y,0);
 memcpy(both,out,sizeof out);
 size_t k=send(u,m,XZ_SHELL_TOUCH_UP,x,y,0);
 memcpy(both+n,out,k*sizeof *out);
 memcpy(out,both,sizeof out);
 return n+k;
}
static void model(struct xz_shell_model *m) {
 fx_model_full(m,&track);
 m->settings_screen=fxs_reset();
}
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, enum xz_shell_screen s) {
 xz_shell_init(u);
 assert(key(u,m,k)==0&&xz_shell_screen(u)==s&&u->depth==2);
 /* Held MENU lands on MODS > UTILITY with the encoder on the rail: push enters its items. */
 if(s==XZ_SHELL_UTILITY){
  assert(u->mods_view.tab==0&&u->mods_view.on_tabs==1);
  struct xz_shell_event e={XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1};
  struct xz_shell_action o[XZ_SHELL_ACTIONS];
  assert(xz_shell_input(u,m,&e,o)==0&&u->mods_view.on_tabs==0);
 }
}
/* Focus pane and cursor are packed into the view; see shell_settings.c. */
static int pane(const struct xz_shell *u) { return u->stack[u->depth-1].focus&3; }
static int cursor_id(const struct xz_shell *u) { return u->stack[u->depth-1].cursor-1; }

enum { ROW=XZ_SHELL_SETTINGS_ROW_H, RAIL=XZ_SHELL_SETTINGS_RAIL_H, ROWS=XZ_SHELL_SETTINGS_ROWS };
/* UTILITY's list with the fixture table: each category a header row over its items. DJ SETTING's header
   is row 0 and its eleven items rows 1-11; DISPLAY (LCD) heads row 12 with WAVEFORM COLOR on row 13;
   VERSION, under INFO, is the last of 39 rows. */
enum { LIST_ROWS=39, LCD_HEAD=12 };
static struct xz_shell_rect R(enum xz_shell_settings_region r) { return xz_shell_settings_rect(r); }
static int list_row(int r) { return R(XZ_SHELL_SR_ITEMS).y+r*ROW+ROW/2; }
static int row_top(int r) { return R(XZ_SHELL_SR_ITEMS).y+r*ROW+3; }
static int item_x(void) { return R(XZ_SHELL_SR_ITEMS).x+4; }
static int value_x(void) { return R(XZ_SHELL_SR_VALUES).x+4; }

static void test_regions(void) {
 struct xz_shell_rect rail=R(XZ_SHELL_SR_RAIL),items=R(XZ_SHELL_SR_ITEMS),values=R(XZ_SHELL_SR_VALUES),status=R(XZ_SHELL_SR_STATUS),back=R(XZ_SHELL_SR_BACK_BUTTON);
 assert(rail.x+rail.w<items.x&&items.x+items.w<values.x&&values.x+values.w==XZ_SHELL_WIDTH);
 assert(rail.y+rail.h<=status.y&&status.y+status.h==XZ_SHELL_HEIGHT&&rail.h==ROWS*ROW&&7*RAIL<=rail.h);
 assert(rail.y==back.y+back.h-2);   /* no tab row: the panes start under the title bar */
 assert(back.w>=44&&back.h>=44&&ROW>=36&&RAIL>=44);
 for(int q=0;q<4;q++){
  struct xz_shell_rect r=xz_shell_shortcut_quadrant(q);
  assert(r.x>=0&&r.x+r.w<=XZ_SHELL_WIDTH&&r.y>=back.y&&r.y+r.h<=status.y);
  for(int k=0;k<4;k++){
   struct xz_shell_rect s=xz_shell_shortcut_slot(q,k);
   assert(s.h>=44&&s.y>=r.y&&s.y+s.h<=r.y+r.h+1);
  }
 }
 assert(xz_shell_shortcut_quadrant(4).w==0&&xz_shell_shortcut_slot(0,4).w==0&&xz_shell_shortcut_slot(-1,0).w==0);
 puts("PASS settings regions: 44 px targets, panes side by side over the deck strip");
}

static void test_empty_and_unknown(void) {
 struct xz_shell u;struct xz_shell_model m;
 model(&m);
 static struct xz_shell_settings_model empty, no_items={5,NULL,NULL};   /* empty is what test_shell.c and preview.c pass */
 const struct xz_shell_settings_model *slots[]={&empty,&no_items};
 for(size_t i=0;i<sizeof slots/sizeof *slots;i++){
  m.settings_screen=slots[i];
  for(int screen=0;screen<2;screen++){
   open_screen(&u,&m,screen?XZ_SHELL_KEY_MENU_HOLD:XZ_SHELL_KEY_SHORTCUT,screen?XZ_SHELL_UTILITY:XZ_SHELL_SHORTCUT);
   render(&u,&m);
   /* An empty table still lists what the shell supplies: its display items and the play model's settings. */
   assert(turn(&u,&m,3)==0);
   size_t n=push(&u,&m);
   assert(n<=2&&(n==0||out[0].kind==XZ_SHELL_ACT_HAND_OFF||out[0].kind==XZ_SHELL_ACT_SET_SETTING));
   assert(hold(&u,&m)==0);
   render(&u,&m);
   if(screen){while(xz_shell_settings_back(&u,&m));}
   assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==1);
  }
  open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
  turn(&u,&m,3);assert(cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR);
  assert(push(&u,&m)>=1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_COLOR&&out[0].value==XZ_SHELL_WAVE_STEMS);
  assert(u.display.wave_color==XZ_SHELL_WAVE_STEMS);
 }
 /* Wild items: NULL strings, bad counts and categories, out-of-range current. */
 static const char *const holes[]={NULL,"ON",NULL};
 static struct xz_shell_setting_item wild[]={
  {XZ_SHELL_ITEM_WAVE_COLOR,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,NULL,NULL,1000,999,1,1},
  {XZ_SHELL_ITEM_LOAD_LOCK,(enum xz_shell_setting_category)99,"BAD CATEGORY",holes,3,0,1,1},
  {XZ_SHELL_ITEM_AUTO_CUE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"HOLES",holes,3,0,1,1},
  {XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_SETTING_CAT_DJ_SETTING,"NEGATIVE",holes,-4,-7,1,0},
  {XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"PAST END",holes,3,3,1,1},
 };
 static struct xz_shell_settings_model wild_model={sizeof wild/sizeof *wild,wild,NULL};
 m.settings_screen=&wild_model;
 for(int screen=0;screen<2;screen++){
  open_screen(&u,&m,screen?XZ_SHELL_KEY_MENU_HOLD:XZ_SHELL_KEY_SHORTCUT,screen?XZ_SHELL_UTILITY:XZ_SHELL_SHORTCUT);
  for(int step=0;step<6;step++){render(&u,&m);turn(&u,&m,1);push(&u,&m);hold(&u,&m);}
  render(&u,&m);
 }
 /* A known current past the end reads as unknown: the value pane shows no chosen dot. */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 turn(&u,&m,2);assert(cursor_id(&u)==XZ_SHELL_ITEM_TIME_MODE);
 const uint16_t *f=render(&u,&m);
 for(int r=0;r<3;r++)assert(at(f,R(XZ_SHELL_SR_VALUES).x+22,R(XZ_SHELL_SR_VALUES).y+r*ROW+18)!=rgb565(SH_BLUE));
 /* Writing to it picks the first value; nothing was read to compare against. */
 assert(push(&u,&m)==0&&pane(&u)==2);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_TIME_MODE&&out[0].value==0&&out[0].deck==-1);
 /* Garbage view state is clamped, never trusted. */
 model(&m);
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 u.stack[1].cursor=-5;u.stack[1].focus=0x7fffffff;u.stack[1].scroll=-1;
 render(&u,&m);assert(turn(&u,&m,1)==0);render(&u,&m);
 u.stack[1].cursor=0x7fffffff;render(&u,&m);
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 u.stack[1].cursor=-99;render(&u,&m);assert(turn(&u,&m,1)==0&&cursor_id(&u)==XZ_SHELL_ITEM_BEAT_JUMP_BEAT);
 /* Unknown decks and an unknown table render without reading their data. */
 struct xz_shell_model bare;memset(&bare,0,sizeof bare);
 bare.settings_screen=fxs_reset();
 for(int i=0;i<FXS_COUNT;i++){fxs_table[i].known=0;fxs_table[i].current=12345;}
 open_screen(&u,&bare,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);render(&u,&bare);
 open_screen(&u,&bare,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);render(&u,&bare);
 /* The slot disappearing under an open screen draws the chrome and hands off on the next push or touch. */
 model(&m);
 for(int screen=0;screen<2;screen++){
  open_screen(&u,&m,screen?XZ_SHELL_KEY_MENU_HOLD:XZ_SHELL_KEY_SHORTCUT,screen?XZ_SHELL_UTILITY:XZ_SHELL_SHORTCUT);
  struct xz_shell_model gone=m;gone.settings_screen=NULL;
  render(&u,&gone);
  assert(turn(&u,&gone,1)==0);
  assert(push(&u,&gone)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==(screen?XZ_SHELL_UTILITY:XZ_SHELL_SHORTCUT));
  assert(send(&u,&gone,XZ_SHELL_TOUCH_DOWN,400,200,0)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
  send(&u,&gone,XZ_SHELL_TOUCH_UP,400,200,0);
  if(screen)assert(tap(&u,&gone,20,20)==0&&u.depth==2&&u.mods_view.on_tabs);   /* MODS: back to the rail first */
  assert(tap(&u,&gone,20,20)==0&&u.depth==1);   /* the back button still closes */
 }
 puts("PASS empty, NULL and wild settings render and hand off without reading garbage");
}

static void test_utility_encoder(void) {
 struct xz_shell u;struct xz_shell_model m;
 model(&m);
 struct xz_shell_rect page0=xz_shell_mods_tab_rect(NULL,&m,0);
 int page_x=page0.x+4,page_y=page0.y+page0.h-6;
 /* Held MENU: the encoder on the rail, UTILITY lit blue, no content row lit. */
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);
 const uint16_t *f=render(&u,&m);
 assert(u.mods_view.on_tabs&&at(f,page_x,page_y)==rgb565(SH_BLUE)&&at(f,item_x(),row_top(1))!=rgb565(SH_BLUE));
 /* The list: DJ SETTING's header, then its items; push put the cursor on the first item. */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 assert(pane(&u)==1&&cursor_id(&u)==XZ_SHELL_ITEM_LOAD_LOCK);
 f=render(&u,&m);
 assert(at(f,item_x(),row_top(1))==rgb565(SH_BLUE)&&at(f,item_x(),row_top(0))==rgb565(SH_BLACK));
 assert(at(f,page_x,page_y)==rgb565(XZ_FLAGSHIP_RAIL_SELECTED));
 /* Turn walks items past the headers and clamps; the list scrolls to keep the cursor in view. */
 assert(turn(&u,&m,10)==0&&cursor_id(&u)==XZ_SHELL_ITEM_WAVE_PHASE_METER);
 assert(turn(&u,&m,1)==0&&cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR);      /* over the DISPLAY (LCD) header */
 f=render(&u,&m);
 assert((u.stack[1].scroll&0xfff)==LCD_HEAD+1-ROWS+1&&at(f,item_x(),row_top(ROWS-1))==rgb565(SH_BLUE));
 assert(turn(&u,&m,100)==0&&cursor_id(&u)==XZ_SHELL_ITEM_VERSION&&(u.stack[1].scroll&0xfff)==LIST_ROWS-ROWS);
 turn(&u,&m,-100);assert(cursor_id(&u)==XZ_SHELL_ITEM_LOAD_LOCK&&(u.stack[1].scroll&0xfff)==0);
 /* Hold jumps to the next category's first item, its header on top; from the last it wraps. */
 assert(hold(&u,&m)==0&&cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR&&(u.stack[1].scroll&0xfff)==LCD_HEAD);
 f=render(&u,&m);assert(at(f,item_x(),row_top(0))==rgb565(SH_BLACK)&&at(f,item_x(),row_top(1))==rgb565(SH_BLUE));
 assert(hold(&u,&m)==0&&cursor_id(&u)==XZ_SHELL_ITEM_SLIP_FLASHING);
 for(int i=0;i<5;i++)hold(&u,&m);
 assert(cursor_id(&u)==XZ_SHELL_ITEM_LOAD_LOCK);                           /* past INFO, back to the top */
 /* Items -> values on a writable item: the value cursor starts on the current value. */
 turn(&u,&m,3);assert(cursor_id(&u)==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD);
 assert(push(&u,&m)==0&&pane(&u)==2);
 f=render(&u,&m);
 assert(at(f,value_x(),row_top(2))==rgb565(SH_BLUE)&&at(f,item_x(),row_top(4))==rgb565(XZ_FLAGSHIP_RAIL_SELECTED));
 assert(turn(&u,&m,5)==0);f=render(&u,&m);assert(at(f,value_x(),row_top(2))==rgb565(SH_BLUE));
 turn(&u,&m,-1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD&&out[0].value==1&&out[0].deck==-1);
 assert(pane(&u)==1);
 /* The model has not confirmed yet; pushing the current value again writes nothing. */
 push(&u,&m);assert(pane(&u)==2&&push(&u,&m)==0&&pane(&u)==1);
 /* BACK steps values, items, then the rail; then the navigation table closes the screen. */
 push(&u,&m);assert(pane(&u)==2);
 assert(xz_shell_settings_back(&u,&m)==1&&pane(&u)==1&&!u.mods_view.on_tabs);
 assert(xz_shell_settings_back(&u,&m)==1&&u.mods_view.on_tabs);
 assert(xz_shell_settings_back(&u,&m)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==1);
 assert(xz_shell_settings_back(&u,&m)==0);   /* not on UTILITY */
 /* Read-only items hand the screen to the stock menu on apply. */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 turn(&u,&m,1);assert(cursor_id(&u)==XZ_SHELL_ITEM_QUANTIZE_BEAT);
 push(&u,&m);turn(&u,&m,-1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_UTILITY&&pane(&u)==2);
 /* So do writable items when the runtime cannot set settings. */
 struct xz_shell_model no_set=m;no_set.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 open_screen(&u,&no_set,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 turn(&u,&no_set,8);assert(cursor_id(&u)==XZ_SHELL_ITEM_PLAY_MODE);
 push(&u,&no_set);turn(&u,&no_set,1);
 assert(push(&u,&no_set)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
 /* Shell-owned display items need no capability: the shell applies them itself. */
 open_screen(&u,&no_set,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 hold(&u,&no_set);assert(cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR);
 push(&u,&no_set);turn(&u,&no_set,-1);
 assert(push(&u,&no_set)>=1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_COLOR&&out[0].value==XZ_SHELL_WAVE_RGB);
 assert(u.display.wave_color==XZ_SHELL_WAVE_RGB);
 /* A known value draws the blue dot; an unknown one draws none. */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 hold(&u,&m);turn(&u,&m,2);assert(cursor_id(&u)==XZ_SHELL_ITEM_LCD_BRIGHTNESS);
 f=render(&u,&m);
 int dot_x=R(XZ_SHELL_SR_VALUES).x+22;
 assert(at(f,dot_x,R(XZ_SHELL_SR_VALUES).y+2*ROW+18)==rgb565(SH_BLUE));
 /* Under the choices, the description panel on black: what the setting does. */
 assert(at(f,value_x(),row_top(ROWS-1))==rgb565(SH_BLACK));
 turn(&u,&m,1);assert(cursor_id(&u)==XZ_SHELL_ITEM_JOG_LCD_BRIGHTNESS);
 f=render(&u,&m);
 for(int r=0;r<5;r++)assert(at(f,dot_x,R(XZ_SHELL_SR_VALUES).y+r*ROW+18)!=rgb565(SH_BLUE));
 /* Long value lists scroll and keep the cursor in view above the description panel. */
 turn(&u,&m,1);assert(cursor_id(&u)==XZ_SHELL_ITEM_LANGUAGE);
 push(&u,&m);assert(turn(&u,&m,13)==0);
 f=render(&u,&m);
 int lit=-1;
 for(int r=0;r<ROWS;r++)if(at(f,value_x(),row_top(r))==rgb565(SH_BLUE))lit=r;
 assert(lit>0&&lit<ROWS-1&&at(f,value_x(),row_top(ROWS-1))==rgb565(SH_BLACK));
 assert(((u.stack[1].scroll>>12)&0xfff)==14-(lit+1));
 turn(&u,&m,100);f=render(&u,&m);assert(at(f,value_x(),row_top(lit))==rgb565(SH_BLUE));
 assert(hold(&u,&m)==0);f=render(&u,&m);assert(at(f,value_x(),row_top(0))==rgb565(SH_BLUE));
 /* Information items (one choice) stay in the item list. */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 turn(&u,&m,100);assert(cursor_id(&u)==XZ_SHELL_ITEM_VERSION);
 assert(push(&u,&m)==0&&pane(&u)==1);
 puts("PASS UTILITY encoder: rail, item list with category headers, values, clamps, scroll, category jump, apply and BACK");
}

static void test_utility_touch(void) {
 struct xz_shell u;struct xz_shell_model m;
 model(&m);
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 /* The UTILITY rail entry: a touch takes the encoder to the rail, a second enters the list again. */
 struct xz_shell_rect page0=xz_shell_mods_tab_rect(&u,&m,0);
 assert(tap(&u,&m,page0.x+10,page0.y+10)==0&&u.mods_view.on_tabs);
 assert(tap(&u,&m,page0.x+10,page0.y+10)==0&&!u.mods_view.on_tabs&&pane(&u)==1);
 /* Items: first touch highlights, second confirms into the values; header rows take no touch. */
 assert(tap(&u,&m,item_x()+100,list_row(2))==0&&pane(&u)==1&&cursor_id(&u)==XZ_SHELL_ITEM_QUANTIZE_BEAT);
 assert(tap(&u,&m,item_x()+100,list_row(0))==0&&cursor_id(&u)==XZ_SHELL_ITEM_QUANTIZE_BEAT);   /* the header */
 assert(tap(&u,&m,item_x()+100,list_row(4))==0&&pane(&u)==1&&cursor_id(&u)==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD);
 assert(tap(&u,&m,item_x()+100,list_row(4))==0&&pane(&u)==2);
 /* Values: touch highlights, a second touch on the same row applies on release. */
 assert(tap(&u,&m,value_x()+60,list_row(0))==0&&pane(&u)==2);
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,value_x()+60,list_row(0),0)==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,value_x()+60,list_row(0),0)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD&&out[0].value==0);
 assert(pane(&u)==1);
 /* A drag scrolls instead of confirming. */
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,item_x()+100,list_row(4),0)==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,item_x()+100,list_row(4)-ROW*2-4,0)==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,item_x()+100,list_row(2),0)==0&&pane(&u)==1);
 assert((u.stack[1].scroll&0xfff)==2&&cursor_id(&u)==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD);
 const uint16_t *f=render(&u,&m);
 assert(at(f,item_x(),row_top(2))==rgb565(SH_BLUE));   /* same item, two rows higher */
 /* Dragging the cursor row out of view pulls the cursor onto the first item row in view. */
 turn(&u,&m,-100);assert(cursor_id(&u)==XZ_SHELL_ITEM_LOAD_LOCK&&(u.stack[1].scroll&0xfff)==0);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,item_x()+100,list_row(1),0);
 send(&u,&m,XZ_SHELL_TOUCH_MOVE,item_x()+100,list_row(1)-ROW*2-4,0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,item_x()+100,list_row(1),0)==0&&pane(&u)==1);
 assert((u.stack[1].scroll&0xfff)==2&&cursor_id(&u)==XZ_SHELL_ITEM_QUANTIZE_BEAT);
 /* The description panel under the values takes no touch. */
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,value_x()+60,list_row(ROWS-1),0)==0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,value_x()+60,list_row(ROWS-1),0);
 /* The back button steps focus left, then to the rail, then closes. */
 push(&u,&m);assert(pane(&u)==2);
 struct xz_shell_rect back=R(XZ_SHELL_SR_BACK_BUTTON);
 assert(tap(&u,&m,back.x+10,back.y+10)==0&&pane(&u)==1&&u.depth==2);
 assert(tap(&u,&m,back.x+10,back.y+10)==0&&u.depth==2&&u.mods_view.on_tabs);
 assert(tap(&u,&m,back.x+10,back.y+10)==0&&u.depth==1);
 /* MENU closes UTILITY from any pane (navigation table). */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);push(&u,&m);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.depth==1);
 puts("PASS UTILITY touch: rail entry, highlight then confirm, headers inert, drag scroll, description panel inert, back button");
}

static struct xz_shell_rect slot_of(int q, int k) { return xz_shell_shortcut_slot(q,k); }
static int chip_x(int q, int k, int chip, int chips) {
 struct xz_shell_rect s=slot_of(q,k);
 int w=s.w-24;
 return s.x+12+w*chip/chips+w/chips/2;
}
static int chip_y(int q, int k) { return slot_of(q,k).y+27; }
/* A pixel of the chip's fill, clear of its text and border. */
static uint16_t chip_fill(const uint16_t *f, int q, int k, int chip, int chips) {
 struct xz_shell_rect s=slot_of(q,k);
 int w=s.w-24,cw=(w-3*(chips-1))/chips;
 return at(f,s.x+12+chip*(cw+3)+3,s.y+17);
}
static void test_shortcut(void) {
 struct xz_shell u;struct xz_shell_model m;
 model(&m);
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 /* Focus starts on the first player setting and moves in reading order, skipping DEVICE INFORMATION. */
 const uint16_t *f=render(&u,&m);
 assert(at(f,slot_of(0,0).x+1,slot_of(0,0).y+20)==rgb565(SH_BLUE)&&at(f,slot_of(0,1).x+1,slot_of(0,1).y+20)!=rgb565(SH_BLUE));
 assert(turn(&u,&m,1)==0&&cursor_id(&u)==XZ_SHELL_ITEM_BEAT_JUMP_BEAT);
 turn(&u,&m,3);assert(cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR);
 turn(&u,&m,100);assert(cursor_id(&u)==XZ_SHELL_ITEM_AUTO_CUE_LEVEL);
 f=render(&u,&m);assert(at(f,slot_of(3,3).x+1,slot_of(3,3).y+20)==rgb565(SH_BLUE));
 turn(&u,&m,-100);assert(cursor_id(&u)==XZ_SHELL_ITEM_QUANTIZE_BEAT);
 /* Push on a read-only item hands off; on a writable one it steps to the next value, wrapping. */
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SHORTCUT);
 turn(&u,&m,4);
 assert(push(&u,&m)>=1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_COLOR&&out[0].value==XZ_SHELL_WAVE_STEMS&&out[0].deck==-1);
 /* Chip taps set that value; the current chip writes nothing; the label line only focuses. */
 assert(u.display.wave_color==XZ_SHELL_WAVE_STEMS);
 assert(tap(&u,&m,chip_x(2,1,1,2),chip_y(2,1))==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_POSITION&&out[0].value==1);
 assert(cursor_id(&u)==XZ_SHELL_ITEM_WAVE_POSITION&&u.display.wave_position==XZ_SHELL_POSITION_LEFT);
 assert(tap(&u,&m,chip_x(2,1,1,2),chip_y(2,1))==0);
 /* KEY DISPLAY and ZOOM are shell-owned too; zoom goes out as the ZOOM action. */
 assert(tap(&u,&m,chip_x(2,2,2,3),chip_y(2,2))==1&&out[0].id==XZ_SHELL_ITEM_KEY_DISPLAY&&out[0].value==XZ_SHELL_KEY_ALPHANUMERIC&&u.display.key_display==XZ_SHELL_KEY_ALPHANUMERIC);
 assert(tap(&u,&m,chip_x(2,3,6,7),chip_y(2,3))==1&&out[0].kind==XZ_SHELL_ACT_ZOOM&&out[0].value==6&&u.zoom==6);
 assert(tap(&u,&m,chip_x(3,1,1,2),slot_of(3,1).y+5)==0&&cursor_id(&u)==XZ_SHELL_ITEM_PLAY_MODE);
 assert(tap(&u,&m,chip_x(0,2,0,3),chip_y(0,2))==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD&&out[0].value==0);
 assert(tap(&u,&m,chip_x(0,1,5,8),chip_y(0,1))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
 f=render(&u,&m);assert(chip_fill(f,2,0,3,4)==rgb565(SH_BLUE));                /* STEMS, shell-owned */
 assert(chip_fill(f,0,1,0,8)==rgb565(SH_ROW_ALT)&&chip_fill(f,0,1,5,8)!=rgb565(SH_BLUE)); /* read-only: a flat value box, no chips */
 /* Device information reads only. */
 uint32_t cursor_before=(uint32_t)u.stack[1].cursor;
 assert(tap(&u,&m,slot_of(1,1).x+200,slot_of(1,1).y+20)==0&&(uint32_t)u.stack[1].cursor==cursor_before);
 /* The stepper (too many choices for chips) steps one value and stops at the ends. */
 fxs_item(XZ_SHELL_ITEM_AUTO_CUE_LEVEL)->writable=1;
 struct xz_shell_rect s=slot_of(3,3);
 assert(tap(&u,&m,s.x+20,chip_y(3,3))==0);
 assert(tap(&u,&m,s.x+s.w-20,chip_y(3,3))==1&&out[0].id==XZ_SHELL_ITEM_AUTO_CUE_LEVEL&&out[0].value==1);
 assert(tap(&u,&m,s.x+s.w/2,chip_y(3,3))==0);
 fxs_item(XZ_SHELL_ITEM_AUTO_CUE_LEVEL)->current=8;
 assert(tap(&u,&m,s.x+s.w-20,chip_y(3,3))==0);
 assert(tap(&u,&m,s.x+20,chip_y(3,3))==1&&out[0].value==7);
 /* Without the capability every chip hands off. */
 struct xz_shell_model no_set=m;no_set.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 assert(tap(&u,&no_set,chip_x(3,0,1,2),chip_y(3,0))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SHORTCUT);
 f=render(&u,&no_set);assert(chip_fill(f,2,0,3,4)==rgb565(SH_BLUE)&&chip_fill(f,3,0,0,2)==rgb565(SH_ROW_ALT));
 /* Unknown values: nothing is chosen, push picks the first value. */
 model(&m);
 for(int i=0;i<FXS_COUNT;i++)fxs_table[i].known=0;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 f=render(&u,&m);assert(chip_fill(f,3,0,0,2)==rgb565(SH_ROW_ALT)&&chip_fill(f,3,0,1,2)==rgb565(SH_ROW_ALT));
 turn(&u,&m,8);assert(cursor_id(&u)==XZ_SHELL_ITEM_TIME_MODE);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_TIME_MODE&&out[0].value==0);
 /* Missing items close up; the quadrant keeps its order. */
 model(&m);
 fxs_item(XZ_SHELL_ITEM_AUTO_CUE_LEVEL)->id=XZ_SHELL_ITEM_ID_END+5;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 turn(&u,&m,100);assert(cursor_id(&u)==XZ_SHELL_ITEM_VINYL_SPEED_ADJUST);   /* moved up into slot 3 */
 /* An item the shell can read from the play model fills in when the table lacks it: BEAT JUMP from the
    active deck, read only because the runtime cannot write it. */
 model(&m);
 fxs_item(XZ_SHELL_ITEM_BEAT_JUMP_BEAT)->id=XZ_SHELL_ITEM_ID_END+5;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 turn(&u,&m,1);assert(cursor_id(&u)==XZ_SHELL_ITEM_BEAT_JUMP_BEAT);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SHORTCUT);
 assert(tap(&u,&m,chip_x(0,1,5,8),chip_y(0,1))==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
 /* Closing: the back button and SHORTCUT both return to the screen below. */
 model(&m);
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 assert(tap(&u,&m,10,10)==0&&u.depth==1);
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 assert(key(&u,&m,XZ_SHELL_KEY_SHORTCUT)==0&&u.depth==1);
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==1);
 puts("PASS SHORTCUT: reading-order focus, chip and stepper taps, push cycles, read-only hand-off, close");
}

static void test_bounds_and_determinism(void) {
 struct xz_shell u;struct xz_shell_model m;
 model(&m);
 static char wide[400];memset(wide,'W',sizeof wide-1);
 static const char *wide_choices[3];wide_choices[0]=wide;wide_choices[1]="\xc3\x89t\xc3\xa9 \xff\xfe";wide_choices[2]=wide;
 for(int i=0;i<FXS_COUNT;i++){fxs_table[i].label=wide;}
 fxs_item(XZ_SHELL_ITEM_VERSION)->choices=wide_choices;
 fxs_item(XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD)->choices=wide_choices;
 fxs_item(XZ_SHELL_ITEM_WAVE_COLOR)->choices=wide_choices;
 m.deck[0].title=wide;m.deck[1].tempo_x100=-99999;m.deck[1].bpm_x100=999999;m.deck[0].elapsed_ms=-5;m.deck[0].total_ms=99*3600000;
 struct xz_shell_rect items=R(XZ_SHELL_SR_ITEMS),values=R(XZ_SHELL_SR_VALUES),rail=R(XZ_SHELL_SR_RAIL);
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 for(int step=0;step<4;step++){
  const uint16_t *f=render(&u,&m);
  /* The gutters between panes stay black: no text crosses a pane edge. */
  for(int y=items.y;y<items.y+items.h;y++){
   for(int x=rail.x+rail.w;x<items.x;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
   for(int x=items.x+items.w;x<values.x;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
  }
  push(&u,&m);turn(&u,&m,3);
 }
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT);
 const uint16_t *f=render(&u,&m);
 struct xz_shell_rect q0=xz_shell_shortcut_quadrant(0),q1=xz_shell_shortcut_quadrant(1);
 for(int y=q0.y;y<q0.y+q0.h;y++)for(int x=q0.x+q0.w;x<q1.x;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
 /* Rendering is deterministic and never writes the model or the view. */
 model(&m);
 struct xz_shell_setting_item before[FXS_COUNT];memcpy(before,fxs_table,sizeof before);
 struct xz_shell_model copy=m;
 for(int screen=0;screen<2;screen++){
  open_screen(&u,&m,screen?XZ_SHELL_KEY_MENU_HOLD:XZ_SHELL_KEY_SHORTCUT,screen?XZ_SHELL_UTILITY:XZ_SHELL_SHORTCUT);
  turn(&u,&m,2);push(&u,&m);
  struct xz_shell view=u;
  render(&u,&m);memcpy(buffer_b,buffer_a,sizeof buffer_a);
  render(&u,&m);
  assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a)&&!memcmp(&u,&view,sizeof u));
  tap(&u,&m,400,100);tap(&u,&m,700,300);hold(&u,&m);
  assert(!memcmp(&m,&copy,sizeof m)&&!memcmp(before,fxs_table,sizeof before));
 }
 puts("PASS settings text stays in its pane; deterministic render; model untouched");
}

/* The BACK key steps UTILITY focus one pane left before it closes the screen. */
static void test_back_key_steps_panes(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);xz_shell_init(&u);
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY);
 push(&u,&m);
 assert(pane(&u)==2);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY&&pane(&u)==1);
 key(&u,&m,XZ_SHELL_KEY_BACK);
 assert(xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.mods_view.on_tabs);
 key(&u,&m,XZ_SHELL_KEY_BACK);
 assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 puts("PASS BACK key steps MODS > UTILITY values, items, onto the rail, then closes");
}
int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);   /* a failed assertion still shows which test got there */
 fx_track_build(&track,7);
 test_regions();
 test_empty_and_unknown();
 test_utility_encoder();
 test_utility_touch();
 test_shortcut();
 test_back_key_steps_panes();
 test_bounds_and_determinism();
 puts("PASS settings");
 return 0;
}
