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
static const uint16_t *render_into(uint16_t *buffer, const struct xz_shell *u, const struct xz_shell_model *m) {
 fill(buffer);
 assert(xz_shell_render(u,m,frame(buffer),STRIDE));
 check_guards(buffer);
 return frame(buffer);
}
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
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) { return send(u,m,XZ_SHELL_ENCODER_TURN,0,0,delta); }
static size_t push(struct xz_shell *u, const struct xz_shell_model *m) { return send(u,m,XZ_SHELL_ENCODER_PUSH,0,0,0); }
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
 m->mods=&fxm_model;
}
static int pane(const struct xz_shell *u) { return u->stack[u->depth-1].focus&3; }
static int value_cursor(const struct xz_shell *u) { return (u->stack[u->depth-1].focus>>4)&0xfff; }
static int cursor_id(const struct xz_shell *u) { return u->stack[u->depth-1].cursor-1; }
enum { RAIL_PANE, ITEMS_PANE, VALUES_PANE };
enum { TAB_UTILITY, TAB_STEMS, TAB_LOOK, TAB_JUMP, TAB_PROFILE, TAB_GAMES, TABS };

static void hold_menu(struct xz_shell *u, const struct xz_shell_model *m) {
 assert(key(u,m,XZ_SHELL_KEY_MENU_HOLD)==0);
 assert(xz_shell_screen(u)==XZ_SHELL_UTILITY&&u->mods_view.tab==TAB_UTILITY&&u->mods_view.on_tabs==1);
}
/* From the tab row: open tab t and push into it. */
static void enter_tab(struct xz_shell *u, const struct xz_shell_model *m, int t) {
 assert(u->mods_view.on_tabs==1);
 turn(u,m,-TABS);turn(u,m,t);
 assert(u->mods_view.tab==t);
 assert(push(u,m)==0&&u->mods_view.on_tabs==0);
}
/* Inside an item tab, from the rail: put the item cursor on id (category cat, by rail row). */
static void goto_item(struct xz_shell *u, const struct xz_shell_model *m, int rail_row, int id) {
 assert(pane(u)==RAIL_PANE);
 turn(u,m,-8);turn(u,m,rail_row);
 assert(push(u,m)==0&&pane(u)==ITEMS_PANE);
 turn(u,m,-40);
 for(int i=0;i<40&&cursor_id(u)!=id;i++)turn(u,m,1);
 assert(cursor_id(u)==id);
}

