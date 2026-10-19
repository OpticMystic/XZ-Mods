/* Renders the MODS screen (UTILITY tab and mod tabs) to PPM files in argv[1]. Host only. */
#include "fixtures.h"
#include "settings_fixtures.h"
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
static int cursor_id(const struct xz_shell *u) { return u->stack[u->depth-1].cursor-1; }
/* Held MENU, then into tab t at rail row r, items cursor on id, values open. */
static void open_item(struct xz_shell *u, const struct xz_shell_model *m, int t, int r, int id) {
 xz_shell_init(u);key(u,m,XZ_SHELL_KEY_MENU_HOLD);
 turn(u,m,t);push(u,m);
 turn(u,m,r);push(u,m);
 for(int i=0;i<40&&cursor_id(u)!=id;i++)turn(u,m,1);
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

 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"mods-utility-tab-row");
 push(&u,&m);shot(&u,&m,"mods-utility-rail");
 push(&u,&m);shot(&u,&m,"mods-utility-items");
 open_item(&u,&m,0,0,XZ_SHELL_ITEM_HOT_CUE_COLOR);shot(&u,&m,"mods-utility-unwritable");
 static const char *notes[FXS_COUNT];
 notes[fxs_item(XZ_SHELL_ITEM_HOT_CUE_COLOR)-fxs_table]="SET IT IN THE COLOR MENU ON THE STOCK SCREEN";
 fxs_model.notes=notes;
 shot(&u,&m,"mods-utility-unwritable-note");
 fxs_model.notes=NULL;
 open_item(&u,&m,0,1,XZ_SHELL_ITEM_WAVE_COLOR);shot(&u,&m,"mods-utility-wave-color");

 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,1);shot(&u,&m,"mods-stems-tab-row");
 open_item(&u,&m,1,1,22);shot(&u,&m,"mods-stems-values");
 open_item(&u,&m,1,1,37);shot(&u,&m,"mods-stems-unwritable");
 open_item(&u,&m,2,0,3);shot(&u,&m,"mods-appearance-theme");
 open_item(&u,&m,3,1,16);shot(&u,&m,"mods-beat-jump-page");

 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,4);shot(&u,&m,"mods-page-tab-row");
 push(&u,&m);shot(&u,&m,"mods-page-entered");

 /* The MODS header button from PLAY: the first mod tab. */
 xz_shell_init(&u);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,XZ_SHELL_MODS_X+20,12,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,XZ_SHELL_MODS_X+20,12,0,0,-1},out);
 shot(&u,&m,"mods-from-header");

 /* SHORTCUT without the set capability: every item says how to change it. */
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"mods-shortcut-read-only");
 return 0;
}
