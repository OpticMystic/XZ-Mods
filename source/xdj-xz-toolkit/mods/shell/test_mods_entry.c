#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* MODS is the runtime's panel, one menu for both interfaces (ui/ui.h). Every way into MODS from the MODERN
   shell asks the runtime to open it (XZ_SHELL_ACT_OPEN_MODS, value -1: the page it was last on): the MODS
   header button, short MENU on PLAY, SOURCE, INFO and SHORTCUT, and the MODS key. Held MENU keeps opening
   UTILITY, the player's own settings, as on the stock player; UTILITY's rail lists the MODS sections and opens
   the panel on one with a single touch, or a turn and a push. The shell draws no MODS page of its own. */
#include "fixtures.h"
#include "settings_fixtures.h"
#include "browse_fixtures.h"
#include "shell_internal.h"
#include "../ui/ui.h"
#include "../ui/menu_key.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static struct xz_shell_model sm;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static uint16_t frame[800*480];
/* The rail shell_runtime.c publishes: UTILITY, then one entry per MODS section. */
static const struct xz_shell_mods_tab tabs[XZ_UI_SECTIONS]={
 {"HOME",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_HOME,"What MODS can do, and anything that needs you.",0},
 {"PLAY",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_PLAY,"Beat jump pads, cue tricks, key sync, the sampler and RAM tracks.",0},
 {"STEMS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_STEM_SECTION,"Mute vocals, harmonics and drums from the pads or the screen.",0},
 {"SCREEN",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_SCREEN,"Interface, deck view, waveform colours and how keys are written.",0},
 {"4 DECKS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_FOUR_DECKS,"Play decks 3 and 4 over the loopback cable.",1},
 {"CONNECT",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_CONNECT,"DJ profile, VJ.Tools, network and updates.",0},
 {"EXTRAS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_EXTRAS,"Games, USB apps, report an issue and about.",0}};
static const struct xz_shell_mods_model mods={XZ_UI_SECTIONS,tabs};

static size_t send(struct xz_shell *s, struct xz_shell_event e) { memset(out,0,sizeof out);return xz_shell_input(s,&sm,&e,out); }
static size_t key(struct xz_shell *s, enum xz_shell_key k) { return send(s,(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1}); }
static size_t turn(struct xz_shell *s, int d) { return send(s,(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,d,0,-1}); }
static size_t push(struct xz_shell *s) { return send(s,(struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1}); }
static size_t tap(struct xz_shell *s, int x, int y) {
 size_t n=send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1});
 struct xz_shell_action keep[XZ_SHELL_ACTIONS];memcpy(keep,out,sizeof keep);
 send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1});memcpy(out,keep,sizeof keep);
 return n;
}
static void draw(struct xz_shell *s) { assert(xz_shell_render_frame(s,&sm,frame,800)); }
static int opens(size_t n, int page) { return n==1&&out[0].kind==XZ_SHELL_ACT_OPEN_MODS&&out[0].value==page; }

static void every_entry_opens_the_panel(void) {
 struct xz_shell s;
 /* the MODS header button, on PLAY: the shell stays where it is under the panel */
 xz_shell_init(&s);draw(&s);
 assert(opens(tap(&s,XZ_SHELL_MODS_X+XZ_SHELL_MODS_W/2,10),-1)&&xz_shell_screen(&s)==XZ_SHELL_PLAY&&s.depth==1);
 /* short MENU on PLAY, SOURCE, INFO and SHORTCUT, and the MODS key: one press each */
 static const enum xz_shell_key screens[4]={XZ_SHELL_KEY_COUNT,XZ_SHELL_KEY_SOURCE,XZ_SHELL_KEY_INFO,XZ_SHELL_KEY_SHORTCUT};
 for(int i=0;i<4;i++){
  xz_shell_init(&s);if(screens[i]!=XZ_SHELL_KEY_COUNT)key(&s,screens[i]);
  enum xz_shell_screen under=xz_shell_screen(&s);int depth=s.depth;
  assert(opens(key(&s,XZ_SHELL_KEY_MENU),-1)&&xz_shell_screen(&s)==under&&s.depth==depth);
  assert(opens(key(&s,XZ_SHELL_KEY_MODS),-1)&&xz_shell_screen(&s)==under);
 }
 /* the list screens keep their own MENU panel */
 static struct bx_library library;static struct xz_shell_browse_model b;bx_build(&library);bx_tracks(&b,&library,XZ_SHELL_BROWSE);
 sm.browse=&b;sm.capabilities|=XZ_SHELL_CAP_BROWSE;
 xz_shell_init(&s);key(&s,XZ_SHELL_KEY_BROWSE);assert(xz_shell_screen(&s)==XZ_SHELL_BROWSE);
 assert(key(&s,XZ_SHELL_KEY_MENU)==0&&s.panel.kind!=XZ_SHELL_PANEL_NONE);
 sm.browse=NULL;
 puts("PASS MODS: the header button, MENU on PLAY / SOURCE / INFO / SHORTCUT and the MODS key each open the one menu in one press");
}

