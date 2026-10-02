#ifndef XZ_NATIVE_ASSET_THEME_H
#define XZ_NATIVE_ASSET_THEME_H
#include "themes.h"
#include "native_asset_roles.h"
#include "native_main_theme.h"
#include "native_lcars_assets.h"
#include "native_glass_theme.h"
#include "native_retro_theme.h"
#include <stdint.h>
#include <stddef.h>

/* A caller-owned private RGB565 copy. Strides/capacity are measured in pixels.
   Source must cover its declared rows. Destination must not overlap source.
   The LUT belongs to the caller and must contain all 65536 entries.
   IDs 77..86 are verified blank 128x36 backgrounds in UI/native1582pack.
   The final keyed column is excluded from the frame when wholly transparent.
   Return 0 for invalid input, 1 for copy/LUT, 2 for a restyled known asset. */
static inline int xz_native_asset_theme_render(
 unsigned asset_id,int theme,const uint16_t *source,int width,int height,
 size_t source_stride,uint16_t *dest,size_t dest_stride,size_t dest_capacity_pixels,
 const uint16_t *lut,int key_enabled,uint16_t key565,int selected) {
 if(!source||!dest||width<=0||height<=0)return 0;
 size_t w=(size_t)width,h=(size_t)height;
 if(source_stride<w||dest_stride<w)return 0;
 if(h-1>(SIZE_MAX-w)/source_stride||h-1>(SIZE_MAX-w)/dest_stride)return 0;
 size_t source_pixels=(h-1)*source_stride+w,dest_pixels=(h-1)*dest_stride+w;
 if(dest_pixels>dest_capacity_pixels||source_pixels>SIZE_MAX/sizeof(uint16_t)||dest_pixels>SIZE_MAX/sizeof(uint16_t))return 0;
 size_t source_bytes=source_pixels*sizeof(uint16_t),dest_bytes=dest_pixels*sizeof(uint16_t);
 uintptr_t source_start=(uintptr_t)source,dest_start=(uintptr_t)dest;
 if(source_start>UINTPTR_MAX-source_bytes||dest_start>UINTPTR_MAX-dest_bytes)return 0;
 if(source_start<dest_start+dest_bytes&&dest_start<source_start+source_bytes)return 0;
 theme=xz_theme_id(theme);
 if(theme!=0&&!lut)return 0;
 for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++){
  uint16_t pixel=source[y*source_stride+x];
  dest[y*dest_stride+x]=theme==0||(key_enabled&&pixel==key565)?pixel:lut[pixel];
 }
 struct xz_asset_role_info role=xz_native_asset_role(asset_id,(unsigned)width,(unsigned)height);
 const struct xz_theme *style=xz_theme(theme);
 int classic=style->chrome==XZ_CHROME_WIN95&&(role.role==XZ_ASSET_BUTTON||role.role==XZ_ASSET_TITLE_STRIP||role.role==XZ_ASSET_TRACK_PANEL);
 struct xz_theme_surface main_surface={dest,dest_stride,width,height,0,0,width,height};
 if(xz_native_glass_art(asset_id,theme,role,main_surface,width,height)||
    xz_native_retro_art(asset_id,theme,role,main_surface,width,height)){
  if(key_enabled)for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++)
   if(source[y*source_stride+x]==key565)dest[y*dest_stride+x]=key565;
  return 2;
 }
 if(theme==XZ_THEME_LCARS&&xz_lcars_asset_owned(asset_id,width,height)){
  if(key_enabled&&key565==0xf81f){xz_lcars_asset_draw(asset_id,theme,main_surface);return 2;}
  return 1;
 }
 if(xz_native_main_art(asset_id,theme,role,main_surface,width,height)){
  if(key_enabled)for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++)
   if(source[y*source_stride+x]==key565)dest[y*dest_stride+x]=key565;
  return 2;
 }
 if(theme==0||(!classic&&(asset_id<77||asset_id>86||width!=128||height!=36)))return 1;
 int frame_width=width;
 if(key_enabled){
  int keyed_column=1;
  for(size_t y=0;y<h;y++)if(source[y*source_stride+w-1]!=key565){keyed_column=0;break;}
  if(keyed_column)frame_width--;
 }
 struct xz_theme_surface surface={dest,dest_stride,width,height,0,0,width,height};
 const struct xz_theme_palette *palette=xz_theme_palette(theme);
 if(classic){
  struct xz_theme_rect whole={0,0,frame_width,height};
  xz_theme_frame(surface,theme,whole,style->face,0,XZ_THEME_BUTTON);
  /* Appearance variants keep their relative brightness so focused, dim and
     pale native states stay distinguishable inside the bevel. */
  uint32_t face=style->face;
  if(role.appearance==XZ_ASSET_LIGHT||role.appearance==XZ_ASSET_ORANGE_LIGHT_EDGE)face=style->hi2;
  else if(role.appearance==XZ_ASSET_DIM)face=xz_theme_mix(style->face,style->lo2,64);
  else if(role.appearance==XZ_ASSET_PALE)face=xz_theme_mix(style->hi2,style->hi,128);
  else if(role.appearance==XZ_ASSET_FLAT)face=xz_theme_mix(style->face,style->lo2,32);
  if(face!=style->face)xz_theme_fill(surface,(struct xz_theme_rect){2,2,frame_width-4,height-4},face);
  if(role.role==XZ_ASSET_TITLE_STRIP){
   xz_theme_fill(surface,(struct xz_theme_rect){3,3,2,height-6},style->title);
   uint16_t original=source[(size_t)(height/2)*source_stride+(size_t)(width/2)];
   uint16_t accent=lut[original];
   for(int y=3;y<height-3;y++)for(int x=frame_width-5;x<frame_width-3;x++)dest[(size_t)y*dest_stride+(size_t)x]=accent;
  }else if(role.appearance==XZ_ASSET_ORANGE_EDGE||role.appearance==XZ_ASSET_ORANGE_LIGHT_EDGE){
   xz_theme_edge(surface,whole,2,style->title);
  }else if(role.appearance==XZ_ASSET_WHITE_EDGE){
   xz_theme_dots(surface,whole,4,palette->ink);
  }
 }else{
 /* Keep the native outer focus border and interior state contrast. Decorations
    sit inside that border instead of making focused/disabled assets identical. */
 xz_theme_frame(surface,theme,(struct xz_theme_rect){3,3,frame_width-6,height-6},palette->bg,selected!=0,XZ_THEME_BUTTON);
 for(int y=9;y<height-9;y++)for(int x=9;x<frame_width-9;x++){
  uint16_t pixel=source[(size_t)y*source_stride+(size_t)x];
  dest[(size_t)y*dest_stride+(size_t)x]=key_enabled&&pixel==key565?pixel:lut[pixel];
 }
 }
 if(key_enabled)for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++)
  if(source[y*source_stride+x]==key565)dest[y*dest_stride+x]=key565;
 return 2;
}
#endif
