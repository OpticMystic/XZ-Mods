/* SPDX-License-Identifier: MIT */
#define XZ_MENU_RUNTIME_MAIN retained_menu_runtime_main
#include "test_menu_runtime.c"
#undef main
static int same_focus(const struct xz_ui_widget *a,const struct xz_ui_widget *b){return a->kind==b->kind&&a->index==b->index&&a->deck==b->deck;}
int main(void){
 retained_menu_runtime_main();
 unsigned pages=0,controls=0,pushes=0;
 for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);
  xz_ui_init(&ui);ui.page=(enum xz_ui_page)page;ui.deck=0;ui.capture=-1;ui.down=0;
  started=1;play_key.open=0;touch.visible=1;touch.capture=0;menu_back_contact=0;
  __atomic_store_n(&visible,1,__ATOMIC_RELEASE);
  struct xz_ui_widget w[XZ_UI_WIDGETS],seen[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w),count=0;
  for(size_t i=0;i<n;i++)if(xz_ui_focusable(&w[i]))seen[count++]=w[i];
  unsigned char visited[XZ_UI_WIDGETS]={0};
  for(size_t step=0;step<count;step++){
   struct xz_ui_widget focused;assert(xz_ui_focused(&ui,&model,&focused));
   for(size_t i=0;i<count;i++)if(same_focus(&focused,&seen[i]))visited[i]=1;
   assert(encoder_rotate_hook(1)==1);
  }
  for(size_t i=0;i<count;i++){assert(visited[i]);controls++;}
  pages++;
 }
 /* One physical encoder push opens a child; release and hold never activate it again. BACK returns one level. */
 rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);
 touch.visible=1;ui.page=XZ_UI_EXTRAS;ui.focus_kind=XZ_UI_PANEL;ui.focus_index=XZ_UI_GAMES;ui.focus_deck=-1;
 uint32_t press[3]={1,0,2},release[3]={0,0,0},hold[3]={2,0,2};
 assert(encoder_push_hook(press,NULL,NULL,NULL)==1&&ui.page==XZ_UI_GAMES&&ui.history_count==1);pushes++;
 assert(encoder_push_hook(hold,NULL,NULL,NULL)==1&&ui.page==XZ_UI_GAMES);
 assert(encoder_push_hook(release,NULL,NULL,NULL)==1&&ui.page==XZ_UI_GAMES);
 unsigned char input[16]={0};input[8]=8;input[9]=0x42;menu_back_key_hook(NULL,input);input[11]=2;menu_back_key_hook(NULL,input);
 assert(ui.page==XZ_UI_EXTRAS&&!ui.history_count);
 input[11]=0;menu_back_key_hook(NULL,input);input[11]=2;menu_back_key_hook(NULL,input);
 assert(!touch.visible&&!__atomic_load_n(&visible,__ATOMIC_ACQUIRE));
 printf("PASS actual CLASSIC hooks: %u pages, %u visible controls reached by rotation; %u push opens child; BACK one level; hold/release inert\n",pages,controls,pushes);
 return 0;
}
