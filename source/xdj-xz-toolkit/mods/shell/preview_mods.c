/* Renders the MODS screen (UTILITY tab and mod tabs) to PPM files in argv[1]. Host only. */
#include "browse_fixtures.h"
#include "settings_fixtures.h"
#include "shell_source.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

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
static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 save(name);
}
static void key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static void turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1},out);
}
static void push(struct xz_shell *u, const struct xz_shell_model *m) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1},out);
}
static void send_hold(struct xz_shell *u, const struct xz_shell_model *m) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_HOLD,0,0,0,0,-1},out);
}
static int cursor_id(const struct xz_shell *u) { return u->stack[u->depth-1].cursor-1; }
/* Held MENU, then into page t, the item cursor on id, its values open (section r is for the reader). */
static void open_item(struct xz_shell *u, const struct xz_shell_model *m, int t, int r, int id) {
 (void)r;
 xz_shell_init(u);key(u,m,XZ_SHELL_KEY_MENU_HOLD);
 turn(u,m,t);push(u,m);
 for(int i=0;i<60&&cursor_id(u)!=id;i++)turn(u,m,1);
 push(u,m);
}

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_mods OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 fx_model_full(&m,&track);
 m.settings_screen=fxs_reset();
 m.mods=&fxm_model;
 (void)bx_folders;

 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"mods-utility-rail");
 push(&u,&m);shot(&u,&m,"mods-utility-items");
 send_hold(&u,&m);shot(&u,&m,"mods-utility-next-category");
 open_item(&u,&m,0,0,XZ_SHELL_ITEM_HOT_CUE_COLOR);shot(&u,&m,"mods-utility-unwritable");
 static const char *notes[FXS_COUNT];
 notes[fxs_item(XZ_SHELL_ITEM_HOT_CUE_COLOR)-fxs_table]="SET IT IN THE COLOR MENU ON THE STOCK SCREEN";
 fxs_model.notes=notes;
 shot(&u,&m,"mods-utility-unwritable-note");
 fxs_model.notes=NULL;
 open_item(&u,&m,0,1,XZ_SHELL_ITEM_WAVE_COLOR);shot(&u,&m,"mods-utility-wave-color");

 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,1);shot(&u,&m,"mods-stems-rail");
 open_item(&u,&m,1,1,22);shot(&u,&m,"mods-stems-values");
 open_item(&u,&m,1,1,37);shot(&u,&m,"mods-stems-unwritable");
 open_item(&u,&m,2,1,2);shot(&u,&m,"mods-appearance-theme");
 open_item(&u,&m,3,1,16);shot(&u,&m,"mods-beat-jump-page");

 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,4);shot(&u,&m,"mods-page-rail");
 push(&u,&m);shot(&u,&m,"mods-page-entered");

 /* The MODS header button from PLAY: the first mod tab. */
 xz_shell_init(&u);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,XZ_SHELL_MODS_X+20,12,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,XZ_SHELL_MODS_X+20,12,0,0,-1},out);
 shot(&u,&m,"mods-from-header");

 /* Many mod pages: the rail scrolls, UTILITY stays on top. */
 static const struct xz_shell_mods_tab many[11]={
  {"GENERAL",XZ_SHELL_TAB_PAGE,NULL,NULL,0,1,"TURN MODS ON AND OFF AND SET THEM UP"},{"CONTROLS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,2,NULL},
  {"ADVANCED",XZ_SHELL_TAB_PAGE,NULL,NULL,0,3,NULL},{"EXTRAS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,4,NULL},
  {"STEMS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,5,NULL},{"BEAT JUMP",XZ_SHELL_TAB_PAGE,NULL,NULL,0,6,NULL},
  {"DJ PROFILE",XZ_SHELL_TAB_PAGE,NULL,NULL,0,7,NULL},{"GAMES",XZ_SHELL_TAB_PAGE,NULL,NULL,0,8,NULL},
  {"PERFORMANCE PADS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,9,NULL},{"RAM TRACKS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,10,NULL},
  {"ABOUT THIS MOD",XZ_SHELL_TAB_PAGE,NULL,NULL,0,11,NULL},
 };
 static const struct xz_shell_mods_model many_mods={11,many};
 m.mods=&many_mods;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"mods-many-pages-utility");
 turn(&u,&m,6);shot(&u,&m,"mods-many-pages-middle");
 turn(&u,&m,10);shot(&u,&m,"mods-many-pages-last");
 /* APPEARANCE as the runtime publishes it: INTERFACE, THEME, WAVEFORM, KEYS AND STEMS. */
 m.mods=&fxm_model;
 open_item(&u,&m,2,3,7);shot(&u,&m,"mods-appearance-sections");

 /* MY SETTINGS on USB 1's browse root. */
 {
  static struct fx_track t2;static struct bx_library lib;static struct xz_shell_browse_model b;static struct xz_shell_source_model src;
  (void)t2;bx_build(&lib);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
  for(int i=0;i<2;i++){src.device[i].known=1u<<XZ_SHELL_D_STATUS;src.device[i].status=XZ_SHELL_DEVICE_READY;}
  struct xz_shell_model bm=m;
  bm.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_LOAD;
  bm.browse=&b;bm.source=&src;bm.my_settings=fxm_my_reset();
  xz_shell_init(&u);xz_shell_browse_reset();key(&u,&bm,XZ_SHELL_KEY_SOURCE_USB1);key(&u,&bm,XZ_SHELL_KEY_MENU);
  shot(&u,&bm,"my-settings-top");
  turn(&u,&bm,1);push(&u,&bm);shot(&u,&bm,"my-settings-save-armed");
  key(&u,&bm,XZ_SHELL_KEY_BACK);turn(&u,&bm,3);shot(&u,&bm,"my-settings-profiles");
  turn(&u,&bm,6);shot(&u,&bm,"my-settings-waveform");
  turn(&u,&bm,20);shot(&u,&bm,"my-settings-stored");
  fxm_my.writable=0;fxm_my.on_stick=0;fxm_my.stored=NULL;fxm_my.profile_count=0;
  xz_shell_init(&u);xz_shell_browse_reset();key(&u,&bm,XZ_SHELL_KEY_SOURCE_USB1);key(&u,&bm,XZ_SHELL_KEY_MENU);
  shot(&u,&bm,"my-settings-locked-empty");
  fxm_my_reset();
 }

 /* USB colours: deck 1 from a stick with a yellow background and RGB waveforms, deck 2 from a pink one. */
 {
  struct xz_shell_model cm=m;
  cm.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);cm.deck[0].look=(struct xz_shell_source_look){1,0xf2d42c,XZ_SHELL_WAVE_RGB};
  cm.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);cm.deck[1].look=(struct xz_shell_source_look){1,0xf870b8,-1};
  xz_shell_init(&u);shot(&u,&cm,"usb-colors-play");
  xz_shell_restore_setting(&u,XZ_SHELL_ITEM_USB_COLORS,XZ_SHELL_USB_COLORS_TILE);shot(&u,&cm,"usb-colors-deck-only");
  xz_shell_restore_setting(&u,XZ_SHELL_ITEM_USB_COLORS,XZ_SHELL_USB_COLORS_OFF);shot(&u,&cm,"usb-colors-off");
 }

 /* SHORTCUT without the set capability: every item says how to change it. */
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"mods-shortcut-read-only");
 return 0;
}
