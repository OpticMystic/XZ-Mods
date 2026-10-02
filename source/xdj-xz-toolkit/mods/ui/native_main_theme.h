#ifndef XZ_NATIVE_MAIN_THEME_H
#define XZ_NATIVE_MAIN_THEME_H
#include "themes.h"
#include "native_asset_roles.h"
#include "pixel_font.h"
#include "font_atlas.h"

static inline void xz_main_round(struct xz_theme_surface s,int x,int y,int w,int h,int r,uint32_t color){
 xz_theme_rounded(s,(struct xz_theme_rect){x,y,w,h},r,color,color,256);
}
static inline void xz_main_text(struct xz_theme_surface s,int x,int y,const char *text,int scale,uint32_t color){
 for(;*text;text++,x+=6*scale)for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)
  if(xz_pixel_font_row((unsigned char)*text,row)&(16u>>col))xz_theme_fill(s,(struct xz_theme_rect){x+(int)col*scale,y+(int)row*scale,scale,scale},color);
}
static inline void xz_main_atlas(struct xz_theme_surface s,int x,int y,const char *text,int scale,uint32_t color){
 for(;*text;text++){
  const struct xz_font_glyph *g=&xz_font_glyphs[1][(unsigned char)*text-32];
  for(unsigned row=0;row<g->height;row++)for(unsigned col=0;col<g->width;col++){
   unsigned alpha=xz_font_coverage[g->offset+row*g->width+col];
   if(alpha)xz_theme_blend(s,(struct xz_theme_rect){x+(g->x+(int)col)*scale,y+(g->y+(int)row)*scale,scale,scale},color,alpha==255?256:alpha);
  }
  x+=g->advance*scale;
 }
}
/* Only verified blank native artwork. The original text, track counters,
   cue overlays and touch rectangles continue to belong to Pioneer. */
