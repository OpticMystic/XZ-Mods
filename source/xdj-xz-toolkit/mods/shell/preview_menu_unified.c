#include "fixtures.h"
#include "settings_fixtures.h"
#include "menu_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static uint16_t pixels[800*480];static struct fx_track track;static struct xz_ui_model m;static struct xz_ui u;static struct xz_shell_menu menu;static struct xz_shell_model sm;
static void save(const char *folder,const char *name){char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(int i=0;i<800*480;i++){unsigned p=pixels[i];unsigned char c[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(c,1,3,f);}fclose(f);}
static void classic(const char *folder,int page,const char *name,int popup){xz_ui_init(&u);u.page=(enum xz_ui_page)page;u.profile_open=popup;assert(xz_ui_render(&u,&m,pixels,800*480,800));save(folder,name);}
int main(int argc,char **argv){assert(argc==2);(void)fxs_item;mfx_model(&m);m.theme=0;classic(argv[1],XZ_UI_GENERAL,"unified-classic-general",0);classic(argv[1],XZ_UI_SETTINGS,"unified-classic-advanced",0);classic(argv[1],XZ_UI_EXTRAS,"unified-classic-extras",0);classic(argv[1],XZ_UI_GENERAL,"unified-classic-profiles",1);classic(argv[1],XZ_UI_STEM_COLORS,"unified-classic-colors",0);
 fx_track_build(&track,7);fx_model_full(&sm,&track);sm.mods=&mfx_mods;sm.menu=&menu;sm.settings_screen=fxs_reset();xz_ui_init(&u);xz_ui_menu_open(&u,XZ_UI_GENERAL);assert(xz_ui_menu_build(&u,&m,&menu));struct xz_shell s;xz_shell_init(&s);xz_shell_mods_show(&s,&sm,mfx_tab_of(XZ_UI_GENERAL));assert(xz_shell_render(&s,&sm,pixels,800));save(argv[1],"unified-flagship-general");s.mods_view.profile_open=1;s.mods_view.profile_focus=0;assert(xz_shell_render(&s,&sm,pixels,800));save(argv[1],"unified-flagship-profiles");
 xz_ui_menu_open(&u,XZ_UI_CONTROLS);xz_ui_menu_build(&u,&m,&menu);xz_shell_mods_show(&s,&sm,mfx_tab_of(XZ_UI_CONTROLS));s.menu_view.page=menu.page;for(int i=0;i<menu.row_count;i++)if(menu.row[i].target.kind==XZ_UI_STEMS_OVERLAY){s.stack[s.depth-1].cursor=menu.row[i].key;s.stack[s.depth-1].focus=2;}assert(xz_shell_render(&s,&sm,pixels,800));save(argv[1],"unified-flagship-layouts");return 0;}
