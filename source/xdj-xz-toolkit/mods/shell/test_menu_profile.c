#ifdef NDEBUG
#error Profile acceptance requires active assertions
#endif
#include "fixtures.h"
#include "settings_fixtures.h"
#include "menu_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct fx_track track;
static struct xz_ui ui;
static struct xz_ui_model um;
static struct xz_shell_menu menu;
static struct xz_shell_model sm;
static struct xz_shell shell;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static uint16_t frame[800*480+16];
static void publish(void){assert(xz_ui_menu_build(&ui,&um,&menu));}
static size_t event(int k,int x,int y,int delta){return xz_shell_input(&shell,&sm,&(struct xz_shell_event){k,x,y,delta,0,-1},out);}
static void open_profile(void){assert(!event(XZ_SHELL_TOUCH_DOWN,540,18,0));assert(shell.mods_view.profile_open);assert(!event(XZ_SHELL_TOUCH_UP,540,18,0));}
static void render(const char *path){
 for(int i=800*480;i<800*480+16;i++)frame[i]=0x5aa5;
 assert(xz_shell_render(&shell,&sm,frame,800));
 for(int i=800*480;i<800*480+16;i++)assert(frame[i]==0x5aa5);
 if(path){FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(int i=0;i<800*480;i++){unsigned p=frame[i];unsigned char c[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(c,1,3,f);}fclose(f);}
}
int main(int argc,char **argv){
 (void)fxs_item;fx_track_build(&track,7);fx_model_full(&sm,&track);mfx_model(&um);xz_ui_init(&ui);xz_ui_menu_open(&ui,XZ_UI_GENERAL);publish();sm.menu=&menu;sm.mods=&mfx_mods;sm.settings_screen=fxs_reset();xz_shell_init(&shell);xz_shell_mods_show(&shell,&sm,mfx_tab_of(XZ_UI_GENERAL));
 assert(menu.profile_present&&menu.profile_count==3&&menu.profile_current==0);
 open_profile();render(argc>1?argv[1]:NULL);
 int depth=shell.depth;int taken=0;sh_settings_back_step(&shell,&sm,out,&taken);assert(taken&&!shell.mods_view.profile_open&&shell.depth==depth);
 open_profile();assert(!event(XZ_SHELL_TOUCH_DOWN,100,300,0));assert(!shell.mods_view.profile_open);assert(!event(XZ_SHELL_TOUCH_UP,100,300,0));assert(shell.depth==depth);
 open_profile();assert(!event(XZ_SHELL_TOUCH_DOWN,400,150,0));size_t n=event(XZ_SHELL_TOUCH_UP,400,150,0);assert(n==1&&out[0].id==XZ_SHELL_MENU_PROFILE_PICK&&out[0].value==1&&!shell.mods_view.profile_open);
 open_profile();assert(!event(XZ_SHELL_TOUCH_DOWN,400,150,0));event(XZ_SHELL_TOUCH_MOVE,400,203,0);assert(!event(XZ_SHELL_TOUCH_UP,400,150,0)&&shell.mods_view.profile_open);
 shell.mods_view.profile_open=0;menu.profiles[1].available=0;open_profile();event(XZ_SHELL_TOUCH_DOWN,400,150,0);assert(!event(XZ_SHELL_TOUCH_UP,400,150,0)&&shell.mods_view.profile_open);
 shell.mods_view.profile_open=0;open_profile();event(XZ_SHELL_ENCODER_TURN,0,0,99);assert(shell.mods_view.profile_focus==3);n=event(XZ_SHELL_ENCODER_PUSH,0,0,0);assert(n==1&&out[0].id==XZ_SHELL_MENU_PROFILE_SETUP);
 um.dj=NULL;publish();assert(!menu.profile_present);assert(!event(XZ_SHELL_TOUCH_DOWN,540,18,0)&&!shell.mods_view.profile_open);event(XZ_SHELL_TOUCH_UP,540,18,0);
 um.dj=&mfx_dj;mfx_dj.count=0;mfx_dj.current=-1;publish();open_profile();render(NULL);n=event(XZ_SHELL_ENCODER_PUSH,0,0,0);assert(n==1&&out[0].id==XZ_SHELL_MENU_PROFILE_SETUP);
 mfx_dj.count=32;for(int i=3;i<32;i++){mfx_dj.entry[i]=mfx_dj.entry[0];mfx_dj.entry[i].name="DJ PROFILE";}mfx_dj.current=31;publish();open_profile();assert(shell.mods_view.profile_scroll==27);render(NULL);event(XZ_SHELL_ENCODER_TURN,0,0,-99);assert(shell.mods_view.profile_focus==0&&shell.mods_view.profile_scroll==0);
 event(XZ_SHELL_TOUCH_DOWN,642,56,0);assert(!event(XZ_SHELL_TOUCH_UP,642,56,0)&&shell.mods_view.profile_scroll==5);
 event(XZ_SHELL_TOUCH_DOWN,606,56,0);assert(!event(XZ_SHELL_TOUCH_UP,606,56,0)&&shell.mods_view.profile_scroll==0);
 event(XZ_SHELL_TOUCH_DOWN,400,204,0);mfx_dj.count=2;publish();assert(!event(XZ_SHELL_TOUCH_UP,400,204,0)&&shell.mods_view.profile_open);
 menu.profile_count=255;render(NULL);mfx_dj.count=32;
 xz_ui_menu_open(&ui,XZ_UI_NETWORK);publish();assert(menu.profile_present&&menu.profile_count==32);
 puts("PASS profile header and dropdown: real menu snapshot, touch ownership, outside tap, BACK, scrolling32, unavailable, empty setup and network page");return 0;
}
