#ifndef XZ_NATIVE_GLASS_THEME_H
#define XZ_NATIVE_GLASS_THEME_H
#include "themes.h"
#include "native_asset_roles.h"

/* These materials are built once in the native asset snapshot. Numeric wells
   keep the exact palette paper used by Pioneer's dynamic text and waveforms. */
static inline void xz_glass_material(struct xz_theme_surface s,struct xz_theme_rect r,
 int dark,uint32_t tint,int focused,int dim){
 uint32_t top=dark?0x354355:0xfafdff,bottom=dark?0x18212d:0xdde5ef;
 uint32_t rim=dark?0x6d839e:0xffffff,shadow=dark?0x080b11:0xaebdcc;
 if(dim){top=xz_theme_mix(top,dark?0x111720:0xdbe1e8,128);bottom=xz_theme_mix(bottom,top,128);}
 top=xz_theme_mix(top,tint,focused?45:12);
 bottom=xz_theme_mix(bottom,tint,focused?42:10);
 int radius=r.h>60?14:r.h>28?9:6;
 xz_theme_rounded(s,r,radius,shadow,shadow,256);
 struct xz_theme_rect inset={r.x+1,r.y+1,r.w-2,r.h-3};
 xz_theme_rounded(s,inset,radius-1,top,bottom,256);
 xz_theme_rounded_ring(s,inset,radius-1,1,rim,focused?210:130);
 /* A narrow reflected edge, kept out of the title/data text area. */
 xz_theme_fill(s,(struct xz_theme_rect){r.x+radius,r.y+2,r.w-2*radius,1},rim);
 if(focused)xz_theme_rounded_ring(s,r,radius,2,tint,256);
}
static inline void xz_glass_well(struct xz_theme_surface s,struct xz_theme_rect r,int theme){
 const struct xz_theme *t=xz_theme(theme);
 xz_theme_rounded(s,r,5,t->palette.bg,t->palette.bg,256);
 xz_theme_rounded_ring(s,r,5,1,t->dark?0x435165:0xbdcbdc,160);
}
static inline int xz_native_glass_art(unsigned id,int theme,struct xz_asset_role_info role,
 struct xz_theme_surface s,int width,int height){
 if(theme!=19&&theme!=20)return 0;
 int panel=role.role==XZ_ASSET_TRACK_PANEL,title=role.role==XZ_ASSET_TITLE_STRIP;
 int compact=(id==725||id==726)&&width==400&&height==70;
 int deck=(id>=1026&&id<=1028)&&width==120&&height==41;
 if(!panel&&!title&&!compact&&!deck&&role.role!=XZ_ASSET_BUTTON&&role.role!=XZ_ASSET_FIELD)return 0;
 const struct xz_theme *t=xz_theme(theme);uint32_t paper=t->palette.bg;
 uint32_t accent=(id==687||id==726||(id>=688&&id<=704))?0x8765ec:t->palette.accent;
 struct xz_theme_rect all={0,0,width,height};
 xz_theme_fill(s,all,paper);
 if(panel){
  xz_glass_material(s,all,t->dark,accent,0,0);
  /* Preserve title paper even when an untagged track skips its title asset. */
  xz_glass_material(s,(struct xz_theme_rect){0,0,400,30},t->dark,accent,0,0);
  xz_theme_fill(s,(struct xz_theme_rect){7,7,3,16},accent);
  xz_theme_fill(s,(struct xz_theme_rect){8,32,382,22},paper);
  xz_glass_well(s,(struct xz_theme_rect){128,52,179,47},theme);
  xz_glass_well(s,(struct xz_theme_rect){11,53,116,48},theme);
  xz_glass_well(s,(struct xz_theme_rect){310,52,83,86},theme);
  xz_theme_fill(s,(struct xz_theme_rect){310,38,90,126},paper);
  xz_theme_fill(s,(struct xz_theme_rect){0,103,310,61},paper);
  xz_theme_fill(s,(struct xz_theme_rect){310,139,90,26},paper);
  return 1;
 }
 if(title){
  xz_glass_material(s,all,t->dark,accent,0,0);
  xz_theme_fill(s,(struct xz_theme_rect){7,7,3,height-14},accent);
  return 1;
 }
 if(compact){
  xz_glass_material(s,all,t->dark,accent,0,0);
  xz_glass_material(s,(struct xz_theme_rect){0,0,width,30},t->dark,accent,0,0);
  xz_theme_fill(s,(struct xz_theme_rect){6,31,width-12,height-36},paper);
  return 1;
 }
 if(deck){
  xz_glass_material(s,all,t->dark,accent,id==1027,id==1028);
  return 1;
 }
 if(role.role==XZ_ASSET_FIELD){xz_glass_well(s,all,theme);return 1;}
 int focused=role.appearance==XZ_ASSET_ORANGE_EDGE||role.appearance==XZ_ASSET_ORANGE_LIGHT_EDGE||role.appearance==XZ_ASSET_WHITE_EDGE;
 int dim=role.appearance==XZ_ASSET_DIM;
 int light=role.appearance==XZ_ASSET_LIGHT||role.appearance==XZ_ASSET_ORANGE_LIGHT_EDGE||role.appearance==XZ_ASSET_PALE;
 xz_glass_material(s,all,t->dark,accent,focused,dim);
 if(light)xz_theme_rounded_ring(s,(struct xz_theme_rect){3,3,width-6,height-6},6,1,accent,180);
 return 1;
}
#endif
