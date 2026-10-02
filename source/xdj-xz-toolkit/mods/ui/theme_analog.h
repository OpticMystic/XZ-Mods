#ifndef XZ_UI_THEME_ANALOG_H
#define XZ_UI_THEME_ANALOG_H
/* Included by themes.h after the fill/edge/mix primitives. */
#define XZ_THEME_ANALOG_PALETTE {0x1a1408,0xf2e6c9,0xffa33c,0xcc3322,{0xe8412c,0x3d8fe0,0x4cc45c}}
#define XZ_THEME_ANALOG_NAME "ANALOG"
/* Bevelled metal key. The stock 128x36 restyle draws this at inset 3 and then
   overwrites everything from inset 9 inward, so the key edge lives in the
   outer 6 px: bezel line, 2 px bevel, dark inner line (amber rim when
   selected), then the dark interior with one gloss row. */
static inline void xz_theme_analog_frame(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 uint32_t hi=selected?0x6e6048:0xf3e6c6,lo=selected?0xf3e6c6:0x7d6e50;(void)role;
 xz_theme_fill(s,r,selected?0xb2a27b:0xcdbd96);
 xz_theme_edge(s,r,0,0x3a3226);
 xz_theme_fill(s,(struct xz_theme_rect){r.x+1,r.y+1,r.w-2,2},hi);xz_theme_fill(s,(struct xz_theme_rect){r.x+1,r.y+1,2,r.h-2},hi);
 xz_theme_fill(s,(struct xz_theme_rect){r.x+1,r.y+r.h-3,r.w-2,2},lo);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-3,r.y+1,2,r.h-2},lo);
 xz_theme_edge(s,r,3,selected?0xffa33c:0x2a2418);
 xz_theme_fill(s,(struct xz_theme_rect){r.x+4,r.y+4,r.w-8,r.h-8},selected?xz_theme_mix(fill,0xffa33c,40):fill);
 xz_theme_fill(s,(struct xz_theme_rect){r.x+5,r.y+5,r.w-10,1},xz_theme_mix(fill,0xf2e6c9,selected?60:24));
}
/* Stock screen as dial glass: six flat warm steps from glass to cream ink,
   saturated hues snapped to amber, drums red, harmonics blue or vocals green. */
static inline int xz_theme_analog_tone(unsigned r,unsigned g,unsigned b,unsigned light,uint32_t *rgb) {
 unsigned mx=r>g?r:g,mn=r<g?r:g;if(b>mx)mx=b;if(b<mn)mn=b;
 if(mx>=96&&mx-mn>=72){
  uint32_t hue=r>=g&&r>=b?(g*2>=r&&g>b?0xffa33c:0xe8412c):g>=b?0x4cc45c:0x3d8fe0;
  *rgb=xz_theme_mix(0x1a1408,hue,light>=160?256:light>=96?200:light>=40?140:80);return 1;
 }
 *rgb=light<20?0x1a1408:light<56?0x2c2414:light<104?0x584a30:light<160?0x9c8c68:light<216?0xd2c4a2:0xf2e6c9;
 return 1;
}
#endif
