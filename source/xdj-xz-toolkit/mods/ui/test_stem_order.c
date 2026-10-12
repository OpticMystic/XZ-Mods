/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Stem order acceptance requires assertions
#endif
#include "ui.h"
#include "stem_pads.h"
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t pixels[800*480];
static struct xz_ui_widget find(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)return w[i];
 assert(0);return w[0];
}
static void save(const char *folder,int theme,int order){
 if(!folder)return;
 char path[1024];snprintf(path,sizeof(path),"%s/stem-order-%d-%d.ppm",folder,theme,order);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(size_t i=0;i<800*480;i++){
  unsigned p=pixels[i];unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  assert(fwrite(rgb,1,3,f)==3);
 }
 assert(!fclose(f));
}
int main(int argc,char **argv){
 static const int themes[]={0,7,21};
 for(size_t t=0;t<sizeof(themes)/sizeof(*themes);t++)for(int order=0;order<XZ_STEM_ORDER_COUNT;order++){
  struct xz_ui u;struct xz_ui_model m={0};xz_ui_init(&u);m.theme=themes[t];m.enabled=XZ_UI_STEM;m.stem_order=order;
  for(int deck=0;deck<2;deck++)m.deck[deck].ready=XZ_UI_STEM;
  u.page=XZ_UI_CONTROLS;
  struct xz_ui_widget button=find(&u,&m,XZ_UI_STEM_ORDER,0);
  assert(button.x>=0&&button.x+button.w<=800&&button.y+button.h<=404);
  struct xz_ui_action actions[XZ_UI_ACTIONS];
  assert(xz_ui_touch(&u,&m,button.x+button.w/2,button.y+button.h/2,1,actions)==1);
  assert(actions[0].kind==XZ_UI_STEM_ORDER&&actions[0].phase==XZ_UI_PRESS);
  xz_ui_touch(&u,&m,0,0,0,actions);
  assert(xz_ui_render(&u,&m,pixels,800*480,800));save(argc>1?argv[1]:NULL,themes[t],order);
  for(int col=0;col<3;col++){
   int stem=xz_stem_at(order,col);assert(stem==xz_stem_orders[order][col]);
   uint16_t chip=xz_theme_rgb565(xz_ui_model_stem_color(&m,stem));
   assert(pixels[(button.y+21)*800+button.x+12+col*60]==chip);
   struct xz_ui_widget pad=find(&u,&m,XZ_UI_STEM_PAD_CONFIG,col);
   assert(pixels[(pad.y+pad.h-8)*800+pad.x+pad.w/2]==chip);
  }
  struct xz_ui_widget rows[XZ_UI_WIDGETS];size_t n=xz_ui_inline_layout(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,rows);
  int column=0;
  for(size_t i=0;i<n;i++)if(rows[i].kind==XZ_UI_MUTE&&rows[i].deck==0){assert(rows[i].index==xz_stem_orders[order][column]);column++;}
  assert(column==3);
  struct xz_ui_info info;assert(xz_ui_describe(&u,&m,&info));
  assert(!strcmp(info.state,xz_stem_order_name(order)));
 }
 assert(xz_stem_at(XZ_STEM_ORDER_COUNT,0)==-1&&xz_stem_at(0,3)==-1&&!strcmp(xz_stem_order_name(99),xz_stem_order_name(0)));
 puts("PASS stem order: six orders x 3 themes, chips, pad diagram, inline rows and info state agree");
}
