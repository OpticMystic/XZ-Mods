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
 xz_ui_menu_build(u,&ui_model,&menu);
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
 turn(s,-99);
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

 /* GENERAL: on the rail as MENU opens it, then inside, then a switch's values */
 reach(&u,CHAIN(XZ_UI_GENERAL));open_shell(&s,&u,1);shot(&s,"menu-general-rail");
 page("menu-general",CHAIN(XZ_UI_GENERAL),NULL,0);
 page("menu-general-gate-values",CHAIN(XZ_UI_GENERAL),"GATE CUE",0);
 page("menu-general-link",CHAIN(XZ_UI_GENERAL),"STEM SETTINGS",0);
 /* no stem engine: USB STEMS is one button into its setup */
 {
  struct xz_ui_model saved=ui_model;
  ui_model.enabled=0;for(int d=0;d<2;d++){ui_model.deck[d].ready&=~(uint32_t)XZ_UI_STEM;ui_model.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}
  page("menu-general-no-engine",CHAIN(XZ_UI_GENERAL),"SET UP USB STEMS",0);
  ui_model.deck[0].usb_reason=XZ_UI_USB_WHY_ENGINE;
  reach(&u,CHAIN(XZ_UI_GENERAL,XZ_UI_LOCAL_STEM_SETUP));open_shell(&s,&u,0);shot(&s,"menu-usb-stems-setup-engine");
  ui_model=saved;
 }
 reach(&u,CHAIN(XZ_UI_GENERAL,XZ_UI_FEATURE_SETUP));u.feature_cap=XZ_UI_PREVIEW;u.feature_step=0;open_shell(&s,&u,0);shot(&s,"menu-feature-setup-detect");
 u.feature_cap=XZ_UI_GATE;u.feature_step=3;open_shell(&s,&u,0);shot(&s,"menu-feature-setup-test");
 page("menu-usb-stems-setup",CHAIN(XZ_UI_GENERAL,XZ_UI_LOCAL_STEM_SETUP),NULL,0);
 page("menu-pc-stems",CHAIN(XZ_UI_GENERAL,XZ_UI_PC_STEMS),NULL,0);
 page("menu-stem-colors",CHAIN(XZ_UI_GENERAL,XZ_UI_STEM_COLORS),"DRUMS COLOR",1);
 page("menu-xpad-no-engine",CHAIN(XZ_UI_GENERAL,XZ_UI_XPAD),NULL,0);
 {
  struct xz_ui_model saved=ui_model;
  for(int d=0;d<2;d++)ui_model.deck[d].ready|=XZ_UI_SAMPLE|XZ_UI_GROOVE|XZ_UI_HOTCUE;
  ui_model.deck[0].sample_loaded=0x0f;ui_model.deck[0].groove_assigned=0x05;ui_model.deck[0].groove_loaded=0x05;
  page("menu-xpad",CHAIN(XZ_UI_GENERAL,XZ_UI_XPAD),"LOOP STRIP",0);
  page("menu-groove-pads",CHAIN(XZ_UI_SETTINGS,XZ_UI_STEMS),"PAD A",0);
  ui_model=saved;
 }
 page("menu-metrics-setup",CHAIN(XZ_UI_GENERAL,XZ_UI_METRICS_SETUP),NULL,0);
 page("menu-general-controls",CHAIN(XZ_UI_GENERAL,XZ_UI_CONTROLS),NULL,0);
 /* CONTROLS */
 page("menu-controls",CHAIN(XZ_UI_CONTROLS),NULL,0);
 page("menu-controls-pad-mode",CHAIN(XZ_UI_CONTROLS),"STEM SHORTCUT PAD MODE",1);
 page("menu-beat-jump",CHAIN(XZ_UI_CONTROLS,XZ_UI_BEAT_JUMP),"PADS C / D",1);
 /* ADVANCED */
 page("menu-advanced",CHAIN(XZ_UI_SETTINGS),NULL,0);
 page("menu-groove-pads-no-engine",CHAIN(XZ_UI_SETTINGS,XZ_UI_STEMS),NULL,0);
 page("menu-key-setup",CHAIN(XZ_UI_SETTINGS,XZ_UI_KEY_SETUP),NULL,0);
 page("menu-key-format",CHAIN(XZ_UI_SETTINGS,XZ_UI_KEY_SETUP,XZ_UI_BROWSE_SETUP),"KEY FORMAT",1);
 page("menu-ram-tracks",CHAIN(XZ_UI_SETTINGS,XZ_UI_RAM_SETUP),NULL,0);
 /* EXTRAS */
 page("menu-extras",CHAIN(XZ_UI_EXTRAS),NULL,0);
 page("menu-extras-profile-values",CHAIN(XZ_UI_EXTRAS),"ACTIVE DJ PROFILE",1);
 page("menu-vjtools",CHAIN(XZ_UI_EXTRAS,XZ_UI_CONNECTION),NULL,0);
 page("menu-games",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES),NULL,0);
 page("menu-doom",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_DOOM),NULL,0);
 page("menu-doom-setup",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_DOOM,XZ_UI_DOOM_SETUP),NULL,0);
 page("menu-doom-files",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_DOOM_FILES),NULL,0);
 page("menu-myhouse",CHAIN(XZ_UI_EXTRAS,XZ_UI_GAMES,XZ_UI_MYHOUSE_SETUP),NULL,0);
 page("menu-updates",CHAIN(XZ_UI_EXTRAS,XZ_UI_UPDATES),NULL,0);
 page("menu-update-setup",CHAIN(XZ_UI_EXTRAS,XZ_UI_UPDATES,XZ_UI_UPDATE_SETUP),NULL,0);
 page("menu-apps",CHAIN(XZ_UI_EXTRAS,XZ_UI_APPS),NULL,0);
 reach(&u,CHAIN(XZ_UI_EXTRAS,XZ_UI_APPS,XZ_UI_APPS_SETUP));u.sdk_step=XZ_SDK_UI_FOLDERS;open_shell(&s,&u,0);shot(&s,"menu-apps-setup");
 page("menu-dj-profile",CHAIN(XZ_UI_EXTRAS,XZ_UI_PROFILE_SETUP),NULL,0);
 /* the profile editor: the name on the keyboard, the logo list, the review */
 reach(&u,CHAIN(XZ_UI_EXTRAS,XZ_UI_PROFILE_SETUP));
 {
  struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&ui_model,w);struct xz_ui_action a[XZ_UI_ACTIONS];
  for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_PROFILE_EDIT_OPEN&&w[i].index>=0){
   struct xz_shell_menu_target t={(int16_t)w[i].kind,(int16_t)w[i].index,(int8_t)w[i].deck,XZ_UI_MENU_TOUCH,0};
   struct xz_ui_action many[XZ_UI_MENU_ACTIONS];xz_ui_menu_run(&u,&ui_model,&t,many);(void)a;break;
  }
 }
 open_shell(&s,&u,0);shot(&s,"menu-profile-review");
 u.edit_step=XZ_UI_EDIT_NAME;open_shell(&s,&u,0);turn(&s,13);shot(&s,"menu-profile-name");
 u.edit_step=XZ_UI_EDIT_LOGO;u.edit_logo=0;open_shell(&s,&u,0);shot(&s,"menu-profile-logo");
 return 0;
}
