/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Stem layout acceptance requires assertions
#endif
#include "ui_skin.h"
#include "native_wave.h"
#include "layered_wave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t frame[536*268+8],scratch[536*268+8],buttons[540*96+8];
struct context {struct xz_ui u;struct xz_ui_model m;};
static int render(void *p,uint16_t *pixels,size_t count,size_t stride,int width,int height){
 struct context *c=p;return xz_ui_inline_render(&c->u,&c->m,pixels,count,stride,width,height);
}
static void save(const char *folder,int theme,int mode){
 if(!folder)return;
 char path[1024];snprintf(path,sizeof(path),"%s/theme-%d-layout-%d.ppm",folder,theme,mode);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n536 268\n255\n");
 for(unsigned i=0;i<536*268;i++){
  unsigned p=frame[i];unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  assert(fwrite(rgb,1,3,f)==3);
 }
 assert(!fclose(f));
}
static void native_frame(int theme){
 const struct xz_theme_palette *p=xz_theme_palette(theme);
 for(unsigned i=0;i<536*268;i++)frame[i]=xz_theme_rgb565(p->bg);
 for(int deck=0;deck<2;deck++)for(unsigned x=0;x<536;x++){
  unsigned char bands[3]={(unsigned char)(22+(x/13)%26),(unsigned char)(20+(x/17)%18),(unsigned char)(12+(x/11)%15)};
  struct xz_layered_data data={bands,1,120,XZ_LAYERED_STEMS,{xz_theme_rgb565(p->stem[0]),xz_theme_rgb565(p->stem[1]),xz_theme_rgb565(p->stem[2])}};
  assert(xz_layered_column(frame,536,536,268,x,deck?201:65,52,&data,0,1,0,0,NULL));
  for(int row=0;row<10;row++){
   frame[(deck*136+row)*536+x]=xz_theme_rgb565(xz_blend(p->bg,p->accent,40));
   frame[(deck*136+122+row)*536+x]=xz_theme_rgb565(xz_blend(p->bg,p->accent,70));
  }
 }
 for(unsigned i=536*268;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=scratch[i]=0xcafe;
}
int main(int argc,char **argv){
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int mode=0;mode<XZ_STEM_LAYOUT_COUNT;mode++){
  struct context c={0};xz_ui_init(&c.u);c.m.theme=theme;c.m.stems_overlay=mode;c.m.enabled=XZ_UI_STEM;
  for(int deck=0;deck<2;deck++){c.m.deck[deck].ready=XZ_UI_STEM;
   c.m.deck[deck].levels[0]=deck?.75f:1;c.m.deck[deck].levels[1]=deck?0:.5f;c.m.deck[deck].levels[2]=deck?.5f:.25f;}
  struct xz_wave_layout g;assert(xz_wave_layout_get(mode,&g));
  native_frame(theme);
  if(mode){
   struct xz_native_wave_pair pair={0};struct xz_native_wave_scene scene={1,0,0x4d200c,0x4d1f30,0x1fc2fc,0x1fdbe4,2,1,100,200,536,268,9,0x41000000,131,18,667,286,1};
   assert(xz_native_wave_capture(&pair,&scene,0x1fd058,7,frame,1072));
   assert(xz_native_wave_finish_styled(&pair,&scene,7,200,scratch,536*268,render,&c,NULL,mode));
   struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_inline_layout(&c.u,&c.m,536,(int)g.control_height*2,w);
   assert(n==8);
   for(size_t i=0;i<n;i++){
    struct xz_ui_widget a=w[i];struct xz_ui_action actions[XZ_UI_ACTIONS];
    assert(a.y>=a.deck*(int)g.control_height&&a.y+a.h<=(a.deck+1)*(int)g.control_height);
    if(a.kind==XZ_UI_MUTE){
     int body=a.w-4,filled=(int)(body*c.m.deck[a.deck].levels[a.index]+.5f);
     unsigned row=g.control_y[a.deck]+(unsigned)(a.y-a.deck*(int)g.control_height+3);
     uint16_t role=xz_theme_rgb565(xz_ui_model_stem_color(&c.m,a.index));
     assert(frame[row*536+a.x+25]==(filled>23?role:xz_theme_rgb565(xz_blend(xz_theme_palette(theme)->bg,xz_theme_palette(theme)->ink,20))));
     if(filled>10&&filled<body-10){
      assert(frame[row*536+a.x+filled-2]==role);
      assert(frame[row*536+a.x+filled+5]!=role);
     }
     /* A rightward slide increases this stem alone, leftward decreases it. */
     c.m.deck[a.deck].levels[a.index]=.5f;
     assert(!xz_ui_inline_touch(&c.u,&c.m,536,(int)g.control_height*2,a.x+30,a.y+8,1,actions));
     assert(xz_ui_inline_touch(&c.u,&c.m,536,(int)g.control_height*2,a.x+50,a.y+8,1,actions)==1);
     assert(actions[0].kind==XZ_UI_LEVEL&&actions[0].deck==a.deck&&actions[0].index==a.index&&actions[0].value>.5f);
     assert(xz_ui_inline_touch(&c.u,&c.m,536,(int)g.control_height*2,a.x+10,a.y+8,1,actions)==1&&actions[0].value<.5f);
     xz_ui_inline_touch(&c.u,&c.m,536,(int)g.control_height*2,0,0,0,actions);
    }
   }
   for(unsigned i=536*268;i<sizeof(frame)/sizeof(*frame);i++)assert(frame[i]==0xcafe&&scratch[i]==0xcafe);
   /* A padded row buffer must stay inside the selected height. */
   size_t count=540*g.control_height*2;
   for(size_t i=0;i<sizeof(buttons)/sizeof(*buttons);i++)buttons[i]=0xbeef;
   assert(xz_ui_inline_render(&c.u,&c.m,buttons,count,540,536,(int)g.control_height*2));
   for(unsigned y=0;y<g.control_height*2;y++)for(unsigned x=536;x<540;x++)assert(buttons[y*540+x]==0xbeef);
   for(size_t i=count;i<sizeof(buttons)/sizeof(*buttons);i++)assert(buttons[i]==0xbeef);
  }
  save(argc>1?argv[1]:NULL,theme,mode);
 }
 puts("PASS 24 themes x 5 layouts: compositor, proportional left-to-right fill, right/left gestures, both decks and buffer guards");
 return 0;
}
