#ifndef XZ_UI_THEME_DRAW_H
#define XZ_UI_THEME_DRAW_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* RGB565 drawing primitives shared by the MODS panel, native asset restyling
   and the theme tests. Every routine clips to the surface and its clip rect.
   Stride is in pixels. Alpha and coverage are 0..256. */
struct xz_theme_surface { uint16_t *pixels; size_t stride; int width,height; int clip_x,clip_y,clip_w,clip_h; };
struct xz_theme_rect { int x,y,w,h; };

static inline uint16_t xz_theme_rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static inline uint32_t xz_theme_rgb888(uint16_t p) {
 return (uint32_t)(((p>>11)&31)*255/31)<<16|(uint32_t)(((p>>5)&63)*255/63)<<8|(uint32_t)((p&31)*255/31);
}
static inline uint32_t xz_theme_mix(uint32_t a,uint32_t b,unsigned amount) {
 unsigned n=amount>256?256:amount;
 return (((((a>>16)&255)*(256-n)+((b>>16)&255)*n)>>8)<<16)|
        (((((a>>8)&255)*(256-n)+((b>>8)&255)*n)>>8)<<8)|
        (((a&255)*(256-n)+(b&255)*n)>>8);
}
static inline unsigned xz_theme_luma(uint32_t c){return (77*((c>>16)&255)+150*((c>>8)&255)+29*(c&255))>>8;}
/* Visible span of r after clipping; returns 0 when nothing is visible. */
static inline int xz_theme_clip(struct xz_theme_surface s,struct xz_theme_rect r,int *x0,int *y0,int *x1,int *y1) {
 int64_t x=r.x,y=r.y,right=x+r.w,bottom=y+r.h,cx=s.clip_x,cy=s.clip_y,cr=cx+s.clip_w,cb=cy+s.clip_h;
 if(!s.pixels||s.width<=0||s.height<=0||s.stride<(size_t)s.width||r.w<=0||r.h<=0||s.clip_w<=0||s.clip_h<=0)return 0;
 if(x<0)x=0;if(y<0)y=0;if(x<cx)x=cx;if(y<cy)y=cy;
 if(right>s.width)right=s.width;if(bottom>s.height)bottom=s.height;if(right>cr)right=cr;if(bottom>cb)bottom=cb;
 if(right<=x||bottom<=y)return 0;
 *x0=(int)x;*y0=(int)y;*x1=(int)right;*y1=(int)bottom;return 1;
}
static inline void xz_theme_fill(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t color) {
 int x0,y0,x1,y1;if(!xz_theme_clip(s,r,&x0,&y0,&x1,&y1))return;
 uint16_t pixel=xz_theme_rgb565(color);
 for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++)s.pixels[(size_t)yy*s.stride+(size_t)xx]=pixel;
}
static inline void xz_theme_blend(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t color,unsigned alpha) {
 int x0,y0,x1,y1;if(alpha>=256){xz_theme_fill(s,r,color);return;}
 if(!alpha||!xz_theme_clip(s,r,&x0,&y0,&x1,&y1))return;
 for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
  uint16_t *p=&s.pixels[(size_t)yy*s.stride+(size_t)xx];*p=xz_theme_rgb565(xz_theme_mix(xz_theme_rgb888(*p),color,alpha));
 }
}
static inline void xz_theme_edge(struct xz_theme_surface s,struct xz_theme_rect r,int inset,uint32_t color) {
 if(inset<0||r.w<=inset*2||r.h<=inset*2)return;
 int x=r.x+inset,y=r.y+inset,w=r.w-inset*2,h=r.h-inset*2;
 xz_theme_fill(s,(struct xz_theme_rect){x,y,w,1},color);xz_theme_fill(s,(struct xz_theme_rect){x,y+h-1,w,1},color);
 xz_theme_fill(s,(struct xz_theme_rect){x,y,1,h},color);xz_theme_fill(s,(struct xz_theme_rect){x+w-1,y,1,h},color);
}
/* One bevel ring: top/left in tl, bottom/right in br; br owns both shared corners. */
static inline void xz_theme_bevel(struct xz_theme_surface s,struct xz_theme_rect r,int inset,uint32_t tl,uint32_t br) {
 if(inset<0||r.w<=inset*2||r.h<=inset*2)return;
 int x=r.x+inset,y=r.y+inset,w=r.w-inset*2,h=r.h-inset*2;
 xz_theme_fill(s,(struct xz_theme_rect){x,y+h-1,w,1},br);xz_theme_fill(s,(struct xz_theme_rect){x+w-1,y,1,h},br);
 xz_theme_fill(s,(struct xz_theme_rect){x,y,w-1,1},tl);xz_theme_fill(s,(struct xz_theme_rect){x,y,1,h-1},tl);
}
static inline void xz_theme_dots(struct xz_theme_surface s,struct xz_theme_rect r,int inset,uint32_t color) {
 if(inset<0||r.w<=inset*2||r.h<=inset*2)return;
 int x=r.x+inset,y=r.y+inset,w=r.w-inset*2,h=r.h-inset*2;
 for(int i=0;i<w;i+=2){xz_theme_fill(s,(struct xz_theme_rect){x+i,y,1,1},color);xz_theme_fill(s,(struct xz_theme_rect){x+i,y+h-1,1,1},color);}
 for(int i=0;i<h;i+=2){xz_theme_fill(s,(struct xz_theme_rect){x,y+i,1,1},color);xz_theme_fill(s,(struct xz_theme_rect){x+w-1,y+i,1,1},color);}
}
static inline int xz_theme_radius(struct xz_theme_rect r,int radius) {
 int limit=(r.w<r.h?r.w:r.h)/2;return radius<0?0:radius>limit?limit:radius;
}
/* Coverage of pixel (x,y) by the rounded rect, anti-aliased on the corner arcs only. */
static inline unsigned xz_theme_round_cov(struct xz_theme_rect r,int radius,int x,int y) {
 if(x<r.x||y<r.y||x>=r.x+r.w||y>=r.y+r.h)return 0;
 if(radius<=0)return 256;
 int left=x<r.x+radius,right=x>=r.x+r.w-radius,top=y<r.y+radius,bottom=y>=r.y+r.h-radius;
 if(!(left||right)||!(top||bottom))return 256;
 int cx=left?r.x+radius:r.x+r.w-radius,cy=top?r.y+radius:r.y+r.h-radius;
 int ddx=2*cx-(2*x+1),ddy=2*cy-(2*y+1);
 float cov=((float)(2*radius+1)-sqrtf((float)(ddx*ddx+ddy*ddy)))*128.f;
 return cov<=0?0:cov>=256?256:(unsigned)cov;
}
/* Rounded rectangle, vertical gradient top..bottom, blended over what is there. */
static inline void xz_theme_rounded(struct xz_theme_surface s,struct xz_theme_rect r,int radius,uint32_t top,uint32_t bottom,unsigned alpha) {
 int x0,y0,x1,y1;if(!alpha||!xz_theme_clip(s,r,&x0,&y0,&x1,&y1))return;
 radius=xz_theme_radius(r,radius);
 for(int yy=y0;yy<y1;yy++){
  uint32_t row=top==bottom?top:xz_theme_mix(top,bottom,(unsigned)((yy-r.y)*256/(r.h>1?r.h-1:1)));
  uint16_t solid=xz_theme_rgb565(row);
  for(int xx=x0;xx<x1;xx++){
   unsigned a=alpha>=256?xz_theme_round_cov(r,radius,xx,yy):(alpha*xz_theme_round_cov(r,radius,xx,yy))>>8;
   if(!a)continue;
   uint16_t *p=&s.pixels[(size_t)yy*s.stride+(size_t)xx];
   *p=a>=256?solid:xz_theme_rgb565(xz_theme_mix(xz_theme_rgb888(*p),row,a));
  }
 }
}
static inline void xz_theme_rounded_ring(struct xz_theme_surface s,struct xz_theme_rect r,int radius,int width,uint32_t color,unsigned alpha) {
 int x0,y0,x1,y1;if(!alpha||width<=0||!xz_theme_clip(s,r,&x0,&y0,&x1,&y1))return;
 radius=xz_theme_radius(r,radius);
 struct xz_theme_rect inner={r.x+width,r.y+width,r.w-2*width,r.h-2*width};int inner_radius=radius-width;
 for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
  unsigned outer=xz_theme_round_cov(r,radius,xx,yy),hole=inner.w>0&&inner.h>0?xz_theme_round_cov(inner,inner_radius,xx,yy):0;
  unsigned a=outer>hole?((outer-hole)*alpha)>>8:0;if(!a)continue;
  uint16_t *p=&s.pixels[(size_t)yy*s.stride+(size_t)xx];
  *p=xz_theme_rgb565(xz_theme_mix(xz_theme_rgb888(*p),color,a));
 }
}
/* Aqua-style gel capsule: glossy upper band, saturated lower half, dark rim. */
static inline void xz_theme_gel(struct xz_theme_surface s,struct xz_theme_rect r,int radius,uint32_t base,uint32_t rim) {
 radius=xz_theme_radius(r,radius);
 xz_theme_rounded(s,r,radius,xz_theme_mix(base,0xffffff,120),xz_theme_mix(base,0x000000,56),256);
 struct xz_theme_rect gloss={r.x+2,r.y+1,r.w-4,r.h*9/20};
 if(gloss.h>2)xz_theme_rounded(s,gloss,xz_theme_radius(gloss,radius-1),0xffffff,xz_theme_mix(base,0xffffff,200),110);
 struct xz_theme_rect glow={r.x+3,r.y+r.h*7/10,r.w-6,r.h*3/10-1};
 if(glow.h>1)xz_theme_rounded(s,glow,xz_theme_radius(glow,radius-2),base,xz_theme_mix(base,0xffffff,90),70);
 xz_theme_rounded_ring(s,r,radius,1,rim,256);
}
static inline uint32_t xz_theme_hash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;}
/* Brushed metal: soft vertical gradient plus horizontal streak noise. */
static inline void xz_theme_brushed(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t base) {
 int x0,y0,x1,y1;if(!xz_theme_clip(s,r,&x0,&y0,&x1,&y1))return;
 for(int yy=y0;yy<y1;yy++){
  uint32_t row=xz_theme_mix(xz_theme_mix(base,0xffffff,40),xz_theme_mix(base,0x000000,30),(unsigned)((yy-r.y)*256/(r.h>1?r.h-1:1)));
  uint32_t seed=(uint32_t)(yy-r.y)*977u+1u;
  for(int xx=x0;xx<x1;xx++){
   int cell=(xx-r.x)/6,t=(xx-r.x)%6;
   int a=(int)(xz_theme_hash(seed+(uint32_t)cell)&31)-16,b=(int)(xz_theme_hash(seed+(uint32_t)cell+1)&31)-16;
   int n=(a*(6-t)+b*t)/6;
   uint32_t c=n>=0?xz_theme_mix(row,0xffffff,(unsigned)n):xz_theme_mix(row,0x000000,(unsigned)(-n));
   s.pixels[(size_t)yy*s.stride+(size_t)xx]=xz_theme_rgb565(c);
  }
 }
}
/* Right-pointing selection cursor whose tip sits at (x+w-1, y+h/2). */
static inline void xz_theme_triangle(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t color) {
 for(int row=0;row<r.h;row++){
  int half=r.h/2,d=row<half?row:r.h-1-row,w=r.w*(d+1)/(half+1);
  if(w>0)xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y+row,w,1},color);
 }
}
/* Mesh gradient backdrop for the glass themes: four soft colour pools over a
   base, computed once per theme at half resolution and ordered-dithered on
   output so RGB565 does not band. Two cache slots cover the light/dark pair. */
