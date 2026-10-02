#ifndef XZ_UI_THEMES_H
#define XZ_UI_THEMES_H
#include <stddef.h>
#include <stdint.h>
#include "theme_draw.h"

/* Theme ids are persisted on users' USB sticks as theme=N. Existing ids keep
   their meaning forever; new themes append. A light/dark pair shares a family. */
#define XZ_THEME_COUNT 24
#define XZ_THEME_LCARS 21
#define XZ_THEME_MATRIX 22
#define XZ_THEME_ANALOG 23
struct xz_theme_palette { uint32_t bg,ink,accent,alarm,stem[3]; };
enum xz_theme_chrome { XZ_CHROME_FLAT, XZ_CHROME_GB_LCD, XZ_CHROME_DMG_SHELL, XZ_CHROME_SNES_PAD, XZ_CHROME_RPG_WINDOW,
                       XZ_CHROME_WIN95, XZ_CHROME_GBC_BOX, XZ_CHROME_AQUA, XZ_CHROME_GLASS };
enum xz_theme_font { XZ_FONT_ATLAS, XZ_FONT_PIXEL };
enum xz_theme_pattern { XZ_PATTERN_NONE, XZ_PATTERN_DITHER, XZ_PATTERN_TILE_GRID, XZ_PATTERN_PINSTRIPE, XZ_PATTERN_MESH };
/* How the stock deck screen is recoloured through the 64K LUT. */
enum xz_theme_map { XZ_MAP_RAMP, XZ_MAP_TONES, XZ_MAP_WIN95 };
enum xz_theme_role { XZ_THEME_BUTTON, XZ_THEME_PANEL, XZ_THEME_HEADER, XZ_THEME_FOOTER, XZ_THEME_LIST };
#include "theme_lcars.h"
#include "theme_matrix.h"
#include "theme_analog.h"
struct xz_theme {
 const char *name;
 uint8_t family,dark;
 enum xz_theme_chrome chrome;
 enum xz_theme_font font;
 enum xz_theme_pattern pattern;
 enum xz_theme_map map;
 struct xz_theme_palette palette;
 /* Chrome colours. face: raised control fill. hi/lo: outer bevel or rim.
    hi2/lo2: inner bevel. title/title_ink: header band. well/well_ink: status
    well (LCD, sunken bar). desk: page colour behind the window. sel_ink: text
    on a selected control (0 = ink). shadow: text drop shadow (0 = none).
    tones: TONES/WIN95 grey ladder, light to dark; mesh pools for GLASS. */
 uint32_t face,hi,hi2,lo,lo2,title,title_ink,well,well_ink,desk,sel_ink,shadow;
 uint32_t tones[4];
};
static inline int xz_theme_id(int theme) { return theme>=0&&theme<XZ_THEME_COUNT?theme:0; }
static inline const struct xz_theme *xz_theme(int theme) {
 static const struct xz_theme table[XZ_THEME_COUNT]={
 {"ORIGINAL",0,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x000000,0xf4f5f6,0xa8ceff,0xffa000,{0xff3b30,0x2997ff,0x30d158}},.face=0},
 {"WHITE",1,0,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0xf0f0f0,0x141414,0x176398,0xa35400,{0xc52727,0x125eae,0x16753d}},.face=0},
 {"CYBERPUNK",2,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x00060e,0xdff3f7,0x54c1e6,0xfee801,{0xff2e88,0x54c1e6,0x2bf58a}},.face=0},
 {"NEON",3,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x0b0d17,0xc9d1d9,0x00e5ff,0xff9100,{0xff2daa,0x00e5ff,0x7c4dff}},.face=0},
 {"MOCHA",4,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x1e1e2e,0xcdd6f4,0xcba6f7,0xfab387,{0xf38ba8,0x89b4fa,0xa6e3a1}},.face=0},
 {"AURORA",5,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x141414,0xf0fef9,0x00e575,0xd451ff,{0xff4fc3,0x006afb,0x00e575}},.face=0},
 {"SANDSTONE",6,0,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0xfbf0d9,0x262a44,0x393f61,0xfdb03f,{0xe8705d,0x393f61,0x869a5f}},.face=0},
 /* 7: the DMG LCD, four sage tones. Lows/mids/highs map to tones 1/2/3. */
 {"GAME BOY",7,0,XZ_CHROME_GB_LCD,XZ_FONT_PIXEL,XZ_PATTERN_DITHER,XZ_MAP_TONES,{0xb5c98b,0x1d2b20,0x4d6041,0x1d2b20,{0x4d6041,0x8a9f68,0x1d2b20}},
  .face=0xb5c98b,.hi=0xb5c98b,.lo=0x1d2b20,.title=0xb5c98b,.title_ink=0x1d2b20,.well=0xb5c98b,.well_ink=0x1d2b20,.tones={0xb5c98b,0x8a9f68,0x4d6041,0x1d2b20}},
 /* 8: the console. US SNES grey body, lavender/purple switches, Super Famicom ABXY colours as the stem vocabulary. */
 {"SUPER NINTENDO",8,0,XZ_CHROME_SNES_PAD,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0xd2d2d6,0x26262e,0x5b4ea6,0xd9a520,{0xe0332e,0x2c5db8,0x2e9e4f}},
  .face=0xe6e6ea,.hi=0xf8f8fa,.hi2=0x8c84c8,.lo=0x84848c,.lo2=0x5b4ea6,.title=0xb4b4bc,.title_ink=0x26262e,.well=0xc2c2c8,.well_ink=0x26262e,.desk=0xd2d2d6,.sel_ink=0xffffff},
 /* 9: Windows 95 classic scheme. Face c0c0c0, bevels ffffff/dfdfdf/808080/000000, navy title, teal desktop. */
 {"WINDOWS 95",9,0,XZ_CHROME_WIN95,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_WIN95,{0xc0c0c0,0x000000,0x000080,0x800000,{0x800000,0x000080,0x008000}},
  .face=0xc0c0c0,.hi=0xffffff,.hi2=0xdfdfdf,.lo=0x000000,.lo2=0x808080,.title=0x000080,.title_ink=0xffffff,.well=0xffffff,.well_ink=0x000000,.desk=0x008080,.sel_ink=0x000000,
  .tones={0xc0c0c0,0x808080,0x404040,0x000000}},
 /* 10: Pokemon Gold/Silver text boxes. White box, dark double border, saturated primaries. */
 {"GAME BOY COLOR",10,0,XZ_CHROME_GBC_BOX,XZ_FONT_PIXEL,XZ_PATTERN_TILE_GRID,XZ_MAP_RAMP,{0xd0e8b8,0x181818,0x2878f8,0xd82820,{0xf85040,0x2878f8,0x30b048}},
  .face=0xf8f8f8,.hi=0xf8f8f8,.lo=0x282828,.title=0xf8f8f8,.title_ink=0x181818,.well=0xf8f8f8,.well_ink=0x181818,.desk=0xd0e8b8},
 /* 11: iTunes 4-6. Pinstripes, brushed-metal title, blue gel, LCD status well. */
 {"AQUA / ITUNES",11,0,XZ_CHROME_AQUA,XZ_FONT_ATLAS,XZ_PATTERN_PINSTRIPE,XZ_MAP_RAMP,{0xf2f2f2,0x1c1c1c,0x3875d7,0xd94f3d,{0xd8443a,0x3875d7,0x3fa34d}},
  .face=0xf4f7fb,.hi=0xffffff,.hi2=0xedf3fe,.lo=0x7d8aa0,.lo2=0x2f5aa8,.title=0xbcbcbc,.title_ink=0x202020,.well=0xdfe7d8,.well_ink=0x2a3a2a,.desk=0xe9e9e9,.sel_ink=0xffffff},
 /* 12: the backlit LCD. Same four-tone ladder, inverted (the classic emulator greens). */
 {"GAME BOY DARK",7,1,XZ_CHROME_GB_LCD,XZ_FONT_PIXEL,XZ_PATTERN_DITHER,XZ_MAP_TONES,{0x0f380f,0x9bbc0f,0x8bac0f,0x9bbc0f,{0x8bac0f,0x306230,0x9bbc0f}},
  .face=0x0f380f,.hi=0x0f380f,.lo=0x9bbc0f,.title=0x0f380f,.title_ink=0x9bbc0f,.well=0x0f380f,.well_ink=0x9bbc0f,.tones={0x0f380f,0x306230,0x8bac0f,0x9bbc0f}},
 /* 13: the 16-bit RPG menu window. Navy-to-blue gradient, white rounded bevel, shadowed white text, triangle cursor. */
 {"SUPER NINTENDO DARK",8,1,XZ_CHROME_RPG_WINDOW,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x000010,0xffffff,0x9fb8ff,0xffd75a,{0xff6a5a,0x6aa0ff,0x7ee08a}},
  .face=0x2a48b0,.hi=0xf4f4f4,.hi2=0xa0a0b8,.lo=0x101020,.lo2=0x101020,.title=0x2a48b0,.title_ink=0xffffff,.well=0x0e1858,.well_ink=0xffffff,.desk=0x000010,.sel_ink=0xffffff,.shadow=0x101020},
 /* 14: Windows 95 High Contrast Black scheme (values from memory: black face, white/grey bevels, purple title and selection). */
 {"WINDOWS 95 DARK",9,1,XZ_CHROME_WIN95,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_WIN95,{0x000000,0xffffff,0x800080,0xffff00,{0xff0000,0x0000ff,0x00ff00}},
  .face=0x000000,.hi=0xffffff,.hi2=0xc0c0c0,.lo=0xc0c0c0,.lo2=0x808080,.title=0x800080,.title_ink=0xffffff,.well=0x000000,.well_ink=0xffffff,.desk=0x000000,.sel_ink=0xffffff,
  .tones={0x000000,0x808080,0xc0c0c0,0xffffff}},
 /* 15: Atomic Purple shell. Deep translucent purple, lavender double border, white text. */
 {"GAME BOY COLOR DARK",10,1,XZ_CHROME_GBC_BOX,XZ_FONT_PIXEL,XZ_PATTERN_TILE_GRID,XZ_MAP_RAMP,{0x1e1030,0xf0e6ff,0x5aa0ff,0xff6060,{0xff5a4a,0x5aa0ff,0x58e070}},
  .face=0x35205a,.hi=0xd8c8f8,.lo=0xd8c8f8,.title=0x35205a,.title_ink=0xf0e6ff,.well=0x35205a,.well_ink=0xf0e6ff,.desk=0x1e1030},
 /* 16: OS X Graphite. Charcoal pinstripes, graphite gel, blue selection glow. */
 {"AQUA / ITUNES DARK",11,1,XZ_CHROME_AQUA,XZ_FONT_ATLAS,XZ_PATTERN_PINSTRIPE,XZ_MAP_RAMP,{0x2c2c2e,0xececec,0x4f8ef7,0xff6b5a,{0xff5f57,0x4f8ef7,0x5ad27a}},
  .face=0x6b6b70,.hi=0xa8a8ae,.hi2=0x3a3a3e,.lo=0x141416,.lo2=0x1d3a78,.title=0x48484c,.title_ink=0xf0f0f0,.well=0x1c261e,.well_ink=0xa8e0b0,.desk=0x333336,.sel_ink=0xffffff},
 /* 17: the DMG-01 shell. Dot-matrix grey, dark bezel band with the two stripes, magenta A/B pills, logo purple-blue type, green LCD footer. */
 {"GAME BOY DMG",12,0,XZ_CHROME_DMG_SHELL,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0xc2c0bc,0x2a2a52,0x9b2256,0xb8322a,{0x9b2256,0x484686,0x5f7a2e}},
  .face=0xcfcdc9,.hi=0xe6e4e0,.hi2=0x8c2f52,.lo=0x8c8a86,.lo2=0x484686,.title=0x33334a,.title_ink=0xd0d0e0,.well=0xb5c98b,.well_ink=0x1d2b20,.desk=0xc2c0bc,.sel_ink=0xf6e6ee},
 /* 18: the black Play It Loud! DMG. Same magenta, same green LCD. */
 {"GAME BOY DMG DARK",12,1,XZ_CHROME_DMG_SHELL,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,{0x1a1a1c,0xd8d8dc,0x9b2256,0xd63c3c,{0xc0306c,0x6a6ac8,0x8fb04a}},
  .face=0x2a2a2e,.hi=0x3e3e44,.hi2=0x8c2f52,.lo=0x000000,.lo2=0x484686,.title=0x0e0e10,.title_ink=0xb0b0b8,.well=0xb5c98b,.well_ink=0x1d2b20,.desk=0x1a1a1c,.sel_ink=0xf6e6ee},
 /* 19/20: Liquid Glass. Frosted panels over a mesh gradient, specular rim, capsules, system blue tint when selected. */
 {"LIQUID GLASS",13,0,XZ_CHROME_GLASS,XZ_FONT_ATLAS,XZ_PATTERN_MESH,XZ_MAP_RAMP,{0xeef1f8,0x111318,0x0a84ff,0xff453a,{0xff453a,0x0a84ff,0x30b158}},
  .face=0xffffff,.hi=0xffffff,.hi2=0xffffff,.lo=0x3a4050,.lo2=0x3a4050,.title=0xffffff,.title_ink=0x111318,.well=0xffffff,.well_ink=0x3a4050,.desk=0xeef1f8,.sel_ink=0xffffff,
  .tones={0xe3deec,0xd8e4f2,0xddeae8,0xe8e1ed}},
 {"LIQUID GLASS DARK",13,1,XZ_CHROME_GLASS,XZ_FONT_ATLAS,XZ_PATTERN_MESH,XZ_MAP_RAMP,{0x0f1118,0xf2f4f8,0x0a84ff,0xff6961,{0xff6961,0x409cff,0x30d158}},
  .face=0x30343f,.hi=0xffffff,.hi2=0xffffff,.lo=0x000000,.lo2=0x000000,.title=0x30343f,.title_ink=0xf2f4f8,.well=0x30343f,.well_ink=0xb8bcc8,.desk=0x0f1118,.sel_ink=0xffffff,
  .tones={0x242338,0x172b40,0x183332,0x302333}},
 {XZ_THEME_LCARS_NAME,14,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,XZ_THEME_LCARS_PALETTE,.face=0},
 {XZ_THEME_MATRIX_NAME,15,1,XZ_CHROME_FLAT,XZ_FONT_PIXEL,XZ_PATTERN_NONE,XZ_MAP_RAMP,XZ_THEME_MATRIX_PALETTE,.face=0},
 {XZ_THEME_ANALOG_NAME,16,1,XZ_CHROME_FLAT,XZ_FONT_ATLAS,XZ_PATTERN_NONE,XZ_MAP_RAMP,XZ_THEME_ANALOG_PALETTE,.face=0}
 };
 return &table[xz_theme_id(theme)];
}
static inline const struct xz_theme_palette *xz_theme_palette(int theme) { return &xz_theme(theme)->palette; }
static inline const char *xz_theme_name(int theme) { return xz_theme(theme)->name; }
static inline enum xz_theme_chrome xz_theme_chrome(int theme) { return xz_theme(theme)->chrome; }
static inline enum xz_theme_font xz_theme_font(int theme) { return xz_theme(theme)->font; }
static inline int xz_theme_pixel_text(int theme) { return xz_theme_font(theme)==XZ_FONT_PIXEL; }
static inline int xz_theme_styled(int theme) { return xz_theme(theme)->chrome!=XZ_CHROME_FLAT; }
/* Ids of a family's light and dark members; -1 when absent. Returns the family count. */
static inline int xz_theme_family_count(void) {
 int count=0;for(int i=0;i<XZ_THEME_COUNT;i++)if(xz_theme(i)->family>=count)count=xz_theme(i)->family+1;return count;
}
static inline void xz_theme_family_members(int family,int *light,int *dark) {
 *light=*dark=-1;
 for(int i=0;i<XZ_THEME_COUNT;i++)if(xz_theme(i)->family==family){if(xz_theme(i)->dark){if(*dark<0)*dark=i;}else if(*light<0)*light=i;}
}
static inline uint32_t xz_theme_selected_ink(int theme) { const struct xz_theme *t=xz_theme(theme);return t->sel_ink?t->sel_ink:t->palette.ink; }
static inline uint32_t xz_theme_title_ink(int theme) { const struct xz_theme *t=xz_theme(theme);return t->title_ink?t->title_ink:t->palette.ink; }
static inline uint32_t xz_theme_well_ink(int theme) { const struct xz_theme *t=xz_theme(theme);return t->well_ink?t->well_ink:t->palette.ink; }
/* Fill the page background. The offset places a partial surface (the inline
   strip) at its on-screen position so patterned backdrops line up. */
