/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error PLAY key acceptance requires active assertions
#endif
#include "play_key.h"
#include "ui_menu.h"
#include "../shell/shell_internal.h"
#include <assert.h>
#include <stdio.h>

static struct xz_ui_model model;
static struct xz_ui_action act;
static uint16_t painted[800*480];
static void draw_popup(struct xz_play_key *p){
 struct xz_play_key_view view;xz_play_key_view(p,&model,NULL,&view);
 xz_play_key_render(p,&view,painted,800);xz_play_key_published(p,p);
}
static int click(struct xz_play_key *p,int which){
 struct xz_ui_widget w=xz_play_key_button(which);
 assert(!xz_play_key_touch(p,&model,w.x+w.w/2,w.y+20,1,&act));
 return xz_play_key_touch(p,&model,0,0,0,&act);
}
static void controls(void){
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int d=0;d<4;d++){
  struct xz_play_key p;model.theme=theme;
  for(int k=0;k<2;k++)model.deck[k].ready=XZ_UI_KEY|XZ_UI_KEYSYNC;
  xz_play_key_open(&p,d);assert(!xz_play_key_touch(&p,&model,0,0,0,&act));
  draw_popup(&p);
  for(int i=0;i<4;i++){
   int got=click(&p,i);assert(got==(d<2));
   if(got){assert(act.deck==d&&act.kind==(i==3?XZ_UI_KEY_SYNC:XZ_UI_KEY_SHIFT));assert(act.index==(i==3?0:i-1));}
  }
  assert(!click(&p,4)&&p.setup==(d>1));
  if(d>1){for(int s=0;s<4;s++)assert(!click(&p,4));assert(p.setup&&p.step==0);draw_popup(&p);assert(!click(&p,1)&&p.deck==1&&!p.setup);}
  assert(!click(&p,5)&&!p.open);
 }
 struct xz_play_key p;model.deck[0].ready=0;xz_play_key_open(&p,0);xz_play_key_touch(&p,&model,0,0,0,&act);
 for(int i=0;i<4;i++)assert(!click(&p,i));
 assert(!click(&p,4)&&p.setup&&p.step==0);
 for(int i=0;i<3;i++){assert(click(&p,4)&&act.kind==XZ_UI_KEY_CHECK&&act.deck==0);}
 assert(p.step==3&&click(&p,4)&&p.setup); /* no fake live verification */
 model.deck[0].ready=XZ_UI_KEY|XZ_UI_KEYSYNC;assert(!click(&p,4)&&!p.setup);
 draw_popup(&p);
 /* Sliding off a button cancels the whole contact, including a zero-coordinate release. */
 struct xz_ui_widget w=xz_play_key_button(2);
 xz_play_key_touch(&p,&model,w.x+8,w.y+8,1,&act);xz_play_key_touch(&p,&model,10,10,1,&act);
 xz_play_key_touch(&p,&model,w.x+8,w.y+8,1,&act);assert(!xz_play_key_touch(&p,&model,0,0,0,&act));
 xz_play_key_touch(&p,&model,10,10,1,&act);assert(!p.open);xz_play_key_touch(&p,&model,0,0,0,&act);
 puts("PASS popover: 24 themes, decks 1/2 existing actions, decks 3/4 blocked, setup wizard live gate, hidden actions inert, cancelled contacts");
}
static void geometry(void){
 struct xz_shell u;struct xz_shell_model m={0},view;
 for(int count=2;count<=4;count+=2)for(int mode=2;mode<=4;mode+=2)for(int sides=0;sides<4;sides++)for(int order=0;order<2;order++){
  xz_shell_init(&u);u.play_decks=mode;m.deck_count=count;m.sides_extra=(uint8_t)sides;m.wave_order=order;
  assert(xz_shell_render_frame(&u,&m,painted,800));
  xz_shell_play_projection(&u,&m,&view);
  for(int d=0;d<count;d++){
   struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,d);
   if(!sh_play_visible(&view,d)){assert(!r.w);continue;}
   assert(r.w&&r.h==44);
   for(int y=r.y;y<r.y+r.h;y++)assert(xz_shell_play_key_deck(&u,&m,r.x+8,y)==d);
   for(int other=0;other<count;other++)if(other!=d){
    struct xz_shell_rect q=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,other);
    assert(!q.w||r.y+r.h<=q.y||q.y+q.h<=r.y);
   }
  }
  u.stack[0].screen=XZ_SHELL_BROWSE;assert(xz_shell_play_key_deck(&u,&m,8,100)==-1);
 }
 puts("PASS logical key touch mapping: 2/4 views, all four selected pairs, numeric/mixer wave order, every pixel of each key target, no overlaps");
}
static void menus(void){
 static const int roots[]={XZ_UI_GENERAL,XZ_UI_CONTROLS,XZ_UI_THEMES,XZ_UI_SETTINGS,XZ_UI_EXTRAS};
 for(int interface=0;interface<2;interface++)for(int root=0;root<5;root++){
  struct xz_ui queue[128];int head=0,tail=1,seen[XZ_UI_PAGE_COUNT]={0};xz_ui_init(&queue[0]);queue[0].page=roots[root];
  model.ui_interface=interface;model.theme=interface?XZ_THEME_FLAGSHIP:0;
  while(head<tail){
   struct xz_ui u=queue[head++];if(seen[u.page]++)continue;
   assert(u.page!=XZ_UI_KEY_SETUP);
#ifdef XZ_R1_REMOVE_RAM
   assert(u.page!=XZ_UI_RAM_SETUP);
#endif
   struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&model,w);
   struct xz_shell_menu menu;assert(xz_ui_menu_build(&u,&model,&menu));
   for(int r=0;r<menu.row_count;r++)assert(strcmp(menu.row[r].label,"KEY SETUP")&&strcmp(menu.row[r].label,"KEY -")&&strcmp(menu.row[r].label,"KEY +")&&strcmp(menu.row[r].label,"KEY SYNC"));
   for(size_t i=0;i<n;i++){
    assert(w[i].kind!=XZ_UI_KEY_SHIFT&&w[i].kind!=XZ_UI_KEY_SYNC);
#ifdef XZ_R1_REMOVE_RAM
    assert(w[i].kind!=XZ_UI_RAM_REQUEST&&w[i].kind!=XZ_UI_RAM_CANCEL&&w[i].kind!=XZ_UI_RAM_RELEASE);
#endif
    if(w[i].kind==XZ_UI_PANEL||w[i].kind==XZ_UI_FEATURE_LINK){
     assert(w[i].index!=XZ_UI_KEY_SETUP);
#ifdef XZ_R1_REMOVE_RAM
     assert(w[i].index!=XZ_UI_RAM_SETUP);
#endif
     if(!seen[w[i].index]&&tail<128){queue[tail]=u;xz_ui_navigate(&queue[tail++],(enum xz_ui_page)w[i].index);}
    }
   }
  }
 }
 struct xz_ui u;xz_ui_init(&u);u.page=XZ_UI_THEMES;
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&model,w);int found=0;
 for(size_t i=0;i<n;i++)found|=w[i].kind==XZ_UI_PANEL&&w[i].index==XZ_UI_BROWSE_SETUP;
 assert(found);u.page=XZ_UI_BROWSE_SETUP;n=xz_ui_layout(&u,&model,w);found=0;
 for(size_t i=0;i<n;i++)found+=w[i].kind==XZ_UI_KEY_FORMAT;
 assert(found==3);
 puts("PASS real CLASSIC layouts and MODERN menu adapter: no KEY page/actions; Appearance retains all key formats");
#ifdef XZ_R1_REMOVE_RAM
 puts("PASS DEPENDENT VARIANT ONLY: no reachable RAM page or manual request/cancel/release controls");
#endif
}
int main(void){controls();geometry();menus();return 0;}