static void utility_rail_opens_sections(void) {
 struct xz_shell s;
 /* held MENU: UTILITY, the player's own settings, with the encoder on its rail */
 xz_shell_init(&s);
 assert(key(&s,XZ_SHELL_KEY_MENU_HOLD)==0&&xz_shell_screen(&s)==XZ_SHELL_UTILITY&&s.mods_view.tab==0&&s.mods_view.on_tabs);
 draw(&s);
 /* one touch on a section's rail cell opens the panel on that section */
 for(int t=1;t<=XZ_UI_SECTIONS;t++){
  struct xz_shell_rect r=xz_shell_mods_tab_rect(&s,&sm,t);
  if(!r.w)continue;
  assert(opens(tap(&s,r.x+r.w/2,r.y+r.h/2),tabs[t-1].page)&&xz_shell_screen(&s)==XZ_SHELL_UTILITY);
 }
 /* a turn shows a section's name and what it holds; a push opens it */
 xz_shell_init(&s);key(&s,XZ_SHELL_KEY_MENU_HOLD);
 for(int t=1;t<=XZ_UI_SECTIONS;t++){
  assert(turn(&s,1)==0&&s.mods_view.tab==t&&s.mods_view.on_tabs);draw(&s);
  assert(opens(push(&s),tabs[t-1].page));
 }
 /* UTILITY itself still works its items: a push from its rail enters them */
 xz_shell_init(&s);key(&s,XZ_SHELL_KEY_MENU_HOLD);
 assert(push(&s)==0&&!s.mods_view.on_tabs&&xz_shell_screen(&s)==XZ_SHELL_UTILITY);
 /* the MODS key on UTILITY closes it, as before; held MENU there brings its rail back to UTILITY */
 xz_shell_init(&s);key(&s,XZ_SHELL_KEY_MENU_HOLD);
 assert(key(&s,XZ_SHELL_KEY_MODS)==0&&xz_shell_screen(&s)==XZ_SHELL_PLAY);
 xz_shell_init(&s);key(&s,XZ_SHELL_KEY_MENU_HOLD);turn(&s,2);
 assert(key(&s,XZ_SHELL_KEY_MENU_HOLD)==0&&xz_shell_screen(&s)==XZ_SHELL_UTILITY&&s.mods_view.tab==0&&s.mods_view.on_tabs);
 puts("PASS UTILITY: held MENU opens it; its rail opens each MODS section in one touch, or a turn and a push");
}

