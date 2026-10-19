/* The new interface's MODS screen on its APPEARANCE tab, for the setup
   wizard's "switch it later" graphic: the shell's own renderer
   (mods/shell/shell_settings.c) with the tabs the runtime publishes
   (shell_runtime.c mod_tabs: the MODS panel's top pages as TAB_PAGE tabs).
   The tab's OPEN APPEARANCE button opens MODS > APPEARANCE in the panel.
   build_wizard_media.py checks these labels against shell_runtime.c. Host only.

   Usage: shell_appearance OUTPUT_DIR
   Writes shell-mods-appearance.ppm and prints the OPEN button rect as JSON. */
#include "fixtures.h"
#include "settings_fixtures.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

/* ui/ui.h page numbers the runtime's tabs open. */
enum { PAGE_SETTINGS=2, PAGE_THEMES=3, PAGE_CONTROLS=5, PAGE_EXTRAS=7, PAGE_GENERAL=26 };
static const struct xz_shell_mods_tab tabs[5]={
 {"GENERAL",XZ_SHELL_TAB_PAGE,NULL,NULL,0,PAGE_GENERAL,"TURN MODS ON AND OFF AND SET THEM UP"},
 {"CONTROLS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,PAGE_CONTROLS,"STEM PAD SHORTCUTS AND CUSTOM BEAT JUMPS"},
 {"APPEARANCE",XZ_SHELL_TAB_PAGE,NULL,NULL,0,PAGE_THEMES,"INTERFACE, THEME, WAVEFORM, KEYS AND STEM COLORS"},
 {"ADVANCED",XZ_SHELL_TAB_PAGE,NULL,NULL,0,PAGE_SETTINGS,"KEY SHIFT, KEY SYNC, GROOVE PADS, X-PAD AND RAM TRACKS"},
 {"EXTRAS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,PAGE_EXTRAS,"VJ.TOOLS LINK, GAMES, USB APPS AND UPDATES"}};
static const struct xz_shell_mods_model mods={5,tabs};

static void save(const char *dir, const char *name) {
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
int main(int argc, char **argv) {
 if(argc!=2){fputs("usage: shell_appearance OUTPUT_DIR\n",stderr);return 2;}
 struct xz_shell u;struct xz_shell_model m;
 fx_track_build(&track,7);fx_model_full(&m,&track);
 m.settings_screen=fxs_reset();(void)fxs_item;
 m.mods=&mods;
 /* The header MODS button: the MODS screen opens on the first mod tab. Then two tabs right to APPEARANCE. */
 xz_shell_init(&u);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,XZ_SHELL_MODS_X+20,12,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,XZ_SHELL_MODS_X+20,12,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,2,0,-1},out);
 if(!xz_shell_render(&u,&m,frame,XZ_SHELL_WIDTH)){fputs("render failed\n",stderr);return 1;}
 save(argv[1],"shell-mods-appearance");
 struct xz_shell_rect b=xz_shell_mods_page_button(),t=xz_shell_mods_tab_rect(&u,&m,3);
 printf("{\"open\":[%d,%d,%d,%d],\"tab\":[%d,%d,%d,%d],\"mods\":[%d,0,%d,24]}\n",b.x,b.y,b.x+b.w,b.y+b.h,t.x,t.y,t.x+t.w,t.y+t.h,
  XZ_SHELL_MODS_X,XZ_SHELL_MODS_X+XZ_SHELL_MODS_W);
 return 0;
}