#define XZ_THEME_MESH_W 400
#define XZ_THEME_MESH_H 240
struct xz_theme_mesh { int theme; uint8_t rgb[XZ_THEME_MESH_W*XZ_THEME_MESH_H*3]; };
static inline const struct xz_theme_mesh *xz_theme_mesh_grid(int theme,uint32_t base,const uint32_t pool[4]) {
 static struct xz_theme_mesh cache[2]={{-1,{0}},{-1,{0}}};
 static unsigned next;
 for(unsigned i=0;i<2;i++)if(cache[i].theme==theme)return &cache[i];
 struct xz_theme_mesh *m=&cache[next++&1];m->theme=theme;
 static const int centers[4][3]={{60,50,240},{340,40,220},{300,215,250},{90,220,210}};
 for(int y=0;y<XZ_THEME_MESH_H;y++)for(int x=0;x<XZ_THEME_MESH_W;x++){
  int acc[3]={(int)((base>>16)&255)<<8,(int)((base>>8)&255)<<8,(int)(base&255)<<8};
  for(int i=0;i<4;i++){
   int dx=x-centers[i][0],dy=y-centers[i][1],rr=centers[i][2];
   int d2=dx*dx+dy*dy,r2=rr*rr;if(d2>=r2)continue;
   int f=256-(int)((int64_t)d2*256/r2);int w=(f*f)>>8;
   acc[0]+=(((int)((pool[i]>>16)&255)-(int)((base>>16)&255))*w);
   acc[1]+=(((int)((pool[i]>>8)&255)-(int)((base>>8)&255))*w);
   acc[2]+=(((int)(pool[i]&255)-(int)(base&255))*w);
  }
  for(int c=0;c<3;c++){int v=acc[c]>>8;m->rgb[(y*XZ_THEME_MESH_W+x)*3+c]=(uint8_t)(v<0?0:v>255?255:v);}
 }
 return m;
}
static inline void xz_theme_mesh_fill(struct xz_theme_surface s,const struct xz_theme_mesh *m,struct xz_theme_rect r,int ox,int oy) {
 static const uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 int x0,y0,x1,y1;if(!m||!xz_theme_clip(s,r,&x0,&y0,&x1,&y1))return;
 for(int yy=y0;yy<y1;yy++){
  int gy=((yy-r.y+oy)>>1)%XZ_THEME_MESH_H;if(gy<0)gy+=XZ_THEME_MESH_H;
  for(int xx=x0;xx<x1;xx++){
   int gx=((xx-r.x+ox)>>1)%XZ_THEME_MESH_W;if(gx<0)gx+=XZ_THEME_MESH_W;
   const uint8_t *c=&m->rgb[(gy*XZ_THEME_MESH_W+gx)*3];unsigned d=bayer[yy&3][xx&3];
   unsigned rv=c[0]+d/2,gv=c[1]+d/4,bv=c[2]+d/2;
   if(rv>255)rv=255;if(gv>255)gv=255;if(bv>255)bv=255;
   s.pixels[(size_t)yy*s.stride+(size_t)xx]=(uint16_t)(((rv>>3)<<11)|((gv>>2)<<5)|(bv>>3));
  }
 }
}
#endif
