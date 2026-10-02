#ifndef XZ_UI_THEME_LCARS_H
#define XZ_UI_THEME_LCARS_H
/* Included by themes.h after the fill/edge/mix primitives. */
#define XZ_THEME_LCARS_PALETTE {0x000000,0xff7800,0xffc000,0xff4545,{0xee4433,0x4488ff,0x44cc66}}
#define XZ_THEME_LCARS_NAME "LCARS"
#define XZ_LCARS_VIOLET 0xa64cff
#define XZ_LCARS_ICE 0x24c4ff
#define XZ_LCARS_NATIVE_AMBER 0xff9018
#define XZ_LCARS_NATIVE_VIOLET 0xa05cff
static inline uint64_t xz_lcars_isqrt(uint64_t n) {
 uint64_t x=0,bit=(uint64_t)1<<62;
 while(bit>n)bit>>=2;
 while(bit){if(n>=x+bit){n-=x+bit;x=(x>>1)+bit;}else x>>=1;bit>>=2;}
 return x;
}
/* Left/right inset of row y in an h-tall rounded rect of radius rad, sampled at pixel centres. */
static inline int64_t xz_lcars_inset(int64_t h,int64_t rad,int64_t y) {
 int64_t dy=2*y+1<2*rad?2*rad-2*y-1:2*y+1>2*(h-rad)?2*y+1-2*(h-rad):0;
 if(dy<=0)return 0;
 return (2*rad-(int64_t)xz_lcars_isqrt((uint64_t)(4*rad*rad-dy*dy)))/2;
}
static inline void xz_lcars_run(struct xz_theme_surface s,int64_t y,int64_t x0,int64_t x1,uint32_t color) {
 if(x0<0)x0=0;if(x1>s.width)x1=s.width;
 if(x0<x1)xz_theme_fill(s,(struct xz_theme_rect){(int)x0,(int)y,(int)(x1-x0),1},color);
}
static inline void xz_theme_lcars_frame(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 const struct xz_theme_palette p=XZ_THEME_LCARS_PALETTE;(void)role;
 uint32_t ring=selected?p.accent:p.ink;
 xz_theme_fill(s,r,fill);
 if(r.w<8||r.h<8){xz_theme_edge(s,r,0,ring);return;}
 int64_t w=r.w,h=r.h,rad=(w<h?w:h)/2,t=selected?5:2,ri=rad-t,wi=w-2*t,hi=h-2*t;
 /* Stock buttons overwrite from inset 6 inward: when that corner sits inside the capsule,
    shrink the inner radius so the ring stays out of it. */
 int64_t reach=(13-2*t)*1707/1000;
 if((2*rad-13)*(2*rad-13)<=2*rad*rad&&ri>reach)ri=reach;
 if(ri<0)ri=0;
 int64_t y0=r.y,y1=(int64_t)r.y+h;
 if(y0<s.clip_y)y0=s.clip_y;if(y0<0)y0=0;
 if(y1>(int64_t)s.clip_y+s.clip_h)y1=(int64_t)s.clip_y+s.clip_h;if(y1>s.height)y1=s.height;
 for(int64_t y=y0;y<y1;y++){
  int64_t ry=y-r.y,x=r.x,o=xz_lcars_inset(h,rad,ry);
  xz_lcars_run(s,y,x,x+o,p.bg);xz_lcars_run(s,y,x+w-o,x+w,p.bg);
  if(ry<t||ry>=h-t||wi<=0||hi<=0){xz_lcars_run(s,y,x+o,x+w-o,ring);continue;}
  int64_t i=t+xz_lcars_inset(hi,ri,ry-t);if(i<o)i=o;
  xz_lcars_run(s,y,x+o,x+i,ring);xz_lcars_run(s,y,x+w-i,x+w-o,ring);
 }
}
/* Stock-screen colour map: black ground, butterscotch ink, other hues into violet/ice,
   stem hues kept so the stock waveform still reads drums/harmonics/vocals. */
static inline int xz_theme_lcars_tone(unsigned r,unsigned g,unsigned b,unsigned light,uint32_t *rgb) {
 const struct xz_theme_palette p={0,XZ_LCARS_NATIVE_AMBER,0xffca55,0xff4545,{0xee4433,0x4488ff,0x44cc66}};
 unsigned hi=r>g?r:g,lo=r<g?r:g;if(b>hi)hi=b;if(b<lo)lo=b;
 if(hi-lo<=48){*rgb=light<20?p.bg:xz_theme_mix(p.bg,p.ink,light*256/255);return 1;}
 uint32_t bucket=XZ_LCARS_NATIVE_VIOLET;
 if(r>=g&&r>=b){
  if(g*2<r+32&&b*2<r+32)bucket=p.stem[0];
  else if(b<g)bucket=g*4>=r*3?p.accent:p.ink;
 }else if(g>r&&g>b)bucket=b*4>=g*3?XZ_LCARS_ICE:p.stem[2];
 else if(r*4>=b*3)bucket=XZ_LCARS_NATIVE_VIOLET;
 else bucket=g*4>=b*3?XZ_LCARS_ICE:p.stem[1];
 *rgb=xz_theme_mix(p.bg,bucket,hi*256/255);return 1;
}
#endif