static inline int xz_native_main_art(unsigned id,int theme,struct xz_asset_role_info role,
 struct xz_theme_surface s,int width,int height){
 if(theme<XZ_THEME_LCARS||theme>XZ_THEME_ANALOG)return 0;
 int panel=role.role==XZ_ASSET_TRACK_PANEL,title=role.role==XZ_ASSET_TITLE_STRIP;
 int deck=id>=1026&&id<=1028&&width==120&&height==41;
 int compact=(id==725||id==726)&&width==400&&height==70;
 int fx=id==969&&width==120&&height==31;
 int idle=id==1487&&width==560&&height==92;
 if(!panel&&!title&&!deck&&!idle&&!compact&&!fx)return 0;
 const struct xz_theme_palette *p=xz_theme_palette(theme);
 struct xz_theme_rect whole={0,0,width,height};
 xz_theme_fill(s,whole,p->bg);
 if(fx){
  if(theme==XZ_THEME_LCARS){xz_main_round(s,0,0,width,height,15,p->ink);xz_main_round(s,6,3,width-12,height-6,11,p->bg);}
  else if(theme==XZ_THEME_MATRIX)xz_theme_matrix_bracket(s,whole,8,2,p->ink);
  else {xz_theme_brushed(s,whole,0xd9c9a3);xz_theme_fill(s,(struct xz_theme_rect){3,3,width-6,height-6},p->bg);}
  if(theme==XZ_THEME_MATRIX)xz_main_text(s,18,11,"BEAT FX",2,p->ink);
  else xz_main_atlas(s,28,8,"BEAT FX",1,p->ink);
  return 1;
 }
 if(compact){
  if(theme==XZ_THEME_LCARS){
   uint32_t rail=id==726?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER;
   xz_main_round(s,0,0,width,height,16,rail);
   xz_main_round(s,10,30,width-15,height-35,10,0);
   xz_theme_fill(s,(struct xz_theme_rect){width-5,0,5,30},0);
  }
  else if(theme==XZ_THEME_MATRIX)xz_theme_matrix_bracket(s,whole,12,2,p->ink);
  else xz_theme_bevel(s,whole,1,0xd9c9a3,0x584a30);
  return 1;
 }
 if(theme==XZ_THEME_LCARS){
  uint32_t rail=id==687||(id>=688&&id<=704)?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER;
  if(panel){
   /* Untagged tracks have no separate title-strip draw. Supply its paper here. */
   xz_main_round(s,0,0,400,30,16,rail);
   xz_theme_fill(s,(struct xz_theme_rect){0,15,400,15},rail);
   /* The 30 px title overlays the panel top; all readout interiors remain black. */
   xz_main_round(s,0,30,400,142,18,rail);
   xz_main_round(s,12,38,378,127,10,0);
   /* Native tempo sprites use the complete rightmost90 px; keep that well open. */
   xz_theme_fill(s,(struct xz_theme_rect){310,38,90,120},0);
   /* Connected title elbow with a segmented return below the cue strip. */
   xz_theme_fill(s,(struct xz_theme_rect){34,165,5,7},0);
   xz_theme_fill(s,(struct xz_theme_rect){286,165,5,7},0);
   /* Keep original cue-marker gutters and the deck-label row free of artwork. */
   xz_theme_fill(s,(struct xz_theme_rect){0,104,12,61},0);
   xz_theme_fill(s,(struct xz_theme_rect){8,34,120,20},0);
   /* Vertical gutters between native hot-cue, time and tempo fields. */
   xz_theme_fill(s,(struct xz_theme_rect){126,42,2,55},rail);
   xz_theme_fill(s,(struct xz_theme_rect){305,43,2,54},rail);
   xz_theme_fill(s,(struct xz_theme_rect){132,102,166,2},rail);
   xz_theme_fill(s,(struct xz_theme_rect){314,157,72,2},rail);
  }else if(title){
   xz_main_round(s,0,0,width,30,16,rail);
   xz_theme_fill(s,(struct xz_theme_rect){0,15,width,15},rail);
   /* Fixed 5 px separator after the native track-title clip. */
   xz_theme_fill(s,(struct xz_theme_rect){width-5,0,5,30},0);
  }else if(deck){
   uint32_t color=id==1027?p->accent:id==1028?0x805038:rail;
   xz_main_round(s,0,0,width,height,18,color);
   xz_main_round(s,10,5,width-15,height-10,12,0);
   xz_theme_fill(s,(struct xz_theme_rect){20,0,width-34,4},0);
  }else{
   /* Native unloaded artwork stays quiet; the play header owns the title. */
   xz_main_round(s,170,42,44,4,2,rail);
   xz_main_atlas(s,226,35,"XZ MODS",1,rail);
   xz_main_round(s,316,42,44,4,2,XZ_LCARS_NATIVE_VIOLET);
  }
 }else if(theme==XZ_THEME_MATRIX){
  uint32_t green=id==1028?0x147014:0x33cc33,tail=id==1028?0x062e06:0x1a801a;
  xz_theme_edge(s,whole,1,tail);
  xz_theme_matrix_bracket(s,whole,deck?9:18,3,id==1027?p->accent:green);
  if(panel){
   xz_theme_fill(s,(struct xz_theme_rect){126,40,1,59},tail);
   xz_theme_fill(s,(struct xz_theme_rect){305,40,1,59},tail);
   for(int x=134;x<300;x+=8)xz_theme_fill(s,(struct xz_theme_rect){x,101,5,1},green);
   xz_theme_fill(s,(struct xz_theme_rect){312,151,76,1},tail);
   for(int x=14;x<388;x+=14)xz_theme_fill(s,(struct xz_theme_rect){x,168,8,2},tail);
  }else if(title){
   xz_theme_fill(s,(struct xz_theme_rect){0,0,width,3},green);
   xz_theme_fill(s,(struct xz_theme_rect){29,4,1,22},tail);
  }else if(idle){
   xz_main_text(s,28,20,"XZ / PERFORMANCE TERMINAL",2,p->accent);
   xz_main_text(s,28,56,"MATRIX   vj.tools/xzmods",2,green);
   for(int k=0;k<6;k++)xz_theme_fill(s,(struct xz_theme_rect){width-18,12+k*12,6,7},k==0?p->accent:k<3?green:tail);
  }
 }else{
  uint32_t brass=0xd9c9a3,light=0xf2e6c9,dark=0x584a30;
  if(panel){
   xz_theme_brushed(s,whole,brass);
   xz_theme_fill(s,(struct xz_theme_rect){3,3,width-6,27},p->bg);
   /* Dark wells retain contrast for native text and the full summary wave. */
   xz_theme_fill(s,(struct xz_theme_rect){9,34,381,126},p->bg);
   xz_theme_bevel(s,(struct xz_theme_rect){8,33,384,129},0,dark,light);
   xz_theme_fill(s,(struct xz_theme_rect){127,39,3,59},dark);
   xz_theme_fill(s,(struct xz_theme_rect){306,39,3,59},dark);
   for(int x=16;x<385;x+=14)xz_theme_fill(s,(struct xz_theme_rect){x,166,1,4},dark);
  }else if(title){
   xz_theme_brushed(s,whole,brass);xz_theme_fill(s,(struct xz_theme_rect){3,3,width-6,height-6},p->bg);
   xz_theme_bevel(s,(struct xz_theme_rect){2,2,width-4,height-4},0,dark,light);
  }else if(deck){
   xz_theme_brushed(s,whole,id==1028?0x75684c:brass);xz_theme_fill(s,(struct xz_theme_rect){11,4,width-17,height-8},p->bg);
   xz_theme_fill(s,(struct xz_theme_rect){3,8,5,height-16},id==1027?p->accent:dark);
  }else{
   xz_theme_brushed(s,whole,brass);xz_theme_fill(s,(struct xz_theme_rect){12,8,width-24,height-16},p->bg);
   xz_main_text(s,32,21,"XZ / ANALOG TRANSPORT",3,light);
   xz_main_text(s,32,58,"vj.tools/xzmods",2,p->accent);
  }
  xz_theme_bevel(s,whole,0,light,dark);
 }
 return 1;
}
#endif
