#include "ui_skin.h"
#include "font_atlas.h"
#include "pixel_font.h"

static uint16_t rgb(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
uint32_t xz_blend(uint32_t a,uint32_t b,unsigned n){
 unsigned r=(((a>>16)&255)*(256-n)+((b>>16)&255)*n)>>8;
 unsigned g=(((a>>8)&255)*(256-n)+((b>>8)&255)*n)>>8;
 unsigned v=((a&255)*(256-n)+(b&255)*n)>>8;return (r<<16)|(g<<8)|v;
}
void xz_rect(struct xz_canvas c,int x,int y,int w,int h,uint32_t col){
 int xx,yy;if(!c.p)return;
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
 if(x+w>c.width)w=c.width-x;if(y+h>c.height)h=c.height-y;
 uint16_t pixel=rgb(col);
 for(yy=y;yy<y+h;yy++)for(xx=x;xx<x+w;xx++)c.p[(size_t)yy*c.stride+(size_t)xx]=pixel;
}
void xz_border(struct xz_canvas c,int x,int y,int w,int h,uint32_t color){
 xz_rect(c,x,y,w,1,color);xz_rect(c,x,y+h-1,w,1,color);
 xz_rect(c,x,y,1,h,color);xz_rect(c,x+w-1,y,1,h,color);
}
void xz_plot(struct xz_canvas c,int x,int y,uint32_t color,int a){
 if(!c.p||a<=0||x<0||y<0||x>=c.width||y>=c.height)return;
 uint16_t *pixel=&c.p[(size_t)y*c.stride+(size_t)x];
 if(a>=255){*pixel=rgb(color);return;}
 unsigned old=*pixel,r=((old>>11)&31)*255/31,g=((old>>5)&63)*255/63,b=(old&31)*255/31,n=(unsigned)a;
 r=(r*(255-n)+((color>>16)&255)*n+127)/255;
 g=(g*(255-n)+((color>>8)&255)*n+127)/255;
 b=(b*(255-n)+(color&255)*n+127)/255;
 *pixel=rgb((r<<16)|(g<<8)|b);
}
static unsigned codepoint(const char **cursor){
 const unsigned char *s=(const unsigned char *)*cursor;unsigned ch=*s++;
 if(ch>=0xc2&&ch<=0xf4){
  unsigned need=ch<0xe0?1:ch<0xf0?2:3,value=ch&((1u<<(6-need))-1);
  for(unsigned i=0;i<need;i++){
   if((*s&0xc0)!=0x80){*cursor=(const char *)s;return '?';}
   value=(value<<6)|(*s++&63);
  }
  ch=value;
 }
 *cursor=(const char *)s;
 return ch>=32&&ch<=255?ch:'?';
}
static void letter(struct xz_canvas c,int x,int y,const struct xz_font_glyph *g,uint32_t color){
 for(unsigned row=0;row<g->height;row++)for(unsigned col=0;col<g->width;col++)
  xz_plot(c,x+g->x+(int)col,y+g->y+(int)row,color,xz_font_coverage[g->offset+row*g->width+col]);
}
void xz_pixel_text(struct xz_canvas c,int x,int y,const char *s,int dot,int limit,uint32_t color){
 if(!s||limit<=0||dot<=0)return;
 for(int count=0;*s&&count<limit;count++,x+=6*dot){
  unsigned ch=codepoint(&s);
  for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)
   if(xz_pixel_font_row(ch,row)&(1u<<(4-col)))xz_rect(c,x+(int)col*dot,y+(int)row*dot,dot,dot,color);
 }
}
void xz_text(struct xz_canvas c,int x,int y,const char *s,int scale,int limit,uint32_t color){
 if(!s||limit<=0)return;
 if(xz_theme_pixel_text(c.theme)){xz_pixel_text(c,x,y,s,scale>1?2:1,limit,color);return;}
 int face=scale>1?1:0,end=x+limit*6*scale;
 while(*s){
  unsigned ch=codepoint(&s);const struct xz_font_glyph *g=&xz_font_glyphs[face][ch-32];
  if(x+g->advance>end)break;
  letter(c,x,y,g,color);x+=g->advance;
 }
}
int xz_text_width(struct xz_canvas c,const char *s,int scale,int limit){
 int x=0;if(!s||limit<=0)return 0;
 if(xz_theme_pixel_text(c.theme)){
  for(int count=0;*s&&count<limit;count++,x+=6*(scale>1?2:1))codepoint(&s);
  return x;
 }
 int face=scale>1?1:0,end=limit*6*scale;
 while(*s){
  const struct xz_font_glyph *g=&xz_font_glyphs[face][codepoint(&s)-32];
  if(x+g->advance>end)break;
  x+=g->advance;
 }
 return x;
}
struct xz_theme_surface xz_surface(struct xz_canvas c){
 return (struct xz_theme_surface){c.p,c.stride,c.width,c.height,0,0,c.width,c.height};
}
void xz_frame(struct xz_canvas c,int theme,struct xz_ui_widget a,uint32_t fill,int selected){
 xz_theme_frame(xz_surface(c),theme,(struct xz_theme_rect){a.x,a.y,a.w,a.h},fill,selected,XZ_THEME_BUTTON);
}
struct xz_canvas xz_sub(struct xz_canvas c,int x,int y,int w,int h){
 struct xz_canvas s=c;
 if(!c.p||x<0||y<0||x>=c.width||y>=c.height||w<=0||h<=0){s.width=s.height=0;return s;}
 s.p=c.p+(size_t)y*c.stride+(size_t)x;
 s.width=w<c.width-x?w:c.width-x;s.height=h<c.height-y?h:c.height-y;
 return s;
}
/* 4x4 subsamples per pixel, in 1/8 px units around a centre (cx8,cy8). */
static int coverage(int px,int py,int cx8,int cy8,int outer8,int inner8){
 int n=0;
 for(int j=0;j<4;j++)for(int i=0;i<4;i++){
  int dx=px*8+2*i+1-cx8,dy=py*8+2*j+1-cy8,d=dx*dx+dy*dy;
  n+=d<=outer8*outer8&&(inner8<=0||d>=inner8*inner8);
 }
 return n*255/16;
}
void xz_round_rect(struct xz_canvas c,int x,int y,int w,int h,int radius,unsigned corners,uint32_t color){
 if(w<=0||h<=0)return;
 int r=radius,limit=(w<h?w:h)/2;if(r>limit)r=limit;
 if(r<=0||!(corners&15)){xz_rect(c,x,y,w,h,color);return;}
 int top=y<0?0:y,bottom=y+h>c.height?c.height:y+h;
 for(int yy=top;yy<bottom;yy++){
  int upper=yy<y+r,lower=yy>=y+h-r,cy8=(upper?y+r:y+h-r)*8,from=x,to=x+w;
  if((upper&&(corners&1))||(lower&&(corners&4))){
   for(int xx=x;xx<x+r;xx++)xz_plot(c,xx,yy,color,coverage(xx,yy,(x+r)*8,cy8,r*8,0));
   from=x+r;
  }
  if((upper&&(corners&2))||(lower&&(corners&8))){
   for(int xx=x+w-r;xx<x+w;xx++)xz_plot(c,xx,yy,color,coverage(xx,yy,(x+w-r)*8,cy8,r*8,0));
   to=x+w-r;
  }
  xz_rect(c,from,yy,to-from,1,color);
 }
}
static void annulus(struct xz_canvas c,int cx,int cy,int outer,int inner,uint32_t color){
 if(outer<=0)return;
 int cx8=cx*8+4,cy8=cy*8+4,o8=outer*8,i8=inner*8;
 int x0=cx-outer<0?0:cx-outer,x1=cx+outer>=c.width?c.width-1:cx+outer;
 int y0=cy-outer<0?0:cy-outer,y1=cy+outer>=c.height?c.height-1:cy+outer;
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
  int dx=x*8+4-cx8,dy=y*8+4-cy8,d=dx*dx+dy*dy;
  if(d<=(o8-6)*(o8-6)&&(i8<=0||d>=(i8+6)*(i8+6))){xz_plot(c,x,y,color,255);continue;}
  xz_plot(c,x,y,color,coverage(x,y,cx8,cy8,o8,i8));
 }
}
void xz_disc(struct xz_canvas c,int cx,int cy,int r,uint32_t color){annulus(c,cx,cy,r,0,color);}
void xz_ring(struct xz_canvas c,int cx,int cy,int r,int thickness,uint32_t color){
 if(thickness<=0)return;
 annulus(c,cx,cy,r,thickness>=r?0:r-thickness,color);
}
static uint32_t isqrt(uint64_t v){
 uint64_t r=0,bit=(uint64_t)1<<62;
 while(bit>v)bit>>=2;
 while(bit){if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}
 return (uint32_t)r;
}
void xz_line16(struct xz_canvas c,int x0,int y0,int x1,int y1,int width,uint32_t color){
 if(!c.p||width<=0||c.width<=0||c.height<=0)return;
 int64_t half=width/2,pad=half+16,dx=(int64_t)x1-x0,dy=(int64_t)y1-y0,length2=dx*dx+dy*dy,length=isqrt((uint64_t)length2);
 int64_t lo_x=(x0<x1?x0:x1)-pad,hi_x=(x0>x1?x0:x1)+pad,lo_y=(y0<y1?y0:y1)-pad,hi_y=(y0>y1?y0:y1)+pad;
 if(hi_x<0||hi_y<0)return;
 int px0=lo_x<0?0:(int)(lo_x/16),py0=lo_y<0?0:(int)(lo_y/16);
 int px1=hi_x/16>=c.width?c.width-1:(int)(hi_x/16),py1=hi_y/16>=c.height?c.height-1:(int)(hi_y/16);
 for(int py=py0;py<=py1;py++)for(int px=px0;px<=px1;px++){
  int64_t ax=(int64_t)px*16+8-x0,ay=(int64_t)py*16+8-y0,dot=ax*dx+ay*dy,d;
  if(!length2||dot<=0)d=isqrt((uint64_t)(ax*ax+ay*ay));
  else if(dot>=length2){int64_t bx=ax-dx,by=ay-dy;d=isqrt((uint64_t)(bx*bx+by*by));}
  else{int64_t cross=ax*dy-ay*dx;d=(cross<0?-cross:cross)/length;}
  int64_t a=(half+8-d)*255/16;
  xz_plot(c,px,py,color,a>255?255:(int)a);
 }
}
int xz_sin1024(int degrees){
 static const short table[91]={0,18,36,54,71,89,107,125,143,160,178,195,213,230,248,265,282,299,316,333,350,367,384,400,416,433,449,465,481,496,512,527,543,558,573,587,602,616,630,644,658,672,685,698,711,724,737,749,761,773,784,796,807,818,828,839,849,859,868,878,887,896,904,912,920,928,935,943,949,956,962,968,974,979,984,989,994,998,1002,1005,1008,1011,1014,1016,1018,1020,1022,1023,1023,1024,1024};
 int d=degrees%360;if(d<0)d+=360;
 if(d<=90)return table[d];
 if(d<=180)return table[180-d];
 if(d<=270)return -table[d-180];
 return -table[360-d];
}
int xz_cos1024(int degrees){return xz_sin1024(degrees%360+90);}
