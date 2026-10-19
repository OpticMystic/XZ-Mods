/* MODS > APPEARANCE as the deck draws it, for the desktop setup wizard's
   "switch it later" graphic: the real MODS panel renderer (mods/ui) with the
   encoder focus on the INTERFACE option that switches back. One render per
   interface, each in the skin that interface shows (CLASSIC: the ORIGINAL
   theme, MODERN: FLAGSHIP). Also the CLASSIC THEMES page. Host only.

   Usage: menu_render OUTPUT_DIR */
#include "ui.h"
#include "themes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t pixels[800*480];
static void save(const char *dir, const char *name) {
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n800 480\n255\n");
 for(int i=0;i<800*480;i++){
  unsigned v=pixels[i];unsigned char c[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
  if(fwrite(c,1,3,f)!=3)exit(1);
 }
 if(fclose(f))exit(1);
}
static void render(const char *dir, const char *name, struct xz_ui *u, const struct xz_ui_model *m) {
 if(!xz_ui_render(u,m,pixels,800*480,800)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 save(dir,name);
}
int main(int argc, char **argv) {
 if(argc!=2){fputs("usage: menu_render OUTPUT_DIR\n",stderr);return 2;}
 struct xz_ui u;struct xz_ui_model m;
 memset(&m,0,sizeof m);
 m.key_feedback=1;m.wave_mode=1;m.settings_status="SETTINGS SAVED TO USB 1";
 for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;m.deck[d].sample_volume=1;for(int r=0;r<4;r++)m.deck[d].levels[r]=1;}
 for(int face=0;face<2;face++){
  m.ui_interface=face;m.classic_theme=0;m.modern_theme=0;
  m.theme=face==XZ_UI_INTERFACE_MODERN?XZ_THEME_FLAGSHIP:m.classic_theme; /* xz_settings_look */
  xz_ui_init(&u);u.page=XZ_UI_THEMES;
  /* Focus the option that switches to the other interface: the one step the caption names. */
  u.focus_kind=XZ_UI_SET_INTERFACE;u.focus_index=face==XZ_UI_INTERFACE_MODERN?XZ_UI_INTERFACE_CLASSIC:XZ_UI_INTERFACE_MODERN;u.focus_deck=-1;
  render(argv[1],face==XZ_UI_INTERFACE_MODERN?"appearance-modern":"appearance-classic",&u,&m);
 }
 m.ui_interface=XZ_UI_INTERFACE_CLASSIC;m.classic_theme=2;m.theme=2;
 xz_ui_init(&u);u.page=XZ_UI_THEMES;render(argv[1],"appearance-classic-cyberpunk",&u,&m);
 xz_ui_init(&u);u.page=XZ_UI_THEME_PICK;u.focus_kind=XZ_UI_SET_THEME;u.focus_index=2;u.focus_deck=-1;render(argv[1],"theme-pick-cyberpunk",&u,&m);
 /* The MODS badge the classic screen shows in its top right corner. */
 memset(pixels,0,sizeof pixels);xz_ui_render_badge(pixels,800);save(argv[1],"badge");
 return 0;
}