static void test_contract(void) {
 struct xz_shell_model m;model(&m);
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_rect back=xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),rail=xz_shell_settings_rect(XZ_SHELL_SR_RAIL);
 struct xz_shell_rect status=xz_shell_settings_rect(XZ_SHELL_SR_STATUS);
 /* Two groups: STOCK (UTILITY) and MODS; a label before each, so the first tab starts past x 0. */
 int right=0;
 for(int t=0;t<TABS;t++){
  struct xz_shell_rect r=xz_shell_mods_tab_rect(&u,&m,t);
  assert(r.w>=44&&r.h>=44&&r.x>=right&&(t?1:r.x>0));
  if(t==1)assert(r.x>right);   /* the MODS label sits between UTILITY and the first mod tab */
  if(t>1)assert(r.x==right);
  right=r.x+r.w;
  /* Clear of the back button, the header buttons and the panes. */
  assert(r.y>=back.y+back.h&&r.y>=XZ_SHELL_HEADER_HIT_H&&r.y+r.h<=rail.y);
 }
 assert(right==XZ_SHELL_WIDTH&&xz_shell_mods_tab_rect(&u,&m,TABS).w==0&&xz_shell_mods_tab_rect(&u,&m,-1).w==0);
 assert(rail.y+rail.h<=status.y&&status.y+status.h==XZ_SHELL_HEIGHT);
 struct xz_shell_rect b=xz_shell_mods_page_button();
 assert(b.w>=44&&b.h>=44&&b.y>=rail.y&&b.y+b.h<=rail.y+rail.h);
 m.mods=NULL;
 struct xz_shell_rect only=xz_shell_mods_tab_rect(&u,&m,0);
 assert(only.x>0&&only.x+only.w==XZ_SHELL_WIDTH&&xz_shell_mods_tab_rect(&u,&m,1).w==0);
 /* Eleven long tabs do not fit: the row scrolls so the open tab is always whole, and some are out of view. */
 static const struct xz_shell_mods_tab many[11]={
  {"CLASSIC THEMES",XZ_SHELL_TAB_PAGE,NULL,NULL,0,1,NULL},{"MODERN THEMES",XZ_SHELL_TAB_PAGE,NULL,NULL,0,2,NULL},
  {"INTERFACE",XZ_SHELL_TAB_PAGE,NULL,NULL,0,3,NULL},{"APPEARANCE",XZ_SHELL_TAB_PAGE,NULL,NULL,0,4,NULL},
  {"STEMS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,5,NULL},{"BEAT JUMP",XZ_SHELL_TAB_PAGE,NULL,NULL,0,6,NULL},
  {"DJ PROFILE",XZ_SHELL_TAB_PAGE,NULL,NULL,0,7,NULL},{"GAMES",XZ_SHELL_TAB_PAGE,NULL,NULL,0,8,NULL},
  {"PERFORMANCE PADS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,9,NULL},{"ADVANCED",XZ_SHELL_TAB_PAGE,NULL,NULL,0,10,NULL},
  {"ABOUT THIS MOD",XZ_SHELL_TAB_PAGE,NULL,NULL,0,11,NULL},
 };
 static const struct xz_shell_mods_model many_mods={11,many};
 m.mods=&many_mods;
 int hidden_seen=0;
 for(int open=0;open<12;open++){
  u.mods_view.tab=open;
  struct xz_shell_rect r=xz_shell_mods_tab_rect(&u,&m,open);
  assert(r.w>=44&&r.x>=0&&r.x+r.w<=XZ_SHELL_WIDTH);
  for(int t=0;t<12;t++){
   struct xz_shell_rect o=xz_shell_mods_tab_rect(&u,&m,t);
   if(!o.w)hidden_seen=1;else assert(o.x>=0&&o.x+o.w<=XZ_SHELL_WIDTH&&o.w>=44);
  }
  render_into(buffer_a,&u,&m);
 }
 assert(hidden_seen);
 puts("PASS MODS contract: STOCK and MODS tab groups, 44 px tabs, a scrolling row, clear of back, header and panes");
}

static void test_held_menu_and_back_chain(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 hold_menu(&u,&m);
 assert(u.depth==2);
 render_into(buffer_a,&u,&m);
 /* Turn walks the tabs and clamps at both ends; the encoder stays on the row. */
 assert(turn(&u,&m,1)==0&&u.mods_view.tab==TAB_STEMS&&u.mods_view.on_tabs==1);
 turn(&u,&m,99);assert(u.mods_view.tab==TABS-1);
 turn(&u,&m,-99);assert(u.mods_view.tab==TAB_UTILITY);
 /* Push enters UTILITY at the category rail; push walks rail, items, values. */
 assert(push(&u,&m)==0&&u.mods_view.on_tabs==0&&pane(&u)==RAIL_PANE);
 assert(push(&u,&m)==0&&pane(&u)==ITEMS_PANE);
 assert(push(&u,&m)==0&&pane(&u)==VALUES_PANE);
 /* BACK: values, items, rail, tab row, closed. No BACK does anything else on the way. */
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&pane(&u)==ITEMS_PANE&&u.depth==2);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&pane(&u)==RAIL_PANE&&u.depth==2&&!u.mods_view.on_tabs);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.mods_view.on_tabs==1&&u.depth==2&&u.mods_view.tab==TAB_UTILITY);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==1&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* Held MENU always lands on UTILITY with the row focused, whatever tab was last open. */
 hold_menu(&u,&m);enter_tab(&u,&m,TAB_LOOK);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU_HOLD)==0&&u.depth==2&&u.mods_view.tab==TAB_UTILITY&&u.mods_view.on_tabs==1);
 /* Held again on the UTILITY row, MENU closes MODS, so the key never falls through to the stock menu. */
 assert(key(&u,&m,XZ_SHELL_KEY_MENU_HOLD)==0&&u.depth==1);
 hold_menu(&u,&m);
 /* From BROWSE, held MENU stacks MODS over it, and BACK returns there. */
 key(&u,&m,XZ_SHELL_KEY_BACK);assert(u.depth==1);
 key(&u,&m,XZ_SHELL_KEY_BROWSE);
 int under=u.depth;enum xz_shell_screen below=xz_shell_screen(&u);
 hold_menu(&u,&m);assert(u.depth==under+1);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.depth==under&&xz_shell_screen(&u)==below);
 /* A mod tab with one section enters at its items, and BACK from them goes straight to the row. */
 xz_shell_init(&u);hold_menu(&u,&m);
 static const struct xz_shell_setting_item one[]={{7,0,"ONLY",fxs_off_on,2,0,1,1}};
 static const struct xz_shell_settings_model one_model={1,one,NULL};
 static const struct xz_shell_mods_tab one_tab[]={{"ONE",XZ_SHELL_TAB_ITEMS,&one_model,NULL,0,0,NULL}};
 static const struct xz_shell_mods_model one_mods={1,one_tab};
 m.mods=&one_mods;
 enter_tab(&u,&m,1);assert(pane(&u)==ITEMS_PANE);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.mods_view.on_tabs==1&&u.depth==2);
 puts("PASS held MENU lands on MODS > UTILITY; turn picks a tab, push enters, BACK steps panes, row, close");
}

