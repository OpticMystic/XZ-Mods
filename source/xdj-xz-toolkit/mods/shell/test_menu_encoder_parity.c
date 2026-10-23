/* SPDX-License-Identifier: MIT */
#define main retained_menu_main
#include "test_menu.c"
#undef main
#include "browse_fixtures.h"
static unsigned ep_checks,ep_failures;
#define EP(test,label) do { ep_checks++;if(!(test)){ep_failures++;fprintf(stderr,"FAIL %s\n",label);} } while(0)
static void physical_load_parity(void){
 static struct bx_library library;static struct fx_track track;
 bx_build(&library);fx_track_build(&track,1);
 for(int count=2;count<=4;count+=2)for(int deck=0;deck<count;deck++){
  struct xz_shell s;struct xz_shell_model m;struct xz_shell_browse_model b;
  fx_model_full(&m,&track);bx_tracks(&b,&library,XZ_SHELL_BROWSE);b.media_generation=7;
  m.browse=&b;m.deck_count=count;m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;
  xz_shell_init(&s);xz_shell_browse_reset();
  struct xz_shell_action a[XZ_SHELL_ACTIONS];
  struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};xz_shell_input(&s,&m,&e,a);
  e.kind=XZ_SHELL_ENCODER_PUSH;xz_shell_input(&s,&m,&e,a); /* current category */
  e.kind=XZ_SHELL_ENCODER_TURN;e.delta=2;xz_shell_input(&s,&m,&e,a);
  EP(s.stack[s.depth-1].cursor==2,"encoder selects track in one turn event");
  e=(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_LOAD,deck};
  size_t n=xz_shell_input(&s,&m,&e,a);
  EP(n==1&&a[0].kind==XZ_SHELL_ACT_LOAD&&a[0].deck==deck&&a[0].id==(int)b.rows[2].id,"one physical LOAD loads addressed deck");
  EP(!s.load_choice.mode&&xz_shell_screen(&s)==XZ_SHELL_PLAY,"physical LOAD has no chooser");
 }
}
static void visible_menu_rows(void){
 struct xz_shell s;fresh(&s);key(&s,XZ_SHELL_KEY_MENU);
 int row=2;struct xz_shell_rect r=xz_shell_menu_row_rect(&s,&sm,row);
 assert(r.w&&r.y+r.h<=480);
 size_t n=tap(&s,r.x+40,r.y+r.h/2);
 EP(n==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE,"one tap opens a visible menu link");
}
static void pending_navigation(void){
 static struct bx_library library;static struct fx_track t;bx_build(&library);fx_track_build(&t,1);
 static const int keys[]={XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_TRACK_FILTER,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_BACK};
 for(unsigned k=0;k<sizeof keys/sizeof *keys;k++){
  struct xz_shell s;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action a[XZ_SHELL_ACTIONS];
  fx_model_full(&m,&t);bx_tracks(&b,&library,XZ_SHELL_BROWSE);b.media_generation=7;m.browse=&b;m.deck_count=4;
  m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_FILTER|XZ_SHELL_CAP_TAG_LIST;
  xz_shell_init(&s);xz_shell_browse_reset();struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};xz_shell_input(&s,&m,&e,a);
  e.kind=XZ_SHELL_ENCODER_PUSH;xz_shell_input(&s,&m,&e,a);xz_shell_input(&s,&m,&e,a);
  EP(s.load_choice.mode!=0,"encoder opens extra-deck chooser");
  e.kind=XZ_SHELL_KEY;e.key=keys[k];xz_shell_input(&s,&m,&e,a);
  EP(!s.load_choice.mode,"navigation dismisses load chooser");
  EP(k==0?xz_shell_screen(&s)==XZ_SHELL_PLAY:k==1?s.panel.kind==XZ_SHELL_PANEL_FILTER:k==2?xz_shell_screen(&s)==XZ_SHELL_TAG_LIST:xz_shell_screen(&s)==XZ_SHELL_BROWSE,"same navigation press acts; BACK closes one level");
 }
}
static void top_back_return(void){
 static struct bx_library library;static struct xz_shell_browse_model b;bx_build(&library);bx_tracks(&b,&library,XZ_SHELL_BROWSE);
 struct xz_shell s;fresh(&s);sm.browse=&b;sm.capabilities|=XZ_SHELL_CAP_BROWSE;
 key(&s,XZ_SHELL_KEY_BROWSE);int prior=s.depth;
 key(&s,XZ_SHELL_KEY_MODS);push(&s);key(&s,XZ_SHELL_KEY_BACK);
 EP(xz_shell_screen(&s)==XZ_SHELL_BROWSE&&s.depth==prior,"top-level Flagship BACK returns to prior BROWSE in one press");
 sm.browse=NULL;
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);fx_track_build(&track,7);fx_model_full(&sm,&track);sm.settings_screen=fxs_reset();sm.mods=&mfx_mods;sm.menu=&menu;
 (void)&bx_folders;physical_load_parity();visible_menu_rows();pending_navigation();top_back_return();
 test_open();test_encoder_never_leaves();test_back();test_render_every_page();test_press_parity();test_touch();test_wild();
 printf("encoder parity: %u checks, %u failures\n",ep_checks,ep_failures);
 return ep_failures?1:0;
}
