/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Stem color acceptance requires assertions
#endif
#include "ui.h"
#include "layered_wave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t pixels[800*480];
static struct xz_ui_widget find(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 assert(n<=XZ_UI_WIDGETS);
 for(size_t i=0;i<n;i++){
  assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=404);
  if(w[i].kind==kind&&w[i].index==index)return w[i];
 }
 assert(0);return w[0];
}
static void save(const char *folder,int theme,const char *suffix){
 if(!folder)return;
 char path[1024];snprintf(path,sizeof(path),"%s/stem-colors-%d-%s.ppm",folder,theme,suffix);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(size_t i=0;i<800*480;i++){
  unsigned p=pixels[i];unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  assert(fwrite(rgb,1,3,f)==3);
 }
 assert(!fclose(f));
}
int main(int argc,char **argv){
 assert(xz_ui_stem_color(0,0)==0x2997ff);
 assert(xz_ui_stem_color(0,1)==0xff3b30);
 assert(xz_ui_stem_color(0,2)==0x30d158);
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  struct xz_ui u;struct xz_ui_model m={0};xz_ui_init(&u);m.theme=theme;
  u.page=XZ_UI_STEM_SECTION;
  struct xz_ui_widget link=find(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_STEM_COLORS);
  struct xz_ui_action actions[XZ_UI_ACTIONS];
  assert(xz_ui_touch(&u,&m,link.x+10,link.y+10,1,actions)==1&&u.page==XZ_UI_STEM_COLORS);
  xz_ui_touch(&u,&m,0,0,0,actions);
  assert(xz_ui_render(&u,&m,pixels,800*480,800));save(argc>1?argv[1]:NULL,theme,"default");
  for(int stem=0;stem<3;stem++)for(int choice=0;choice<XZ_STEM_COLOR_CHOICES;choice++){
   struct xz_ui_widget w=find(&u,&m,XZ_UI_STEM_COLOR,stem*XZ_STEM_COLOR_CHOICES+choice);
   assert(xz_ui_touch(&u,&m,w.x+w.w/2,w.y+w.h/2,1,actions)==1);
   assert(actions[0].kind==XZ_UI_STEM_COLOR&&actions[0].index==stem&&actions[0].value==choice);
   xz_ui_touch(&u,&m,0,0,0,actions);
   m.stem_colors[stem]=choice;
   assert(xz_ui_model_stem_color(&m,stem)==xz_stem_palette_color(choice,xz_ui_stem_color(theme,stem)));
   assert(xz_ui_render(&u,&m,pixels,800*480,800));
   assert(pixels[(w.y+14)*800+w.x+20]==xz_theme_rgb565(xz_ui_model_stem_color(&m,stem)));
  }
  m.stem_colors[0]=4;m.stem_colors[1]=5;m.stem_colors[2]=6;
  assert(xz_ui_render(&u,&m,pixels,800*480,800));save(argc>1?argv[1]:NULL,theme,"custom");
  unsigned char bands[3]={40,40,40};struct xz_layered_data data={bands,1,120,XZ_LAYERED_STEMS,{0,0,0}};
  for(int stem=0;stem<3;stem++)data.colors[stem]=xz_theme_rgb565(xz_ui_model_stem_color(&m,stem));
  struct xz_layered_col col;
  assert(xz_layered_column(pixels,800,800,480,10,65,52,&data,0,1,0,0,&col));
  assert(pixels[65*800+10]==data.colors[2]);
  assert(pixels[(65-col.h[2])*800+10]==data.colors[1]);
  assert(pixels[(65-col.h[1])*800+10]==data.colors[0]);
  find(&u,&m,XZ_UI_STEM_COLORS_RESET,0);
 }
 puts("PASS stem colors: all 24 skins, 648 selections, exact RGB swatches, waveform role order");
 return 0;
}