static void test_header_mods(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 int x=XZ_SHELL_MODS_X+XZ_SHELL_MODS_W/2,y=12;
 /* With mod tabs published, MODS opens the shell's own screen on the first mod tab. */
 assert(tap(&u,&m,x,y)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.depth==2);
 assert(u.mods_view.tab==TAB_STEMS&&u.mods_view.on_tabs==1);
 /* Already open on UTILITY deep in a pane: it switches tab in place. */
 key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);push(&u,&m);push(&u,&m);
 assert(u.mods_view.tab==TAB_UTILITY&&!u.mods_view.on_tabs);
 assert(tap(&u,&m,x,y)==0&&u.depth==2&&u.mods_view.tab==TAB_STEMS&&u.mods_view.on_tabs==1&&pane(&u)==RAIL_PANE);
 /* No mods model, an empty one, or no tabs: the runtime's panel opens as before. */
 static const struct xz_shell_mods_model empty={0,fxm_tabs},no_tabs={3,NULL};
 const struct xz_shell_mods_model *none[]={NULL,&empty,&no_tabs};
 for(size_t i=0;i<sizeof none/sizeof *none;i++){
  m.mods=none[i];xz_shell_init(&u);
  size_t n=tap(&u,&m,x,y);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_OPEN_MODS&&out[0].value==0&&u.depth==1);
  /* Held MENU still opens UTILITY, with a single tab. */
  hold_menu(&u,&m);turn(&u,&m,1);assert(u.mods_view.tab==TAB_UTILITY);
  render_into(buffer_a,&u,&m);
 }
 puts("PASS MODS header button opens the first mod tab; without mod tabs it hands MODS to the runtime");
}

static void test_set_requests(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);hold_menu(&u,&m);
 /* UTILITY tab: a writable stock item sets through SET_SETTING with its own id. */
 assert(push(&u,&m)==0);
 goto_item(&u,&m,0,XZ_SHELL_ITEM_AUTO_CUE);
 assert(push(&u,&m)==0&&pane(&u)==VALUES_PANE&&value_cursor(&u)==1);
 turn(&u,&m,-1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_AUTO_CUE&&out[0].value==0);
 /* A mod tab: SET_MOD with the tab index, the runtime's item id and the choice index. */
 while(!u.mods_view.on_tabs)key(&u,&m,XZ_SHELL_KEY_BACK);
 enter_tab(&u,&m,TAB_STEMS);
 goto_item(&u,&m,1,22);
 assert(push(&u,&m)==0&&pane(&u)==VALUES_PANE&&value_cursor(&u)==1);
 turn(&u,&m,2);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_SET_MOD&&out[0].deck==TAB_STEMS-1&&out[0].id==22&&out[0].value==3);
 assert(u.depth==2&&pane(&u)==ITEMS_PANE);
 /* The same through touch: highlight a value, then confirm it. */
 key(&u,&m,XZ_SHELL_KEY_BACK);key(&u,&m,XZ_SHELL_KEY_BACK);
 enter_tab(&u,&m,TAB_JUMP);
 goto_item(&u,&m,1,15);
 struct xz_shell_rect v=xz_shell_settings_rect(XZ_SHELL_SR_VALUES);
 push(&u,&m);
 int row=0-((u.stack[u.depth-1].scroll>>12)&0xfff)+2;   /* choice 2 ("1/2") on screen */
 int ty=v.y+row*XZ_SHELL_SETTINGS_ROW_H+XZ_SHELL_SETTINGS_ROW_H/2;
 assert(tap(&u,&m,v.x+40,ty)==0&&value_cursor(&u)==2);
 assert(tap(&u,&m,v.x+40,ty)==1&&out[0].kind==XZ_SHELL_ACT_SET_MOD&&out[0].deck==TAB_JUMP-1&&out[0].id==15&&out[0].value==2);
 /* Picking the value already set asks for nothing. */
 hold_menu(&u,&m);enter_tab(&u,&m,TAB_LOOK);goto_item(&u,&m,0,3);
 push(&u,&m);assert(value_cursor(&u)==7&&push(&u,&m)==0);
 puts("PASS set requests: UTILITY item -> SET_SETTING, mod item -> SET_MOD {tab, id, choice} by encoder and touch");
}