static inline void xz_theme_background_at(struct xz_theme_surface s,int theme,struct xz_theme_rect r,int ox,int oy) {
 const struct xz_theme *t=xz_theme(theme);const struct xz_theme_palette *p=&t->palette;
 if(t->pattern==XZ_PATTERN_MESH){xz_theme_mesh_fill(s,xz_theme_mesh_grid(xz_theme_id(theme),p->bg,t->tones),r,ox,oy);return;}
 if(t->chrome==XZ_CHROME_WIN95){
  xz_theme_fill(s,r,t->desk);
  struct xz_theme_rect window={r.x+3,r.y+3,r.w-6,r.h-6};
  xz_theme_fill(s,window,p->bg);xz_theme_bevel(s,window,0,t->hi,t->lo);xz_theme_bevel(s,window,1,t->hi2,t->lo2);
  return;
 }
 xz_theme_fill(s,r,p->bg);
 if(t->pattern==XZ_PATTERN_DITHER)for(int y=0;y<r.h;y+=8)for(int x=0;x<r.w;x+=8){
  xz_theme_fill(s,(struct xz_theme_rect){r.x+x,r.y+y,1,1},t->tones[1]);
  if(x+4<r.w&&y+4<r.h)xz_theme_fill(s,(struct xz_theme_rect){r.x+x+4,r.y+y+4,1,1},t->tones[1]);
 }
 if(t->pattern==XZ_PATTERN_PINSTRIPE)for(int y=(4-(oy&3))&3;y<r.h;y+=4)xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+y,r.w,1},t->desk);
 if(t->pattern==XZ_PATTERN_TILE_GRID){
  uint32_t grid=xz_theme_mix(p->bg,p->ink,18);
  for(int y=(8-(oy&7))&7;y<r.h;y+=8)for(int x=(8-(ox&7))&7;x<r.w;x+=8)xz_theme_fill(s,(struct xz_theme_rect){r.x+x,r.y+y,1,1},grid);
 }
}
static inline void xz_theme_background(struct xz_theme_surface s,int theme,struct xz_theme_rect r) { xz_theme_background_at(s,theme,r,0,0); }
static inline void xz_theme_frame_gb_lcd(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=&t->palette;uint32_t dark=p->ink,light=p->bg;
 xz_theme_fill(s,r,selected?t->tones[1]:p->bg);
 xz_theme_edge(s,r,0,dark);xz_theme_edge(s,r,1,dark);xz_theme_edge(s,r,2,light);xz_theme_edge(s,r,3,dark);
 if(r.w>16&&r.h>16){
  xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,3,3},p->bg);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-3,r.y,3,3},p->bg);
  xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+r.h-3,3,3},p->bg);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-3,r.y+r.h-3,3,3},p->bg);
  if(role!=XZ_THEME_BUTTON){
   xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+4,2,2},p->accent);
   xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-6,r.y+4,2,2},p->accent);
   xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+r.h-6,2,2},p->accent);
   xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-6,r.y+r.h-6,2,2},p->accent);
  }
 }
 if(selected)xz_theme_fill(s,(struct xz_theme_rect){r.x+5,r.y+6,2,r.h-12},dark);
}
static inline void xz_theme_frame_dmg(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=&t->palette;
 if(role==XZ_THEME_HEADER){
  xz_theme_fill(s,r,t->title);
  xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+r.h-1,r.w,1},t->lo);
  int x0=r.x+r.w*3/8,x1=r.x+r.w*13/16;
  if(x1>x0+40){
   xz_theme_fill(s,(struct xz_theme_rect){x0,r.y+r.h/2-5,x1-x0,3},t->hi2);
   xz_theme_fill(s,(struct xz_theme_rect){x0,r.y+r.h/2+2,x1-x0,3},t->lo2);
  }
  return;
 }
 if(role==XZ_THEME_FOOTER){
  xz_theme_fill(s,r,t->title);
  xz_theme_fill(s,(struct xz_theme_rect){r.x+6,r.y+3,r.w-12,r.h-6},t->well);
  xz_theme_edge(s,(struct xz_theme_rect){r.x+5,r.y+2,r.w-10,r.h-4},0,xz_theme_mix(t->well,0x000000,90));
  return;
 }
 int radius=role==XZ_THEME_BUTTON?xz_theme_radius(r,r.h/2>12?12:r.h/2):6;
 uint32_t fill=selected?p->accent:t->face;
 xz_theme_rounded(s,r,radius,xz_theme_mix(fill,0xffffff,selected?40:70),xz_theme_mix(fill,0x000000,selected?60:24),256);
 xz_theme_rounded_ring(s,r,radius,1,selected?xz_theme_mix(p->accent,0x000000,110):t->lo,256);
 if(!selected)xz_theme_fill(s,(struct xz_theme_rect){r.x+radius,r.y+1,r.w-2*radius,1},t->hi);
}
static inline void xz_theme_frame_snes(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=&t->palette;
 if(role==XZ_THEME_HEADER){
  xz_theme_fill(s,r,t->title);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+r.h-2,r.w,2},t->lo);
  const uint32_t abxy[4]={p->stem[0],p->alarm,p->stem[2],p->stem[1]};
  for(int i=0;i<4;i++)xz_theme_rounded(s,(struct xz_theme_rect){r.x+r.w*13/20+i*18,r.y+r.h/2-6,12,12},6,abxy[i],xz_theme_mix(abxy[i],0x000000,70),256);
  return;
 }
 if(role==XZ_THEME_FOOTER){xz_theme_fill(s,r,t->well);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,r.w,1},t->lo);return;}
 int radius=role==XZ_THEME_BUTTON?6:4;
 if(selected){
  xz_theme_rounded(s,r,radius,xz_theme_mix(p->accent,0xffffff,50),xz_theme_mix(p->accent,0x000000,50),256);
  xz_theme_rounded_ring(s,r,radius,1,xz_theme_mix(p->accent,0x000000,120),256);
  return;
 }
 xz_theme_rounded(s,r,radius,t->hi,t->face,256);
 xz_theme_rounded_ring(s,r,radius,1,t->lo,256);
 if(role==XZ_THEME_BUTTON&&r.h>16)xz_theme_fill(s,(struct xz_theme_rect){r.x+radius,r.y+r.h-4,r.w-2*radius,3},t->hi2);
}
static inline void xz_theme_frame_rpg(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 uint32_t top=selected?xz_theme_mix(t->face,0xffffff,40):t->face,bottom=selected?xz_theme_mix(t->well,0xffffff,30):t->well;
 xz_theme_rounded(s,r,4,top,bottom,256);
 xz_theme_rounded_ring(s,r,4,1,t->lo,256);
 xz_theme_rounded_ring(s,(struct xz_theme_rect){r.x+1,r.y+1,r.w-2,r.h-2},3,2,t->hi,256);
 xz_theme_rounded_ring(s,(struct xz_theme_rect){r.x+3,r.y+3,r.w-6,r.h-6},2,1,t->hi2,256);
 if(selected&&role!=XZ_THEME_HEADER&&role!=XZ_THEME_FOOTER&&r.h>=20)xz_theme_triangle(s,(struct xz_theme_rect){r.x+2,r.y+r.h/2-5,6,11},t->hi);
}
static inline void xz_theme_frame_win95(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 if(role==XZ_THEME_HEADER){
  struct xz_theme_rect bar={r.x+3,r.y+3,r.w-6,r.h-6};
  xz_theme_fill(s,r,t->face);xz_theme_fill(s,bar,t->title);
  struct xz_theme_rect close={bar.x+bar.w-18,bar.y+(bar.h-14)/2,16,14};
  xz_theme_fill(s,close,t->face);xz_theme_bevel(s,close,0,t->hi,t->lo);xz_theme_bevel(s,close,1,t->hi2,t->lo2);
  for(int i=0;i<6;i++){
   xz_theme_fill(s,(struct xz_theme_rect){close.x+4+i,close.y+3+i,2,1},t->lo);
   xz_theme_fill(s,(struct xz_theme_rect){close.x+9-i,close.y+3+i,2,1},t->lo);
  }
  return;
 }
 if(role==XZ_THEME_FOOTER){
  xz_theme_fill(s,r,t->face);
  struct xz_theme_rect well={r.x+3,r.y+2,r.w-6,r.h-5};
  xz_theme_bevel(s,well,0,t->lo2,t->hi);return;
 }
 if(role==XZ_THEME_LIST){xz_theme_fill(s,r,t->well);xz_theme_bevel(s,r,0,t->lo2,t->hi);xz_theme_bevel(s,r,1,t->lo,t->hi2);return;}
 xz_theme_fill(s,r,t->face);
 if(selected){
  if(t->dark){
   xz_theme_fill(s,r,t->title);
   xz_theme_bevel(s,r,0,t->lo2,t->hi);
   xz_theme_dots(s,r,4,t->hi);
   return;
  }
  for(int y=2;y<r.h-2;y++)for(int x=2+(y&1);x<r.w-2;x+=2)xz_theme_fill(s,(struct xz_theme_rect){r.x+x,r.y+y,1,1},t->hi2);
  xz_theme_bevel(s,r,0,t->lo,t->hi);xz_theme_bevel(s,r,1,t->lo2,t->hi2);
  xz_theme_dots(s,r,4,t->lo);
  return;
 }
 xz_theme_bevel(s,r,0,t->hi,t->lo);xz_theme_bevel(s,r,1,t->hi2,t->lo2);
}
static inline void xz_theme_frame_gbc(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=&t->palette;
 uint32_t fill=selected?xz_theme_mix(t->face,p->accent,t->dark?90:56):t->face;
 xz_theme_rounded(s,r,4,fill,fill,256);
 xz_theme_rounded_ring(s,r,4,1,t->lo,256);
 xz_theme_rounded_ring(s,(struct xz_theme_rect){r.x+2,r.y+2,r.w-4,r.h-4},3,1,t->lo,256);
 if(selected&&role!=XZ_THEME_HEADER&&role!=XZ_THEME_FOOTER)xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+5,3,r.h-10},p->accent);
}
static inline void xz_theme_frame_aqua(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=&t->palette;
 if(role==XZ_THEME_HEADER){xz_theme_brushed(s,r,t->title);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+r.h-1,r.w,1},t->lo);return;}
 if(role==XZ_THEME_FOOTER){
  xz_theme_fill(s,r,t->title);
  struct xz_theme_rect lcd={r.x+4,r.y+3,r.w-8,r.h-6};
  xz_theme_rounded(s,lcd,4,t->well,t->well,256);
  xz_theme_rounded_ring(s,lcd,4,1,xz_theme_mix(t->well,0x000000,t->dark?140:80),256);
  xz_theme_fill(s,(struct xz_theme_rect){lcd.x+4,lcd.y+1,lcd.w-8,1},xz_theme_mix(t->well,0x000000,40));
  return;
 }
 if(role==XZ_THEME_LIST){
  xz_theme_fill(s,r,t->dark?t->hi2:0xffffff);
  for(int y=0;y<r.h;y+=36)if((y/36)&1)xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+y,r.w,r.h-y<36?r.h-y:36},t->dark?xz_theme_mix(t->hi2,0xffffff,14):t->hi2);
  xz_theme_edge(s,r,0,t->lo);return;
 }
 if(role==XZ_THEME_PANEL&&r.h>52){
  uint32_t base=selected?p->accent:fill;
  xz_theme_rounded(s,r,10,xz_theme_mix(base,0xffffff,selected?12:t->dark?0:120),base,256);
  xz_theme_rounded_ring(s,r,10,1,t->lo,256);
  return;
 }
 int radius=xz_theme_radius(r,r.h/2);
 if(selected&&t->dark)xz_theme_rounded_ring(s,(struct xz_theme_rect){r.x-1,r.y-1,r.w+2,r.h+2},radius+1,2,p->accent,110);
 if(t->dark){
  uint32_t base=selected?xz_theme_mix(p->accent,0x000000,65):xz_theme_mix(t->face,0x000000,110);
  xz_theme_rounded(s,r,radius,xz_theme_mix(base,0xffffff,25),xz_theme_mix(base,0x000000,65),256);
  struct xz_theme_rect gloss={r.x+3,r.y+2,r.w-6,r.h/3};
  xz_theme_rounded(s,gloss,xz_theme_radius(gloss,radius-2),0xffffff,base,25);
  xz_theme_rounded_ring(s,r,radius,1,selected?p->accent:t->hi,150);
  return;
 }
 xz_theme_gel(s,r,radius,selected?p->accent:t->face,selected?t->lo2:t->lo);
}
static inline void xz_theme_frame_glass(struct xz_theme_surface s,const struct xz_theme *t,struct xz_theme_rect r,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette *p=&t->palette;
 if(role==XZ_THEME_HEADER)r=(struct xz_theme_rect){r.x+4,r.y+3,r.w-8,r.h-6};
 if(role==XZ_THEME_FOOTER)r=(struct xz_theme_rect){r.x+4,r.y+2,r.w-8,r.h-5};
 int radius=xz_theme_radius(r,12);
 if(selected)xz_theme_rounded(s,r,radius,xz_theme_mix(p->accent,0xffffff,50),p->accent,170);
 else xz_theme_rounded(s,r,radius,t->face,t->face,t->dark?150:140);
 xz_theme_rounded_ring(s,r,radius,1,t->hi,t->dark?70:110);
 xz_theme_fill(s,(struct xz_theme_rect){r.x+radius,r.y,r.w-2*radius,1},xz_theme_mix(selected?p->accent:t->face,t->hi,selected?150:200));
 xz_theme_blend(s,(struct xz_theme_rect){r.x+radius,r.y+r.h-1,r.w-2*radius,1},t->lo,40);
 xz_theme_blend(s,(struct xz_theme_rect){r.x,r.y+radius,1,r.h-2*radius},t->hi,t->dark?90:140);
}
/* Fill the entire frame before drawing caller-owned text, icons and meters.
   Panel/header content should keep an 8px inset; compact buttons need 6px. */
