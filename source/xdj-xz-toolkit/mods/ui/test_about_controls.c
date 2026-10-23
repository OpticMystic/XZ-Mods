/* Offline event replay: production key map/contact phases and actual shell/menu. */
#define main retained_menu_main
#include "../shell/test_menu.c"
#undef main
#include "../shell_runtime_core.c"
#include "ui_about.h"
#include "../shell/shell_browse.h"

static struct xz_shrt_contacts physical;
static void menu_physical(struct xz_shell *s,unsigned op){
 const struct xz_shrt_key_map *map=xz_shrt_key_lookup(0x20b,0);
 enum xz_shrt_phase phase=xz_shrt_phase_of(op);int held=0;
 if(phase==XZ_SHRT_PRESS){assert(xz_shrt_key_verdict(s,&sm,map)==XZ_SHRT_SWALLOW);xz_shrt_contact_press(&physical,0x20b,0,1);}
 else assert(xz_shrt_contact_follow(&physical,0x20b,0,phase,&held)==1);
 int k=xz_shrt_key_for_phase(map,phase,held);if(k>=0)key(s,(enum xz_shell_key)k);
}
static void hold_menu(struct xz_shell *s){
 struct xz_shell before=*s;menu_physical(s,0);assert(!memcmp(s,&before,sizeof *s));
 menu_physical(s,1);assert(xz_shell_screen(s)==XZ_SHELL_UTILITY&&s->mods_view.tab==0&&s->mods_view.on_tabs);
 before=*s;menu_physical(s,1);menu_physical(s,2);assert(!memcmp(s,&before,sizeof *s));
}
static void painted(struct xz_shell *s){assert(xz_shell_render_frame(s,&sm,buffer+GUARD+STRIDE,STRIDE));}
static void shot(struct xz_shell *s,const char *dir,const char *name,int classic){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 if(classic)assert(xz_ui_render(&ui,&um,buffer+GUARD+STRIDE,STRIDE*480,STRIDE));else painted(s);
 FILE *f=fopen(path,"wb");assert(f);fputs("P6\n800 480\n255\n",f);
 for(int y=0;y<480;y++)for(int x=0;x<800;x++){
  unsigned v=buffer[GUARD+STRIDE+y*STRIDE+x];unsigned char rgb[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};
  assert(fwrite(rgb,1,3,f)==3);
 }assert(!fclose(f));
}
static void header_tests(struct xz_shell *s,const char *dir){
 static const struct xz_shell_browse_model browse={0};
 sm.browse=&browse;sm.capabilities|=XZ_SHELL_CAP_BROWSE;
 fresh(s);painted(s);key(s,XZ_SHELL_KEY_MENU);
 /* The still-painted PLAY header cannot become a close button without a new frame. */
 assert(!tap(s,XZ_SHELL_MODS_X+8,10)&&in_mods(s));
 painted(s);assert(!tap(s,XZ_SHELL_STEMS_X+8,10)&&in_mods(s));
 shot(s,dir,"mods-header",0);tap(s,XZ_SHELL_MODS_X+8,10);assert(xz_shell_screen(s)==XZ_SHELL_PLAY);
 /* A close press owns its move/release, even when the underlying PLAY row becomes visible. */
 painted(s);key(s,XZ_SHELL_KEY_MENU);painted(s);
 send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,XZ_SHELL_MODS_X+8,10,0,0,-1});
 assert(s->down&&s->down_region==XZ_SHELL_TOUCH_HEADER&&xz_shell_screen(s)==XZ_SHELL_PLAY);
 assert(!send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_MOVE,200,160,0,0,-1}));
 assert(!send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,200,160,0,0,-1})&&!s->down);
 key(s,XZ_SHELL_KEY_BROWSE);assert(xz_shell_screen(s)==XZ_SHELL_BROWSE);painted(s);
 shot(s,dir,"browse-header",0);tap(s,XZ_SHELL_MODS_X+8,10);assert(xz_shell_screen(s)==XZ_SHELL_PLAY);
 key(s,XZ_SHELL_KEY_BROWSE);key(s,XZ_SHELL_KEY_INFO);assert(s->panel.kind!=XZ_SHELL_PANEL_NONE);painted(s);
 tap(s,XZ_SHELL_MODS_X+8,10);assert(xz_shell_screen(s)==XZ_SHELL_BROWSE&&s->panel.kind==XZ_SHELL_PANEL_NONE);
 key(s,XZ_SHELL_KEY_SHORTCUT);painted(s);tap(s,XZ_SHELL_MODS_X+8,10);assert(xz_shell_screen(s)==XZ_SHELL_BROWSE);
 key(s,XZ_SHELL_KEY_MENU);assert(s->panel.kind!=XZ_SHELL_PANEL_NONE);hold_menu(s);assert(s->panel.kind==XZ_SHELL_PANEL_NONE);
 puts("PASS painted X close targets MODS, BROWSE, panel and SHORTCUT; hidden STEMS inert; stale frame blocked; drag/release owned");
}
int main(int argc,char **argv){
 assert(argc==2);
 fx_track_build(&track,7);fx_model_full(&sm,&track);sm.settings_screen=fxs_reset();sm.mods=&mfx_mods;sm.menu=&menu;(void)fxs_item;
 struct xz_shell s;fresh(&s);
 for(int tab=0;tab<7;tab++){xz_shell_mods_show(&s,&sm,tab);hold_menu(&s);hold_menu(&s);}
 puts("PASS physical MENU DOWN/HOLD/repeated HOLD/UP opens UTILITY exactly once from every tab including UTILITY");
 fresh(&s);menu_physical(&s,0);assert(xz_shell_screen(&s)==XZ_SHELL_PLAY);menu_physical(&s,2);assert(in_mods(&s)&&ui.page==XZ_UI_GENERAL);
 header_tests(&s,argv[1]);
 for(unsigned t=0;t<mfx_mods.count;t++)assert(strcmp(mfx_tabs[t].label,"BUTTONS"));
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);push(&s);cursor_to(&s,"BUTTONS");push(&s);
 assert(ui.page==XZ_UI_BUTTONS&&menu.root==XZ_UI_CONTROLS);key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_CONTROLS);
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"BUTTONS"));push(&s);assert(ui.page==XZ_UI_BUTTONS);
 puts("PASS BUTTONS under CONTROLS; BACK restores link; one push re-enters; persistent IDs retained");
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,4);push(&s);cursor_to(&s,"ABOUT XZ MODS");push(&s);
 assert(ui.page==XZ_UI_ABOUT&&menu.layout==XZ_SHELL_MENU_ABOUT&&menu.root==XZ_UI_EXTRAS&&menu.row_count==XZ_ABOUT_LINKS);
 for(int i=0;i<XZ_ABOUT_LINKS;i++){
  cursor_to(&s,xz_about_links[i].label);push(&s);assert(ui.about_link==i);
  char name[64];snprintf(name,sizeof name,"about-flagship-%d",i);shot(&s,argv[1],name,0);
  struct xz_shell_rect rect=xz_shell_menu_row_rect(&s,&sm,(i+1)%XZ_ABOUT_LINKS);
  tap(&s,rect.x+8,rect.y+8);assert(ui.about_link==(i+1)%XZ_ABOUT_LINKS);
 }
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_EXTRAS);assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"ABOUT XZ MODS"));
 push(&s);assert(ui.page==XZ_UI_ABOUT);
 struct xz_ui_action actions[XZ_UI_ACTIONS];um.ui_interface=XZ_UI_INTERFACE_CLASSIC;um.theme=0;
 ui.focus_kind=XZ_UI_ABOUT_LINK;ui.focus_index=0;ui.focus_deck=-1;
 for(int i=0;i<XZ_ABOUT_LINKS;i++){
  struct xz_ui_widget focused;assert(xz_ui_focused(&ui,&um,&focused)&&focused.kind==XZ_UI_ABOUT_LINK&&focused.index==i);
  assert(!xz_ui_encoder_push(&ui,&um,actions)&&ui.about_link==i);
  char name[64];snprintf(name,sizeof name,"about-classic-%d",i);shot(&s,argv[1],name,1);
  xz_ui_encoder_turn(&ui,&um,1,actions);
 }
 assert(xz_ui_back(&ui,actions)<=1&&ui.page==XZ_UI_EXTRAS);
 puts("PASS About touch and encoder selection, both renderers, one QR, EXTRAS ancestor BACK; no external actions");
 return 0;
}