static void test_unwritable(void) {
 struct xz_shell u,copy;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);hold_menu(&u,&m);push(&u,&m);
 /* UTILITY: the value shows, push hands the screen to the stock deck at that item. */
 goto_item(&u,&m,0,XZ_SHELL_ITEM_HOT_CUE_COLOR);
 assert(push(&u,&m)==0&&pane(&u)==VALUES_PANE);
 const uint16_t *plain=render_into(buffer_a,&u,&m);
 copy=u;
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_UTILITY&&out[0].value==XZ_SHELL_ITEM_HOT_CUE_COLOR+1);
 /* The runtime's note replaces the shell's line in the footer, and only there. */
 static const char *notes[FXS_COUNT];
 notes[fxs_item(XZ_SHELL_ITEM_HOT_CUE_COLOR)-fxs_table]="SET IT IN THE COLOR MENU ON THE STOCK SCREEN";
 fxs_model.notes=notes;
 const uint16_t *noted=render_into(buffer_b,&copy,&m);
 struct xz_shell_rect v=xz_shell_settings_rect(XZ_SHELL_SR_VALUES);
 int footer=v.y+(XZ_SHELL_SETTINGS_ROWS-1)*XZ_SHELL_SETTINGS_ROW_H,differs_in=0,differs_out=0;
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++){
  if(plain[(size_t)y*STRIDE+(size_t)x]==noted[(size_t)y*STRIDE+(size_t)x])continue;
  if(x>=v.x&&y>=footer&&y<footer+XZ_SHELL_SETTINGS_ROW_H)differs_in=1;else differs_out=1;
 }
 assert(differs_in&&!differs_out);
 fxs_model.notes=NULL;
 /* The footer touch hands off too. */
 u=copy;
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,v.x+60,footer+XZ_SHELL_SETTINGS_ROW_H/2,0)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].value==XZ_SHELL_ITEM_HOT_CUE_COLOR+1);
 send(&u,&m,XZ_SHELL_TOUCH_UP,v.x+60,footer+XZ_SHELL_SETTINGS_ROW_H/2,0);
 /* A mod item the runtime cannot write shows its value and note; nothing is asked of anyone. */
 xz_shell_init(&u);hold_menu(&u,&m);enter_tab(&u,&m,TAB_STEMS);
 goto_item(&u,&m,1,37);
 assert(push(&u,&m)==0&&pane(&u)==VALUES_PANE);
 render_into(buffer_a,&u,&m);
 turn(&u,&m,2);
 assert(push(&u,&m)==0&&send(&u,&m,XZ_SHELL_TOUCH_DOWN,v.x+60,footer+XZ_SHELL_SETTINGS_ROW_H/2,0)==0&&u.depth==2);
 puts("PASS unwritable items: value plus how to change it; UTILITY hands off at the item, mod tabs ask nothing");
}

