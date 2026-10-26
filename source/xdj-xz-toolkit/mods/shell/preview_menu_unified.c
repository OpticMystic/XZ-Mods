#include "fixtures.h"
#include "settings_fixtures.h"
#include "menu_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* The same MODS sections in both interfaces, side by side: the CLASSIC panel and the MODERN MENU tabs. */
static uint16_t pixels[800*480];static struct fx_track track;static struct xz_ui_model m;static struct xz_ui u;static struct xz_shell_menu menu;static struct xz_shell_model sm;
static void save(const char *folder,const char *name){char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(int i=0;i<800*480;i++){unsigned p=pixels[i];unsigned char c[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(c,1,3,f);}fclose(f);}
static void classic(const char *folder,int page,const char *name,int popup){xz_ui_init(&u);xz_ui_navigate(&u,(enum xz_ui_page)page);u.profile_open=popup;int ok=xz_ui_render(&u,&m,pixels,800*480,800);assert(ok);(void)ok;save(folder,name);}
static void modern(const char *folder,int page,const char *name){
 xz_ui_init(&u);xz_ui_menu_open(&u,(enum xz_ui_page)page);int built=xz_ui_menu_build(&u,&m,&menu);assert(built);(void)built;mfx_look_values(&menu);
 struct xz_shell s;xz_shell_init(&s);xz_shell_mods_show(&s,&sm,mfx_tab_of(page));xz_shell_render(&s,&sm,pixels,800);save(folder,name);
}
int main(int argc,char **argv){
 (void)argc;assert(argc==2);(void)fxs_item;mfx_model(&m);m.theme=0;m.route.state=XZ_UI_ROUTE_READY;m.deck_view=2;
 fx_track_build(&track,7);fx_model_full(&sm,&track);sm.mods=&mfx_mods;sm.menu=&menu;sm.settings_screen=fxs_reset();
 static const int pages[]={XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_STEM_SECTION,XZ_UI_SCREEN,XZ_UI_FOUR_DECKS,XZ_UI_CONNECT,XZ_UI_EXTRAS};
 static const char *const names[]={"home","play","stems","screen","four-decks","connect","extras"};
 for(int i=0;i<7;i++){
  char name[64];snprintf(name,sizeof name,"unified-classic-%s",names[i]);classic(argv[1],pages[i],name,0);
  snprintf(name,sizeof name,"unified-modern-%s",names[i]);modern(argv[1],pages[i],name);
 }
 classic(argv[1],XZ_UI_HOME,"unified-classic-profiles",1);classic(argv[1],XZ_UI_STEM_COLORS,"unified-classic-colors",0);
 struct xz_shell s;xz_shell_init(&s);xz_ui_init(&u);xz_ui_menu_open(&u,XZ_UI_HOME);xz_ui_menu_build(&u,&m,&menu);
 xz_shell_mods_show(&s,&sm,mfx_tab_of(XZ_UI_HOME));s.mods_view.profile_open=1;s.mods_view.profile_focus=0;xz_shell_render(&s,&sm,pixels,800);save(argv[1],"unified-modern-profiles");
 return 0;
}