/* A short MENU follows ui/menu_key.h: stock first. Each row of the table the shell can be on. */
static void menu_key_rows(void) {
 struct xz_shell s;
 static struct bx_library library;static struct xz_shell_browse_model b;bx_build(&library);bx_tracks(&b,&library,XZ_SHELL_BROWSE);
 sm.browse=&b;sm.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH;
 /* track lists and TAG LIST: their own menu (sort menu, LIST MENU, the track menu), never MODS */
 static const enum xz_shell_key lists[3]={XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_SEARCH};
 static const int places[3]={XZ_MENU_AT_TRACK_LIST,XZ_MENU_AT_TAG_LIST,XZ_MENU_AT_TRACK_LIST};
 for(int i=0;i<3;i++){
  xz_shell_init(&s);key(&s,lists[i]);
  assert(xz_menu_rule(places[i])->job==XZ_MENU_JOB_STOCK);
  size_t n=key(&s,XZ_SHELL_KEY_MENU);
  assert(n==0&&s.panel.kind!=XZ_SHELL_PANEL_NONE);
  /* a panel open over the list: MENU is that panel's (it closes), never MODS */
  assert(xz_menu_rule(XZ_MENU_AT_STOCK_MENU)->job==XZ_MENU_JOB_STOCK);
  assert(key(&s,XZ_SHELL_KEY_MENU)==0&&s.panel.kind==XZ_SHELL_PANEL_NONE);
 }
 /* a popup open over PLAY: MENU closes it, never MODS */
 xz_shell_init(&s);s.popup.kind=XZ_SHELL_POPUP_RECENT;
 assert(key(&s,XZ_SHELL_KEY_MENU)==0&&s.popup.kind==XZ_SHELL_POPUP_NONE);
 /* GRID ADJUST: the beat grid commands are on the screen; MENU does not open MODS over them */
 xz_shell_init(&s);s.grid_mode=1;assert(xz_menu_rule(XZ_MENU_AT_GRID_ADJUST)->job==XZ_MENU_JOB_STOCK);
 assert(key(&s,XZ_SHELL_KEY_MENU)==0&&s.grid_mode&&xz_shell_screen(&s)==XZ_SHELL_PLAY);
 /* UTILITY: MENU closes it (the player's own menu), never MODS */
 xz_shell_init(&s);key(&s,XZ_SHELL_KEY_MENU_HOLD);
 assert(key(&s,XZ_SHELL_KEY_MENU)==0&&xz_shell_screen(&s)==XZ_SHELL_PLAY);
 /* PLAY, SOURCE, INFO and SHORTCUT have no stock MENU job: MODS (every_entry_opens_the_panel) */
 assert(xz_menu_rule(XZ_MENU_AT_PLAY)->job==XZ_MENU_JOB_MODS&&xz_menu_rule(XZ_MENU_AT_SOURCE)->job==XZ_MENU_JOB_MODS&&
        xz_menu_rule(XZ_MENU_AT_INFO)->job==XZ_MENU_JOB_MODS&&xz_menu_rule(XZ_MENU_AT_SHORTCUT)->job==XZ_MENU_JOB_MODS);
 sm.browse=NULL;sm.capabilities&=~(uint32_t)(XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH);
 puts("PASS MENU table on the shell: lists, TAG LIST, open panels and popups, GRID ADJUST and UTILITY keep their own MENU; PLAY, SOURCE, INFO and SHORTCUT open MODS");
}
/* Kept from the retired MENU-tab parity suite: the list screens' own one-press paths. */
static void physical_load_parity(void) {
 static struct bx_library library;static struct fx_track t;
 bx_build(&library);fx_track_build(&t,1);
 for(int count=2;count<=4;count+=2)for(int deck=0;deck<count;deck++){
  struct xz_shell s;struct xz_shell_model m;struct xz_shell_browse_model b;
  fx_model_full(&m,&t);bx_tracks(&b,&library,XZ_SHELL_BROWSE);b.media_generation=7;
  m.browse=&b;m.deck_count=count;m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;
  xz_shell_init(&s);xz_shell_browse_reset();
  struct xz_shell_action a[XZ_SHELL_ACTIONS];
  struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};xz_shell_input(&s,&m,&e,a);
  e.kind=XZ_SHELL_ENCODER_PUSH;xz_shell_input(&s,&m,&e,a);
  e.kind=XZ_SHELL_ENCODER_TURN;e.delta=2;xz_shell_input(&s,&m,&e,a);
  assert(s.stack[s.depth-1].cursor==2);
  e=(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_LOAD,deck};
  size_t n=xz_shell_input(&s,&m,&e,a);
  assert(n==1&&a[0].kind==XZ_SHELL_ACT_LOAD&&a[0].deck==deck&&a[0].id==(int)b.rows[2].id);
  assert(!s.load_choice.mode&&xz_shell_screen(&s)==XZ_SHELL_PLAY);
 }
 puts("PASS one physical LOAD loads the addressed deck, no chooser");
}
static void pending_navigation(void) {
 static struct bx_library library;static struct fx_track t;bx_build(&library);fx_track_build(&t,1);
 static const int keys[]={XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_TRACK_FILTER,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_BACK};
 for(unsigned k=0;k<sizeof keys/sizeof *keys;k++){
  struct xz_shell s;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action a[XZ_SHELL_ACTIONS];
  fx_model_full(&m,&t);bx_tracks(&b,&library,XZ_SHELL_BROWSE);b.media_generation=7;m.browse=&b;m.deck_count=4;
  m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_FILTER|XZ_SHELL_CAP_TAG_LIST;
  xz_shell_init(&s);xz_shell_browse_reset();struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};xz_shell_input(&s,&m,&e,a);
  e.kind=XZ_SHELL_ENCODER_PUSH;xz_shell_input(&s,&m,&e,a);xz_shell_input(&s,&m,&e,a);
  assert(s.load_choice.mode!=0);
  e.kind=XZ_SHELL_KEY;e.key=keys[k];xz_shell_input(&s,&m,&e,a);
  assert(!s.load_choice.mode);
  assert(k==0?xz_shell_screen(&s)==XZ_SHELL_PLAY:k==1?s.panel.kind==XZ_SHELL_PANEL_FILTER:k==2?xz_shell_screen(&s)==XZ_SHELL_TAG_LIST:xz_shell_screen(&s)==XZ_SHELL_BROWSE);
 }
 puts("PASS navigation dismisses the load chooser and still acts in the same press");
}
static void mods_over_a_list(void) {
 /* MODS from BROWSE asks for the panel and leaves BROWSE where it was, so closing MODS returns there */
 static struct bx_library library;static struct xz_shell_browse_model b;bx_build(&library);bx_tracks(&b,&library,XZ_SHELL_BROWSE);
 struct xz_shell s;xz_shell_init(&s);sm.browse=&b;sm.capabilities|=XZ_SHELL_CAP_BROWSE;
 key(&s,XZ_SHELL_KEY_BROWSE);int prior=s.depth;
 assert(opens(key(&s,XZ_SHELL_KEY_MODS),-1)&&xz_shell_screen(&s)==XZ_SHELL_BROWSE&&s.depth==prior);
 sm.browse=NULL;
 puts("PASS MODS over BROWSE: the list stays under the menu");
}

int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);
 fx_track_build(&track,7);fx_model_full(&sm,&track);sm.settings_screen=fxs_reset();sm.mods=&mods;
 (void)&bx_folders;(void)&fxs_item;
 every_entry_opens_the_panel();
 utility_rail_opens_sections();
 menu_key_rows();
 physical_load_parity();
 pending_navigation();
 mods_over_a_list();
 return 0;
}
