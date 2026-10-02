#ifndef XZ_UI_THEME_MATRIX_H
#define XZ_UI_THEME_MATRIX_H
/* Included by themes.h after the fill/edge/mix primitives. */
#define XZ_THEME_MATRIX_PALETTE {0x000000,0x33cc33,0xccffcc,0xffb000,{0xff4444,0x44a0ff,0x66ff66}}
#define XZ_THEME_MATRIX_NAME "MATRIX"
#define XZ_MATRIX_TAIL1 0x1a801a
#define XZ_MATRIX_TAIL2 0x0a5c0a
#define XZ_MATRIX_TAIL3 0x062e06
static inline void xz_theme_matrix_bracket(struct xz_theme_surface s,struct xz_theme_rect r,int arm,int thick,uint32_t color) {
 int x1=r.x+r.w-arm,y1=r.y+r.h-arm;
 xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,arm,thick},color);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,thick,arm},color);
 xz_theme_fill(s,(struct xz_theme_rect){x1,r.y,arm,thick},color);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-thick,r.y,thick,arm},color);
 xz_theme_fill(s,(struct xz_theme_rect){r.x,y1,arm,thick},color);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+r.h-thick,thick,arm},color);
 xz_theme_fill(s,(struct xz_theme_rect){x1,y1,arm,thick},color);xz_theme_fill(s,(struct xz_theme_rect){r.x+r.w-thick,y1,thick,arm},color);
}
/* Stock 128x36 buttons arrive as the 122x30 rect at inset 3; the native label
   overwrites everything from inset 9 inward, so the frame lives in that band. */
static inline void xz_theme_matrix_frame(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette p=XZ_THEME_MATRIX_PALETTE;(void)role;
 int arm=r.w<r.h?r.w/3:r.h/3;if(arm>10)arm=10;if(arm<2)arm=2;
 xz_theme_fill(s,r,fill);
 if(selected){xz_theme_edge(s,r,0,p.accent);xz_theme_edge(s,r,1,p.accent);xz_theme_edge(s,r,3,p.ink);return;}
 xz_theme_matrix_bracket(s,r,arm,2,p.ink);
}
/* Stock-screen colour map: stem hues stay red, blue and green; everything else
   is a five-step phosphor ramp. Return 1 after writing *rgb. */
static inline int xz_theme_matrix_tone(unsigned r,unsigned g,unsigned b,unsigned light,uint32_t *rgb) {
 const struct xz_theme_palette p=XZ_THEME_MATRIX_PALETTE;
 unsigned maximum=r>g?r:g;if(b>maximum)maximum=b;
 unsigned minimum=r<g?r:g;if(b<minimum)minimum=b;
 if(maximum>=112&&maximum-minimum>=72){
  if(r==maximum&&r>=g*2&&r>=b*2){*rgb=p.stem[0];return 1;}
  if(b==maximum&&b>=r*2&&b*4>=g*5){*rgb=p.stem[1];return 1;}
  if(g==maximum&&g>=r*2&&g>=b*2){*rgb=p.stem[2];return 1;}
 }
 *rgb=light>=212?p.accent:light>=132?p.ink:light>=76?XZ_MATRIX_TAIL1:light>=40?XZ_MATRIX_TAIL2:light>=22?XZ_MATRIX_TAIL3:p.bg;
 return 1;
}
#endif
