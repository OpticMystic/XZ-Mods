#ifdef NDEBUG
#error Four-deck order control acceptance requires active assertions
#endif
#include "ui_skin.h"
#include "ui_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t frame[800*480+8];
static struct xz_ui_widget find_order(const struct xz_ui *u,const struct xz_ui_model *m){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++){
  assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=404);
  for(size_t j=i+1;j<n;j++)assert(w[i].x+w[i].w<=w[j].x||w[j].x+w[j].w<=w[i].x||w[i].y+w[i].h<=w[j].y||w[j].y+w[j].h<=w[i].y);
 }
 for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_FOUR_WAVE_ORDER){assert(w[i].w>=160&&w[i].h>=36);return w[i];}
 assert(!"missing waveform order control");return w[0];
}
int main(void){
 struct xz_ui u;struct xz_ui_model m={0};struct xz_ui_action a[XZ_UI_MENU_ACTIONS];
 xz_ui_init(&u);xz_ui_navigate(&u,XZ_UI_CONTROLS);
 m.deck[0].ready=m.deck[1].ready=0x3fff;m.enabled=0x3fff;
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int mixer=0;mixer<2;mixer++){
  m.theme=theme;m.four_wave_mixer=mixer;
  struct xz_ui_widget w=find_order(&u,&m);
  assert(strstr(w.label,mixer?"3 1 2 4":"1 2 3 4"));
  assert(xz_ui_widget_look(&u,&m,&w).selected==mixer);
  assert(xz_ui_touch(&u,&m,w.x+w.w/2,w.y+w.h/2,1,a)==1);
  assert(a[0].kind==XZ_UI_FOUR_WAVE_ORDER&&a[0].value==(float)!mixer);
  xz_ui_touch(&u,&m,w.x+w.w/2,w.y+w.h/2,0,a);
  u.focus_kind=XZ_UI_FOUR_WAVE_ORDER;u.focus_index=0;u.focus_deck=-1;
  assert(xz_ui_encoder_push(&u,&m,a)==1&&a[0].kind==XZ_UI_FOUR_WAVE_ORDER&&a[0].value==(float)!mixer);
  struct xz_shell_menu menu;assert(xz_ui_menu_build(&u,&m,&menu));int found=0;
  for(int i=0;i<menu.row_count;i++)if(!strcmp(menu.row[i].label,"FOUR-DECK WAVE ORDER")){
   assert(menu.row[i].current==mixer&&menu.row[i].choice_count==2);found=1;
  }
  assert(found);
  for(int i=0;i<8;i++)frame[800*480+i]=0xbeef;
  assert(xz_ui_render(&u,&m,frame,800*480,800));
  for(int i=0;i<8;i++)assert(frame[800*480+i]==0xbeef);
  if(theme==0){
   char name[64];snprintf(name,sizeof(name),"classic-wave-order-%d.ppm",mixer);FILE *f=fopen(name,"wb");assert(f);
   fprintf(f,"P6\n800 480\n255\n");for(int i=0;i<800*480;i++){
    uint16_t v=frame[i];unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
    assert(fwrite(rgb,1,3,f)==3);
   }
   assert(!fclose(f));
  }
 }
 puts("PASS shared order MODS button: touch, encoder, Modern menu, all-theme geometry/render guards");
}