static void test_page_tabs(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);hold_menu(&u,&m);
 turn(&u,&m,TAB_PROFILE);
 render_into(buffer_a,&u,&m);
 /* Push enters the page tab (no action), push again opens the page in the runtime's panel. */
 assert(push(&u,&m)==0&&!u.mods_view.on_tabs);
 render_into(buffer_b,&u,&m);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_OPEN_MODS&&out[0].value==24);
 struct xz_shell_rect b=xz_shell_mods_page_button();
 assert(tap(&u,&m,b.x+b.w/2,b.y+b.h/2)==1&&out[0].kind==XZ_SHELL_ACT_OPEN_MODS&&out[0].value==24);
 /* Turn on a page tab does nothing; BACK returns to the row. */
 assert(turn(&u,&m,1)==0&&u.mods_view.tab==TAB_PROFILE);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.mods_view.on_tabs==1&&u.depth==2);
 turn(&u,&m,1);assert(u.mods_view.tab==TAB_GAMES);
 assert(tap(&u,&m,b.x+b.w/2,b.y+b.h/2)==1&&out[0].kind==XZ_SHELL_ACT_OPEN_MODS&&out[0].value==13);
 puts("PASS page tabs: push or the button opens the page through OPEN_MODS value = page");
}

static void test_tab_touch(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);hold_menu(&u,&m);push(&u,&m);push(&u,&m);
 assert(!u.mods_view.on_tabs&&pane(&u)==ITEMS_PANE);
 for(int t=TABS-1;t>=0;t--){
  struct xz_shell_rect r=xz_shell_mods_tab_rect(&u,&m,t);
  assert(tap(&u,&m,r.x+r.w/2,r.y+r.h/2)==0&&u.mods_view.tab==t&&u.mods_view.on_tabs==1&&u.depth==2);
  render_into(buffer_a,&u,&m);
 }
 /* A changed tab starts at its top; a touch in a pane takes the encoder off the row. */
 assert(pane(&u)==RAIL_PANE&&u.stack[1].scroll==0);
 struct xz_shell_rect rail=xz_shell_settings_rect(XZ_SHELL_SR_RAIL);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,rail.x+10,rail.y+10,0);
 assert(!u.mods_view.on_tabs);
 send(&u,&m,XZ_SHELL_TOUCH_UP,rail.x+10,rail.y+10,0);
 /* The lit rail row was confirmed: items have the encoder. BACK twice reaches the row; the back button closes. */
 assert(pane(&u)==ITEMS_PANE);
 key(&u,&m,XZ_SHELL_KEY_BACK);key(&u,&m,XZ_SHELL_KEY_BACK);
 assert(u.mods_view.on_tabs);
 struct xz_shell_rect back=xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON);
 tap(&u,&m,back.x+back.w/2,back.y+back.h/2);
 assert(u.depth==1);
 puts("PASS tab touch selects the tab, focuses the row; pane touch leaves it; back button closes");
}

static void test_one_value(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 /* WAVEFORM COLOR set on SHORTCUT is the value UTILITY shows: one setting, two doors. */
 assert(key(&u,&m,XZ_SHELL_KEY_SHORTCUT)==0&&xz_shell_screen(&u)==XZ_SHELL_SHORTCUT);
 for(int i=0;i<16&&cursor_id(&u)!=XZ_SHELL_ITEM_WAVE_COLOR;i++)turn(&u,&m,1);
 assert(cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR);
 int before=(int)u.display.wave_color;
 /* A local waveform colour overrides the sticks' colours: USB COLORS steps from ALL to DECK ONLY with it. */
 assert(u.display.usb_colors==XZ_SHELL_USB_COLORS_ALL);
 assert(push(&u,&m)==2&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_COLOR);
 assert(out[1].kind==XZ_SHELL_ACT_SET_SETTING&&out[1].id==XZ_SHELL_ITEM_USB_COLORS&&out[1].value==XZ_SHELL_USB_COLORS_TILE);
 assert(u.display.usb_colors==XZ_SHELL_USB_COLORS_TILE);
 int now=(int)u.display.wave_color;
 assert(now!=before&&out[0].value==now);
 key(&u,&m,XZ_SHELL_KEY_BACK);assert(u.depth==1);
 hold_menu(&u,&m);push(&u,&m);
 goto_item(&u,&m,1,XZ_SHELL_ITEM_WAVE_COLOR);
 assert(push(&u,&m)==0&&value_cursor(&u)==now);
 /* And the other way: UTILITY's change is what SHORTCUT shows next. */
 turn(&u,&m,-1);
 assert(push(&u,&m)==1&&(int)u.display.wave_color==now-1);
 /* The table holds the item once, whatever the runtime listed. */
 int seen=0;
 turn(&u,&m,-40);
 for(int i=0;i<40;i++){if(cursor_id(&u)==XZ_SHELL_ITEM_WAVE_COLOR)seen++;int c=cursor_id(&u);turn(&u,&m,1);if(cursor_id(&u)==c)break;}
 assert(seen==1);
 puts("PASS SHORTCUT and UTILITY show one value per setting");
}

