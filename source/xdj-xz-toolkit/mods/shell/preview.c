/* Renders the shell's fixtures to PPM files in argv[1]. Host only. */
#include "fixtures.h"
#include "shell_browse.h"
#include "shell_settings.h"
#include "shell_info.h"
#include "shell_source.h"
static struct xz_shell_browse_model browse_zero;static struct xz_shell_settings_model settings_zero;static struct xz_shell_info_model info_zero;static struct xz_shell_source_model source_zero;
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;

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
int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 xz_shell_init(&u);
 fx_model_full(&m,&track);
 shot(&u,&m,"play-full");
 m.settings.writable=(uint64_t)1<<XZ_SHELL_SET_TIME_MODE;
 shot(&u,&m,"play-time-mode-writable");
 fx_model_full(&m,&track);

 u.deck=1;u.zoom=XZ_SHELL_ZOOM_MAX;shot(&u,&m,"play-zoom-in");
 u.zoom=0;shot(&u,&m,"play-zoom-out");
 u.zoom=XZ_SHELL_ZOOM_DEFAULT;u.deck=0;

 u.grid_mode=1;shot(&u,&m,"play-grid-mode");u.grid_mode=0;

 fx_model_full(&m,&track);m.settings.wave_position=XZ_SHELL_POSITION_LEFT;shot(&u,&m,"play-left");
 fx_model_full(&m,&track);m.settings.wave_color=XZ_SHELL_WAVE_BLUE;shot(&u,&m,"play-blue");
 fx_model_full(&m,&track);m.settings.wave_color=XZ_SHELL_WAVE_RGB;shot(&u,&m,"play-rgb");

 fx_model_full(&m,&track);
 memset(&m.deck[1],0,sizeof m.deck[1]);m.deck[1].known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED)|XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO)|XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO_RANGE)|XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE)|XZ_SHELL_KNOWN(XZ_SHELL_F_QUANTIZE)|XZ_SHELL_KNOWN(XZ_SHELL_F_BEAT_JUMP)|XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER_TEMPO);
 m.deck[1].loaded=0;m.deck[1].tempo_range=10;m.deck[1].source=XZ_SHELL_SRC_USB2;m.deck[1].quantize_num=1;m.deck[1].quantize_den=1;m.deck[1].beat_jump_num=16;m.deck[1].beat_jump_den=1;
 shot(&u,&m,"play-not-loaded");

 memset(&m,0,sizeof m);m.capabilities=0;
 shot(&u,&m,"play-unknown");

 fx_model_full(&m,&track);
 m.deck[0].elapsed_ms=FX_TOTAL_MS-12000;m.deck[0].bar_phase=2;m.clock_ms=250;
 shot(&u,&m,"play-ending-flash");

 /* Placeholder screens, with the slots present so they open. */
 fx_model_full(&m,&track);
 m.browse=&browse_zero;m.settings_screen=&settings_zero;
 m.source=&source_zero;m.info=&info_zero;
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_SOURCE;
 static const struct { enum xz_shell_key key; const char *name; } pages[]={
  {XZ_SHELL_KEY_BROWSE,"screen-browse"},{XZ_SHELL_KEY_PLAYLIST,"screen-playlist"},{XZ_SHELL_KEY_TAG_LIST,"screen-tag-list"},
  {XZ_SHELL_KEY_SEARCH,"screen-search"},{XZ_SHELL_KEY_INFO,"screen-info"},{XZ_SHELL_KEY_SHORTCUT,"screen-shortcut"},
  {XZ_SHELL_KEY_MENU_HOLD,"screen-utility"},{XZ_SHELL_KEY_SOURCE,"screen-source"}};
 for(size_t i=0;i<sizeof pages/sizeof *pages;i++){
  struct xz_shell_action out[XZ_SHELL_ACTIONS];
  xz_shell_init(&u);
  xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,pages[i].key,-1},out);
  shot(&u,&m,pages[i].name);
 }
 xz_shell_init(&u);u.popup.kind=XZ_SHELL_POPUP_RECENT;shot(&u,&m,"play-popup-slot");
 return 0;
}
