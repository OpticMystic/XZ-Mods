/* Renders every MODS panel page the MODERN MODS screen shows, through the real page adapter
   (ui/ui_menu.c) and the shell's MENU tab, to PPM files in argv[1]. Host only. */
#include "fixtures.h"
#include "settings_fixtures.h"
#include "menu_fixtures.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static struct xz_shell_menu menu;
static struct xz_ui_model ui_model;

static void save(const char *name) {
 char path[1024];FILE *f;
 snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
/* The panel on a page reached the way the DJ reaches it: links pushed from a top page. */
static void reach(struct xz_ui *u, const int *chain, int n) {
 xz_ui_init(u);xz_ui_menu_open(u,(enum xz_ui_page)chain[0]);
 for(int i=1;i<n;i++){
  int old=u->page;xz_ui_navigate(u,(enum xz_ui_page)chain[i]);
  if(xz_ui_page_is_top(chain[i]))u->history[u->history_count++]=(enum xz_ui_page)old;
 }
}
static struct xz_shell_model shell_model;
static void open_shell(struct xz_shell *s, const struct xz_ui *u, int rail) {
 xz_ui_menu_build(u,&ui_model,&menu);mfx_look_values(&menu);
 xz_shell_init(s);
 xz_shell_mods_show(s,&shell_model,mfx_tab_of(menu.root));
 if(rail)s->mods_view.on_tabs=1;
}
static void shot(const struct xz_shell *s, const char *name) {
 if(!xz_shell_render(s,&shell_model,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 save(name);
}
static void turn(struct xz_shell *s, int delta) { xz_shell_input(s,&shell_model,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1},out); }
static void push(struct xz_shell *s) { xz_shell_input(s,&shell_model,&(struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1},out); }
/* Cursor on the row whose label starts with label. */
static void cursor_to(struct xz_shell *s, const char *label) {
 for(int i=0;i<8;i++)turn(s,-8);
 for(int i=0;i<menu.row_count;i++){
  const char *have=NULL;
  for(int r=0;r<menu.row_count;r++)if(menu.row[r].key==s->stack[s->depth-1].cursor)have=menu.row[r].label;
  if(have&&!strncmp(have,label,strlen(label)))return;
  turn(s,1);
 }
 fprintf(stderr,"no row %s on %s\n",label,menu.title);exit(1);
}
static void page(const char *name, const int *chain, int n, const char *row, int values) {
 struct xz_ui u;struct xz_shell s;
 reach(&u,chain,n);
 open_shell(&s,&u,0);
 if(row)cursor_to(&s,row);
 if(values)push(&s);
 shot(&s,name);
}
#define CHAIN(...) (const int[]){__VA_ARGS__},(int)(sizeof (const int[]){__VA_ARGS__}/sizeof(int))

int main(int argc, char **argv) {
 if(argc<2){fputs("usage: preview_menu OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 fx_model_full(&shell_model,&track);
 shell_model.settings_screen=fxs_reset();
 shell_model.mods=&mfx_mods;shell_model.menu=&menu;
 mfx_model(&ui_model);
 (void)fxs_item;
 struct xz_ui u;struct xz_shell s;

 /* HOME: on the rail as MENU opens it, then inside; decks 3 and 4 waiting for the cable */
 reach(&u,CHAIN(XZ_UI_HOME));open_shell(&s,&u,1);shot(&s,"menu-home-rail");
 page("menu-home",CHAIN(XZ_UI_HOME),NULL,0);
 {
  struct xz_ui_model saved=ui_model;
  ui_model.deck_view=2;ui_model.route.state=XZ_UI_ROUTE_NOT_DETECTED;
  ui_model.enabled=0;for(int d=0;d<2;d++){ui_model.deck[d].ready&=~(uint32_t)XZ_UI_STEM;ui_model.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}
  mfx_tabs[4].attention=1;
  page("menu-home-needs-you",CHAIN(XZ_UI_HOME),"4 DECKS",0);
  page("menu-home-set-up",CHAIN(XZ_UI_HOME),"SET UP",0);
  mfx_tabs[4].attention=0;
  ui_model=saved;
 }
 /* PLAY and its pages */
 page("menu-play",CHAIN(XZ_UI_PLAY),NULL,0);
 page("menu-play-button",CHAIN(XZ_UI_PLAY),"BUTTON NEXT TO LINK",1);
 page("menu-beat-jump",CHAIN(XZ_UI_PLAY,XZ_UI_BEAT_JUMP),NULL,0);
 {
  struct xz_ui_model saved=ui_model;
  ui_model.jump.enabled=1;ui_model.jump_page[1]=1;ui_model.route.state=XZ_UI_ROUTE_READY;
  reach(&u,CHAIN(XZ_UI_PLAY,XZ_UI_BEAT_JUMP));u.jump_edit_pair=1;open_shell(&s,&u,0);cursor_to(&s,"PADS C / D");shot(&s,"menu-beat-jump-pair");
  u.adjusting=1;u.focus_kind=XZ_UI_JUMP_PAIR;u.focus_index=2;u.focus_deck=-1;ui_model.jump.sizes[1]=8;
  open_shell(&s,&u,0);cursor_to(&s,"PADS C / D");shot(&s,"menu-beat-jump-adjusting");
  ui_model=saved;
 }
 page("menu-keys",CHAIN(XZ_UI_PLAY,XZ_UI_KEY_SETUP),NULL,0);
 reach(&u,CHAIN(XZ_UI_PLAY,XZ_UI_FEATURE_SETUP));u.feature_cap=XZ_UI_PREVIEW;u.feature_step=0;open_shell(&s,&u,0);shot(&s,"menu-feature-setup-detect");
 u.feature_cap=XZ_UI_GATE;u.feature_step=3;open_shell(&s,&u,0);shot(&s,"menu-feature-setup-test");
 page("menu-xpad-no-engine",CHAIN(XZ_UI_PLAY,XZ_UI_XPAD),NULL,0);
 page("menu-ram-tracks",CHAIN(XZ_UI_PLAY,XZ_UI_RAM_SETUP),NULL,0);
 {
  struct xz_ui_model saved=ui_model;
  for(int d=0;d<2;d++)ui_model.deck[d].ready|=XZ_UI_SAMPLE|XZ_UI_GROOVE|XZ_UI_HOTCUE;
  ui_model.deck[0].sample_loaded=0x0f;ui_model.deck[0].groove_assigned=0x05;ui_model.deck[0].groove_loaded=0x05;
  page("menu-xpad",CHAIN(XZ_UI_PLAY,XZ_UI_XPAD),"LOOP STRIP",0);
  page("menu-groove-pads",CHAIN(XZ_UI_STEM_SECTION,XZ_UI_STEMS),"PAD A",0);
  ui_model=saved;
 }
 /* STEMS */
 page("menu-stems",CHAIN(XZ_UI_STEM_SECTION),NULL,0);
 page("menu-stems-buttons",CHAIN(XZ_UI_STEM_SECTION),"STEM BUTTONS ON SCREEN",1);
 page("menu-stem-pads",CHAIN(XZ_UI_STEM_SECTION,XZ_UI_STEM_PADS),NULL,0);
 page("menu-stem-pads-order",CHAIN(XZ_UI_STEM_SECTION,XZ_UI_STEM_PADS),"ORDER",0);
 page("menu-stem-colors",CHAIN(XZ_UI_STEM_SECTION,XZ_UI_STEM_COLORS),"DRUMS: BLUE",0);
 page("menu-pc-stems",CHAIN(XZ_UI_STEM_SECTION,XZ_UI_PC_STEMS),NULL,0);
 {
  struct xz_ui_model saved=ui_model;
  ui_model.enabled=0;for(int d=0;d<2;d++){ui_model.deck[d].ready&=~(uint32_t)XZ_UI_STEM;ui_model.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}
  page("menu-stems-no-engine",CHAIN(XZ_UI_STEM_SECTION),"SET UP USB STEMS",0);
  reach(&u,CHAIN(XZ_UI_STEM_SECTION,XZ_UI_LOCAL_STEM_SETUP));open_shell(&s,&u,0);shot(&s,"menu-usb-stems-setup-engine");
  ui_model=saved;
 }
 page("menu-usb-stems-setup",CHAIN(XZ_UI_STEM_SECTION,XZ_UI_LOCAL_STEM_SETUP),NULL,0);
 /* SCREEN */
 ui_model.route.state=XZ_UI_ROUTE_READY;ui_model.deck_view=2;
 page("menu-screen",CHAIN(XZ_UI_SCREEN),NULL,0);
 page("menu-screen-deck-view",CHAIN(XZ_UI_SCREEN),"DECK VIEW",1);
 page("menu-screen-interface",CHAIN(XZ_UI_SCREEN),"INTERFACE",1);
 page("menu-screen-waveforms",CHAIN(XZ_UI_SCREEN),"WAVEFORM COLOURS",1);
 page("menu-screen-keys",CHAIN(XZ_UI_SCREEN),"KEY NOTATION",1);
 page("menu-screen-top-bar",CHAIN(XZ_UI_SCREEN),"MODE LAMPS",0);
 page("menu-metrics-setup",CHAIN(XZ_UI_SCREEN,XZ_UI_METRICS_SETUP),NULL,0);
 /* 4 DECKS in its states */
 {
  static const int states[]={XZ_UI_ROUTE_READY,XZ_UI_ROUTE_NOT_DETECTED,XZ_UI_ROUTE_SCANNING,XZ_UI_ROUTE_WAITING_NATIVE_STOP,XZ_UI_ROUTE_FAILED,XZ_UI_ROUTE_NO_ENGINE};
  static const char *const names[]={"menu-four-decks-ready","menu-four-decks-not-detected","menu-four-decks-scanning","menu-four-decks-waiting","menu-four-decks-failed","menu-four-decks-no-engine"};
  struct xz_ui_model saved=ui_model;
  for(size_t i=0;i<sizeof states/sizeof *states;i++){
   ui_model.route.state=states[i];ui_model.route.fail_part=states[i]==XZ_UI_ROUTE_FAILED?XZ_UI_ROUTE_PART_PORT:XZ_UI_ROUTE_PART_NONE;
   snprintf(ui_model.route.reason,sizeof ui_model.route.reason,"%s",states[i]==XZ_UI_ROUTE_FAILED?"USB 1 gave no audio device. Plug the cable into USB 1 on top.":"");
   mfx_tabs[4].attention=xz_ui_four_decks_attention(&ui_model);
   page(names[i],CHAIN(XZ_UI_FOUR_DECKS),i==0?"STEMS":NULL,0);
  }
  mfx_tabs[4].attention=0;
  ui_model=saved;
 }
 /* CONNECT and EXTRAS */
 page("menu-connect",CHAIN(XZ_UI_CONNECT),NULL,0);
 page("menu-connect-profile-values",CHAIN(XZ_UI_CONNECT),"ACTIVE DJ PROFILE",1);
 page("menu-vjtools",CHAIN(XZ_UI_CONNECT,XZ_UI_CONNECTION),NULL,0);
 page("menu-updates",CHAIN(XZ_UI_CONNECT,XZ_UI_UPDATES),NULL,0);
 page("menu-update-setup",CHAIN(XZ_UI_CONNECT,XZ_UI_UPDATES,XZ_UI_UPDATE_SETUP),NULL,0);
 page("menu-extras",CHAIN(XZ_UI_EXTRAS),NULL,0);
 page("menu-games",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES),NULL,0);
 page("menu-doom",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_DOOM),NULL,0);
 page("menu-doom-setup",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_DOOM,XZ_UI_DOOM_SETUP),NULL,0);
 page("menu-doom-files",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_DOOM_FILES),NULL,0);
 page("menu-myhouse",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_MYHOUSE_SETUP),NULL,0);
 page("menu-apps",CHAIN(XZ_UI_EXTRAS,XZ_UI_APPS),NULL,0);
 reach(&u,CHAIN(XZ_UI_EXTRAS,XZ_UI_APPS,XZ_UI_APPS_SETUP));u.sdk_step=XZ_SDK_UI_FOLDERS;open_shell(&s,&u,0);shot(&s,"menu-apps-setup");
 page("menu-dj-profile",CHAIN(XZ_UI_CONNECT,XZ_UI_PROFILE_SETUP),NULL,0);
 /* the profile editor: the name on the keyboard, the logo list, the review */
 reach(&u,CHAIN(XZ_UI_CONNECT,XZ_UI_PROFILE_SETUP));
 {
  struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&ui_model,w);
  for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_PROFILE_EDIT_OPEN&&w[i].index>=0){
   struct xz_shell_menu_target t={(int16_t)w[i].kind,(int16_t)w[i].index,(int8_t)w[i].deck,XZ_UI_MENU_TOUCH,0};
   struct xz_ui_action many[XZ_UI_MENU_ACTIONS];xz_ui_menu_run(&u,&ui_model,&t,many);break;
  }
 }
 open_shell(&s,&u,0);shot(&s,"menu-profile-review");
 u.edit_step=XZ_UI_EDIT_NAME;open_shell(&s,&u,0);turn(&s,13);shot(&s,"menu-profile-name");
 u.edit_step=XZ_UI_EDIT_LOGO;u.edit_logo=0;open_shell(&s,&u,0);shot(&s,"menu-profile-logo");
 return 0;
}