static inline void xz_theme_frame(struct xz_theme_surface s,int theme,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 const struct xz_theme *t=xz_theme(theme);const struct xz_theme_palette *p=&t->palette;
 if(r.w<=0||r.h<=0)return;
 if(theme==XZ_THEME_LCARS){xz_theme_lcars_frame(s,r,fill,selected,role);return;}
 if(theme==XZ_THEME_MATRIX){xz_theme_matrix_frame(s,r,fill,selected,role);return;}
 if(theme==XZ_THEME_ANALOG){xz_theme_analog_frame(s,r,fill,selected,role);return;}
 switch(t->chrome){
 case XZ_CHROME_GB_LCD:xz_theme_frame_gb_lcd(s,t,r,selected,role);return;
 case XZ_CHROME_DMG_SHELL:xz_theme_frame_dmg(s,t,r,selected,role);return;
 case XZ_CHROME_SNES_PAD:xz_theme_frame_snes(s,t,r,selected,role);return;
 case XZ_CHROME_RPG_WINDOW:xz_theme_frame_rpg(s,t,r,selected,role);return;
 case XZ_CHROME_WIN95:xz_theme_frame_win95(s,t,r,selected,role);return;
 case XZ_CHROME_GBC_BOX:xz_theme_frame_gbc(s,t,r,selected,role);return;
 case XZ_CHROME_AQUA:xz_theme_frame_aqua(s,t,r,fill,selected,role);return;
 case XZ_CHROME_GLASS:xz_theme_frame_glass(s,t,r,selected,role);return;
 case XZ_CHROME_FLAT:break;
 }
 if(role==XZ_THEME_FOOTER){xz_theme_fill(s,r,xz_theme_mix(p->bg,p->ink,20));xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,r.w,1},xz_theme_mix(p->bg,p->ink,90));return;}
 if(role==XZ_THEME_LIST){xz_theme_fill(s,r,fill);return;}
 xz_theme_fill(s,r,fill);
 xz_theme_edge(s,r,0,selected?p->accent:xz_theme_mix(p->bg,p->ink,112));
}
#endif