static void test_probe_and_wild(void) {
 struct xz_shell u,probe;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);hold_menu(&u,&m);
 /* A probe copy takes keys without touching the original: tab state lives in the struct. */
 probe=u;
 turn(&probe,&m,TAB_JUMP);push(&probe,&m);
 assert(probe.mods_view.tab==TAB_JUMP&&!probe.mods_view.on_tabs);
 assert(u.mods_view.tab==TAB_UTILITY&&u.mods_view.on_tabs==1);
 const uint16_t *a=render_into(buffer_a,&u,&m);
 render_into(buffer_b,&probe,&m);
 struct xz_shell again=u;
 const uint16_t *c=render_into(buffer_b,&again,&m);
 assert(memcmp(a,c,sizeof(uint16_t)*STRIDE*XZ_SHELL_HEIGHT)==0);
 /* Wild tabs: NULL labels, items and sections, a section count past the names, zero counts. */
 static const char *const holes[]={NULL,"ON"};
 static const struct xz_shell_setting_item odd[]={
  {1,(enum xz_shell_setting_category)99,"BAD SECTION",holes,2,0,1,1},
  {2,5,NULL,NULL,9,4,1,1},
  {3,0,"PAST END",holes,2,7,1,0},
  {4,0,"UNKNOWN",holes,2,0,0,1},
 };
 static const struct xz_shell_settings_model odd_model={4,odd,NULL},zero_model={0,odd,NULL},null_items={3,NULL,NULL};
 static const char *const few[]={"ONE",NULL};
 static const struct xz_shell_mods_tab wild[]={
  {NULL,XZ_SHELL_TAB_ITEMS,&odd_model,few,2,0,NULL},
  {"NO ITEMS",XZ_SHELL_TAB_ITEMS,NULL,NULL,0,0,NULL},
  {"ZERO",XZ_SHELL_TAB_ITEMS,&zero_model,NULL,0,0,NULL},
  {"NULL ITEMS",XZ_SHELL_TAB_ITEMS,&null_items,NULL,0,0,NULL},
  {NULL,XZ_SHELL_TAB_PAGE,NULL,NULL,0,-5,NULL},
  {"A VERY LONG TAB LABEL THAT CANNOT FIT",(enum xz_shell_mods_tab_kind)9,NULL,NULL,0,0,"A SUMMARY FAR TOO LONG FOR ITS LINE ON THIS SMALL SCREEN, IT MUST BE CUT"},
 };
 static const struct xz_shell_mods_model wild_mods={sizeof wild/sizeof *wild,wild};
 m.mods=&wild_mods;
 for(int t=0;t<(int)(sizeof wild/sizeof *wild)+1;t++){
  xz_shell_init(&u);hold_menu(&u,&m);turn(&u,&m,t);
  for(int step=0;step<8;step++){
   render_into(buffer_a,&u,&m);
   push(&u,&m);turn(&u,&m,1);
   send(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
   if(step==4){key(&u,&m,XZ_SHELL_KEY_BACK);key(&u,&m,XZ_SHELL_KEY_BACK);}
  }
  render_into(buffer_a,&u,&m);
  while(u.depth>1)key(&u,&m,XZ_SHELL_KEY_BACK);
 }
 /* A mods model that shrinks under an open tab falls back to UTILITY. */
 m.mods=&fxm_model;
 xz_shell_init(&u);hold_menu(&u,&m);enter_tab(&u,&m,TAB_GAMES);
 m.mods=NULL;
 render_into(buffer_a,&u,&m);
 push(&u,&m);turn(&u,&m,1);render_into(buffer_a,&u,&m);
 while(u.depth>1)key(&u,&m,XZ_SHELL_KEY_BACK);
 puts("PASS probe copies keep tab state apart; wild and shrinking mod tabs render and drive safely");
}

int main(void) {
 fx_track_build(&track,7);
 test_contract();
 test_held_menu_and_back_chain();
 test_header_mods();
 test_set_requests();
 test_unwritable();
 test_page_tabs();
 test_tab_touch();
 test_one_value();
 test_probe_and_wild();
 puts("PASS mods");
 return 0;
}
