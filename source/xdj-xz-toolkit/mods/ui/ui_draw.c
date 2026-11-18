#include "ui_skin.h"
#include "font_atlas.h"
#include "pixel_font.h"
#include "l10n.inc"

static uint16_t rgb(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
uint32_t xz_blend(uint32_t a,uint32_t b,unsigned n){
 unsigned r=(((a>>16)&255)*(256-n)+((b>>16)&255)*n)>>8;
 unsigned g=(((a>>8)&255)*(256-n)+((b>>8)&255)*n)>>8;
 unsigned v=((a&255)*(256-n)+(b&255)*n)>>8;return (r<<16)|(g<<8)|v;
}
/* Fills are the renderer's largest single cost. The compiler emits one 16-bit store per loop turn for the
 * plain loop, so rows are filled with aligned 32-bit stores, eight pixels per turn. Output is identical. */
typedef uint32_t __attribute__((may_alias)) xz_pixel_pair;
static inline void fill_row(uint16_t *row,int count,uint16_t pixel){
 if(count>0&&((uintptr_t)row&2u)){*row++=pixel;count--;}
 xz_pixel_pair pair=(uint32_t)pixel|((uint32_t)pixel<<16),*wide=(xz_pixel_pair *)row;
 for(;count>=8;count-=8,wide+=4){wide[0]=pair;wide[1]=pair;wide[2]=pair;wide[3]=pair;}
 for(;count>=2;count-=2)*wide++=pair;
 if(count>0)*(uint16_t *)wide=pixel;
}
void xz_rect(struct xz_canvas c,int x,int y,int w,int h,uint32_t col){
 int yy;if(!c.p)return;
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
 if(x+w>c.width)w=c.width-x;if(y+h>c.height)h=c.height-y;
 uint16_t pixel=rgb(col);
 for(yy=y;yy<y+h;yy++)fill_row(c.p+(size_t)yy*c.stride+(size_t)x,w,pixel);
}
void xz_border(struct xz_canvas c,int x,int y,int w,int h,uint32_t color){
 xz_rect(c,x,y,w,1,color);xz_rect(c,x,y+h-1,w,1,color);
 xz_rect(c,x,y,1,h,color);xz_rect(c,x+w-1,y,1,h,color);
}
/* The Cortex-A9 has no divide instruction, so the blend uses tables for the 5/6-bit expansions and an exact
 * shift form of division by 255 (exact for every value up to 65,535). Output is bit-identical. */
static const uint8_t expand5[32]={0,8,16,24,32,41,49,57,65,74,82,90,98,106,115,123,131,139,148,156,164,172,180,189,197,205,213,222,230,238,246,255};
static const uint8_t expand6[64]={0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80,85,89,93,97,101,105,109,113,117,121,125,
 129,133,137,141,145,149,153,157,161,165,170,174,178,182,186,190,194,198,202,206,210,214,218,222,226,230,234,238,242,246,250,255};
static inline unsigned div255(unsigned v){return (v+1u+(v>>8))>>8;}
static inline void blend_pixel(uint16_t *pixel,uint32_t color,unsigned n){
 unsigned old=*pixel,r=expand5[(old>>11)&31],g=expand6[(old>>5)&63],b=expand5[old&31];
 r=div255(r*(255-n)+((color>>16)&255)*n+127);
 g=div255(g*(255-n)+((color>>8)&255)*n+127);
 b=div255(b*(255-n)+(color&255)*n+127);
 *pixel=rgb((r<<16)|(g<<8)|b);
}
void xz_plot(struct xz_canvas c,int x,int y,uint32_t color,int a){
 if(!c.p||a<=0||x<0||y<0||x>=c.width||y>=c.height)return;
 uint16_t *pixel=&c.p[(size_t)y*c.stride+(size_t)x];
 if(a>=255){*pixel=rgb(color);return;}
 blend_pixel(pixel,color,(unsigned)a);
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
 if(!c.p)return;
 int left=x+g->x,top=y+g->y,first_col=left<0?-left:0,first_row=top<0?-top:0;
 int cols=(int)g->width,rows=(int)g->height;
 if(left+cols>c.width)cols=c.width-left;
 if(top+rows>c.height)rows=c.height-top;
 uint16_t solid=rgb(color);
 for(int row=first_row;row<rows;row++){
  const unsigned char *coverage=&xz_font_coverage[g->offset+(unsigned)row*g->width];
  uint16_t *line=&c.p[(size_t)(top+row)*c.stride+(size_t)left];
  for(int col=first_col;col<cols;col++){
   unsigned a=coverage[col];
   if(!a)continue;
   if(a>=255)line[col]=solid;else blend_pixel(&line[col],color,a);
  }
 }
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
 const struct xz_l10n_mask *localized=xz_l10n_mask_for(s,scale,limit);
 if(localized){xz_l10n_paint(c,x,y,localized,color);return;}
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
 const struct xz_l10n_mask *localized=xz_l10n_mask_for(s,scale,limit);
 if(localized)return (int)localized->width;
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

/* Longest prefix of text that fits width at scale, cut on a whole character. */
void xz_ui_chrome_fit(struct xz_canvas c,int x,int y,int width,const char *text,int scale,uint32_t color){
 char row[100];size_t n=0;
 if(!text)return;
 while(text[n]&&n<sizeof(row)-1){row[n]=text[n];n++;}row[n]=0;
 while(n&&xz_text_width(c,row,scale,99)>width){do n--;while(n&&((unsigned char)row[n]&0xc0)==0x80);row[n]=0;}
 xz_text(c,x,y,row,scale,99,color);
}
