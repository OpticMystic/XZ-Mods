/* Renders the SHORTCUT and UTILITY fixtures to PPM files in argv[1]. Host only. */
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
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) { xz_shell_init(u);key(u,m,k); }

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_settings OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 fx_model_full(&m,&track);
 m.settings_screen=fxs_reset();

 /* SHORTCUT */
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"settings-shortcut-full");
 turn(&u,&m,100);shot(&u,&m,"settings-shortcut-focus-last");
 turn(&u,&m,-6);shot(&u,&m,"settings-shortcut-focus-device");
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"settings-shortcut-read-only");
 m.capabilities|=XZ_SHELL_CAP_SET_SETTING;
 for(int i=0;i<FXS_COUNT;i++){fxs_table[i].known=0;}
 m.link_known=0;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"settings-shortcut-unknown");
 m.link_known=1;m.link_connected=1;
 static char wide[300];memset(wide,'W',sizeof wide-1);
 static const char *const long_choices[]={"A VERY LONG CHOICE NAME","ANOTHER LONG ONE","THIRD"};
 fxs_reset();
 fxs_item(XZ_SHELL_ITEM_QUANTIZE_BEAT)->label=wide;
 fxs_item(XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD)->choices=long_choices;
 fxs_item(XZ_SHELL_ITEM_VERSION)->choices=(const char *const[]){wide};
 fxs_item(XZ_SHELL_ITEM_WAVE_COLOR)->label=NULL;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"settings-shortcut-long");
 static struct xz_shell_settings_model empty;
 m.settings_screen=&empty;
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"settings-shortcut-empty");
 m.settings_screen=fxs_reset();

 /* UTILITY */
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"settings-utility-rail");
 push(&u,&m);turn(&u,&m,100);shot(&u,&m,"settings-utility-items-deep");
 turn(&u,&m,-100);turn(&u,&m,3);push(&u,&m);shot(&u,&m,"settings-utility-values");
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,1);push(&u,&m);push(&u,&m);turn(&u,&m,-1);shot(&u,&m,"settings-utility-values-writable");
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,1);push(&u,&m);turn(&u,&m,4);push(&u,&m);turn(&u,&m,11);shot(&u,&m,"settings-utility-values-scrolled");
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,1);push(&u,&m);turn(&u,&m,3);shot(&u,&m,"settings-utility-unknown");
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,4);push(&u,&m);turn(&u,&m,3);shot(&u,&m,"settings-utility-mixer-unknown");
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);turn(&u,&m,100);push(&u,&m);shot(&u,&m,"settings-utility-info");
 fxs_item(XZ_SHELL_ITEM_QUANTIZE_BEAT)->label=wide;
 fxs_item(XZ_SHELL_ITEM_BEAT_JUMP_BEAT)->choices=(const char *const[]){"1/2","1","2","4","8",wide,"32","64"};
 fxs_item(XZ_SHELL_ITEM_VERSION)->choices=(const char *const[]){wide};
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);push(&u,&m);turn(&u,&m,1);shot(&u,&m,"settings-utility-long");
 fxs_reset();
 m.settings_screen=&empty;
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"settings-utility-empty");
 m.settings_screen=NULL;shot(&u,&m,"settings-utility-slot-gone");
 m.settings_screen=fxs_reset();
 memset(&m.deck[1],0,sizeof m.deck[1]);m.deck[1].known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED);
 memset(&m.deck[0],0,sizeof m.deck[0]);
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"settings-utility-decks-unknown");
 return 0;
}
