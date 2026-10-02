/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_RETRO_THEME_H
#define XZ_NATIVE_RETRO_THEME_H
#include "native_main_theme.h"
#include "native_title_theme.h"

static inline void xz_retro_title(struct xz_theme_surface s,int theme,int width,int height){
 const struct xz_theme *t=xz_theme(theme);
 xz_theme_fill(s,(struct xz_theme_rect){0,0,width,height},t->title);
 /* Flat title paper matches the per-glyph antialias background exactly. */
 xz_theme_fill(s,(struct xz_theme_rect){0,height-2,width,1},t->lo);
 xz_theme_fill(s,(struct xz_theme_rect){0,height-1,width,1},t->hi);
 if(t->chrome==XZ_CHROME_AQUA)xz_theme_fill(s,(struct xz_theme_rect){0,0,width,1},t->hi);
}

static inline void xz_retro_shell(struct xz_theme_surface s,int theme,struct xz_theme_rect r){
 const struct xz_theme *t=xz_theme(theme);
 if(t->chrome==XZ_CHROME_AQUA){
  xz_theme_brushed(s,r,t->title);
  xz_theme_bevel(s,r,0,t->hi,t->lo);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+2,r.y+2,r.w-4,1},t->hi2);
 }else if(t->chrome==XZ_CHROME_DMG_SHELL){
  xz_theme_fill(s,r,t->face);xz_theme_bevel(s,r,0,t->hi,t->lo);
  xz_theme_rounded(s,(struct xz_theme_rect){r.x+3,r.y+3,r.w-6,r.h-6},6,t->title,t->title,256);
 }else xz_theme_frame(s,theme,r,t->face,0,XZ_THEME_PANEL);
}

/* Native glyph ink is still mapped independently. Keep its original paper
 * polarity until both paper and glyphs can be changed together. */
static inline void xz_retro_well(struct xz_theme_surface s,int theme,struct xz_theme_rect r){
 const struct xz_theme *t=xz_theme(theme);
 xz_theme_fill(s,r,t->palette.bg);
 if(t->chrome==XZ_CHROME_WIN95){
  xz_theme_bevel(s,r,0,t->lo2,t->hi);xz_theme_bevel(s,r,1,t->lo,t->hi2);
 }else if(t->chrome==XZ_CHROME_GB_LCD||t->chrome==XZ_CHROME_GBC_BOX){
  xz_theme_edge(s,r,0,t->palette.ink);xz_theme_edge(s,r,2,t->palette.ink);
 }else xz_theme_bevel(s,r,0,t->lo,t->hi);
}

/* Only exact 1.26 blank artwork is owned. The caller preserves source keys;
 * this routine never changes native touch positions, text or numeric values. */
