#ifdef NDEBUG
#error Four-deck order control acceptance requires active assertions
#endif
#include "ui_skin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* DECK VIEW (it replaced FOUR-DECK WAVE ORDER): three pictures on SCREEN and three segments on 4 DECKS, in both
   interfaces (one menu). Touch and the encoder pick a view, the view the model holds is lit, and the old row is
   gone everywhere. */
static uint16_t frame[800*480+8];
static size_t views(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget out[3]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w),k=0;
 for(size_t i=0;i<n;i++){
  assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=404);
  assert(w[i].kind!=XZ_UI_FOUR_WAVE_ORDER);
  for(size_t j=i+1;j<n;j++)assert(w[i].x+w[i].w<=w[j].x||w[j].x+w[j].w<=w[i].x||w[i].y+w[i].h<=w[j].y||w[j].y+w[j].h<=w[i].y);
  if(w[i].kind==XZ_UI_DECK_VIEW){assert(k<3&&w[i].index==(int)k&&w[i].w>=100&&w[i].h>=40);out[k++]=w[i];}
 }
 return k;
}
int main(void){
 struct xz_ui u;struct xz_ui_model m={0};struct xz_ui_action a[XZ_UI_ACTIONS];
 m.deck[0].ready=m.deck[1].ready=0x3fff;m.enabled=0x3fff;m.route.state=XZ_UI_ROUTE_NOT_DETECTED;
 static const enum xz_ui_page pages[2]={XZ_UI_SCREEN,XZ_UI_FOUR_DECKS};
 for(int p=0;p<2;p++)for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int view=0;view<3;view++){
  xz_ui_init(&u);xz_ui_navigate(&u,pages[p]);m.theme=theme;m.deck_view=view;
  m.ui_interface=theme==XZ_THEME_FLAGSHIP?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC;m.modern_look.known=1;
  struct xz_ui_widget w[3];assert(views(&u,&m,w)==3);
  for(int k=0;k<3;k++){
   assert(xz_ui_widget_look(&u,&m,&w[k]).selected==(k==view));
   assert(xz_ui_touch(&u,&m,w[k].x+w[k].w/2,w[k].y+w[k].h/2,1,a)==1);
   assert(a[0].kind==XZ_UI_DECK_VIEW&&a[0].value==(float)k);
   xz_ui_touch(&u,&m,w[k].x+w[k].w/2,w[k].y+w[k].h/2,0,a);
   u.focus_kind=XZ_UI_DECK_VIEW;u.focus_index=k;u.focus_deck=-1;
   assert(xz_ui_encoder_push(&u,&m,a)==1&&a[0].kind==XZ_UI_DECK_VIEW&&a[0].value==(float)k);
   struct xz_ui_info info;assert(xz_ui_describe(&u,&m,&info)&&strstr(info.title,"DECK VIEW")&&strstr(info.body,"3 1 2 4"));
   assert(info.feature==XZ_DECK_CAN_DISPLAY&&(info.works_on&3u)==3u);
  }
  for(int i=0;i<8;i++)frame[800*480+i]=0xbeef;
  assert(xz_ui_render(&u,&m,frame,800*480,800));
  for(int i=0;i<8;i++)assert(frame[800*480+i]==0xbeef);
 }
 /* A loader without the four-deck connector offers no DECK VIEW: four decks could never show. */
 m.route.state=XZ_UI_ROUTE_NO_ENGINE;xz_ui_init(&u);xz_ui_navigate(&u,XZ_UI_SCREEN);
 {struct xz_ui_widget w[3];assert(views(&u,&m,w)==0);}
 puts("PASS DECK VIEW: SCREEN pictures and 4 DECKS segments, touch, encoder, both interfaces, no old wave order row, no-engine hiding");
}