static inline int xz_native_retro_art(unsigned id,int theme,struct xz_asset_role_info role,
 struct xz_theme_surface s,int width,int height){
 if(theme<7||theme>18||width<=0||height<=0)return 0;
 struct xz_asset_role_info exact=xz_native_asset_role(id,(unsigned)width,(unsigned)height);
 if(exact.role!=role.role||exact.appearance!=role.appearance)return 0;
 int panel=exact.role==XZ_ASSET_TRACK_PANEL,title=exact.role==XZ_ASSET_TITLE_STRIP;
 int button=exact.role==XZ_ASSET_BUTTON,field=exact.role==XZ_ASSET_FIELD;
 int deck=id>=1026&&id<=1028&&width==120&&height==41;
 int compact=(id==725||id==726)&&width==400&&height==70;
 int fx=id==969&&width==120&&height==31;
 if(!panel&&!title&&!button&&!field&&!deck&&!compact&&!fx)return 0;
 const struct xz_theme *t=xz_theme(theme);const struct xz_theme_palette *p=&t->palette;
 struct xz_theme_rect all={0,0,width,height};xz_theme_fill(s,all,p->bg);
 if(panel){
  xz_retro_shell(s,theme,all);
  /* Top paper also handles untagged tracks with no separate title strip. */
  xz_retro_title(s,theme,400,30);
  xz_theme_fill(s,(struct xz_theme_rect){8,33,392,20},p->bg);
  xz_retro_well(s,theme,(struct xz_theme_rect){8,51,120,51});
  xz_retro_well(s,theme,(struct xz_theme_rect){128,51,180,51});
  /* Pitch uses the full rightmost90px, including its sign and punctuation. */
  xz_theme_fill(s,(struct xz_theme_rect){310,38,90,126},p->bg);
  xz_theme_fill(s,(struct xz_theme_rect){0,104,310,61},p->bg);
  xz_theme_fill(s,(struct xz_theme_rect){12,168,286,2},t->lo);
  if(t->chrome==XZ_CHROME_DMG_SHELL){
   xz_theme_fill(s,(struct xz_theme_rect){12,30,120,2},t->hi2);
   xz_theme_fill(s,(struct xz_theme_rect){136,30,116,2},t->lo2);
   for(int i=0;i<5;i++)xz_theme_fill(s,(struct xz_theme_rect){318+i*14,166,3,5},t->lo2);
  }else if(t->chrome==XZ_CHROME_SNES_PAD){
   const uint32_t abxy[4]={p->stem[0],p->alarm,p->stem[2],p->stem[1]};
   for(int i=0;i<4;i++)xz_theme_rounded(s,(struct xz_theme_rect){323+i*16,165,6,6},3,abxy[i],abxy[i],256);
  }else if(t->chrome==XZ_CHROME_AQUA){
   for(int x=15;x<295;x+=7)xz_theme_fill(s,(struct xz_theme_rect){x,166,3,1},t->hi);
  }
  return 1;
 }
 if(title){
  xz_retro_title(s,theme,width,height);
  if(t->chrome==XZ_CHROME_GB_LCD||t->chrome==XZ_CHROME_GBC_BOX)
   for(int x=0;x<width;x+=8)xz_theme_fill(s,(struct xz_theme_rect){x,height-3,4,1},p->ink);
  return 1;
 }
 if(field){xz_retro_well(s,theme,all);return 1;}
 if(compact){
  xz_retro_shell(s,theme,all);xz_theme_fill(s,(struct xz_theme_rect){4,4,width-8,height-8},p->bg);
  xz_retro_title(s,theme,width,30);return 1;
 }
 if(fx){
  xz_retro_shell(s,theme,all);xz_theme_fill(s,(struct xz_theme_rect){6,5,width-12,height-10},p->bg);
  if(t->font==XZ_FONT_PIXEL)xz_main_text(s,38,12,"BEAT FX",1,p->ink);
  else xz_main_atlas(s,28,8,"BEAT FX",1,p->ink);
  return 1;
 }
 if(deck){
  xz_retro_shell(s,theme,all);
  xz_theme_fill(s,(struct xz_theme_rect){12,5,width-24,height-10},p->bg);
  uint32_t rail=id==1027?p->accent:id==1028?xz_theme_mix(p->bg,p->ink,60):p->ink;
  xz_theme_fill(s,(struct xz_theme_rect){4,7,4,height-14},rail);
  if(id==1027)xz_theme_edge(s,all,1,p->accent);
  return 1;
 }
 int focus=exact.appearance==XZ_ASSET_ORANGE_EDGE||exact.appearance==XZ_ASSET_ORANGE_LIGHT_EDGE||exact.appearance==XZ_ASSET_WHITE_EDGE;
 int light=exact.appearance==XZ_ASSET_LIGHT||exact.appearance==XZ_ASSET_ORANGE_LIGHT_EDGE||exact.appearance==XZ_ASSET_PALE;
 xz_theme_frame(s,theme,all,t->face,0,XZ_THEME_BUTTON);
 if(light)xz_theme_blend(s,(struct xz_theme_rect){5,5,width-10,height-10},p->accent,40);
 if(exact.appearance==XZ_ASSET_DIM)xz_theme_blend(s,all,p->bg,120);
 if(focus){
  if(t->chrome==XZ_CHROME_AQUA)xz_theme_rounded_ring(s,all,height/2,2,p->accent,256);
  else xz_theme_edge(s,all,2,p->accent);
 }
 return 1;
}
#endif
