#ifdef NDEBUG
#error Logo acceptance requires active assertions
#endif
/* The built-in XZ Mods logo: where it lands, what it writes, its colours on the RGB565 screen, and that a
 * DJ's own logo still replaces it pixel for pixel. */
#include "ui_about.h"
#include "logo_assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 800
#define H 480
#define SENTINEL 0x1234
/* The ABOUT mark's yellow XZ (#f3e900) and white MODS; the empty PLAY screen's mark is all in the shell's grey
 * text colour (#8c8c8c). */
enum { YELLOW=0xf740, WHITE=0xffff, GREY=0x8c71, BLACK=0x0000 };
static uint16_t buf[W*H], again[W*H], user[160*160];
static struct xz_canvas screen(uint16_t *p){return (struct xz_canvas){p,W,W,H,0};}
static void fill(uint16_t *p,uint16_t v){for(int i=0;i<W*H;i++)p[i]=v;}
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
/* The PLAY mark and its edges are neutral greys from black up to the grey, never brighter and never tinted. */
static int quiet_grey(uint16_t v){
 unsigned r=v>>11,g=(v>>5)&63,b=v&31;
 return r==b&&g==(r*63+15)/31&&r<=(GREY>>11);
}
static void save(const char *name,const uint16_t *p,int x0,int y0,int w,int h){
 FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",w,h);
 for(int y=y0;y<y0+h;y++)for(int x=x0;x<x0+w;x++){unsigned v=p[y*W+x];
  unsigned char rgb[3]={(unsigned char)((v>>11)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
  assert(fwrite(rgb,1,3,f)==3);}
 fclose(f);
}
int main(void){
 /* The built-in logo is made for its slot: the empty PLAY screen's 160 px square on black. */
 assert(xz_logo_idle.side==160&&xz_logo_idle.background==0x000000);
 assert(xz_logo_idle.top>0&&xz_logo_idle.top+xz_logo_idle.rows<160&&xz_logo_idle.rows*3>=160/2);
 assert(xz_logo_about.side==112&&xz_logo_about.background==0x0a0d15);

 /* It paints exactly its square and nothing else; the band is a straight copy, the rest is black. */
 fill(buf,SENTINEL);xz_ui_logo_draw(screen(buf),NULL,160,320,132,160);
 int grey=0,left=160,right=-1;
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  uint16_t v=buf[y*W+x];int in=x>=320&&x<480&&y>=132&&y<292;
  if(!in){assert(v==SENTINEL);continue;}
  assert(v!=SENTINEL&&quiet_grey(v));
  int row=y-132;
  if(row<xz_logo_idle.top||row>=xz_logo_idle.top+xz_logo_idle.rows)assert(v==BLACK);
  else assert(v==xz_logo_idle.band[(row-xz_logo_idle.top)*160+(x-320)]);
  grey+=v==GREY;
  if(v!=BLACK){if(x-320<left)left=x-320;if(x-320>right)right=x-320;}
 }
 /* The grey mark is a solid fill, not a noisy picture, and small: a wide black margin all round in its square. */
 assert(grey>2500);
 assert(left>=28&&right<=160-28&&xz_logo_idle.top>=40&&xz_logo_idle.top+xz_logo_idle.rows<=160-40);
 assert(buf[132*W+320]==BLACK&&buf[291*W+479]==BLACK&&buf[(132+80)*W+320]==BLACK);
 save("logo-idle-160.ppm",buf,320,132,160,160);

 /* Same pixels when the square is clipped by the screen edge, and no write outside the screen. */
 fill(again,SENTINEL);xz_ui_logo_draw(screen(again),NULL,160,-40,-30,160);
 for(int y=0;y<130;y++)for(int x=0;x<120;x++)assert(again[y*W+x]==buf[(y+132+30)*W+x+320+40]);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(x>=120||y>=130)assert(again[y*W+x]==SENTINEL);
 fill(again,SENTINEL);xz_ui_logo_draw(screen(again),NULL,160,720,400,160);
 for(int y=400;y<H;y++)for(int x=720;x<W;x++)assert(again[y*W+x]==buf[(y-400+132)*W+x-720+320]);

 /* Another size scales from the same art and still fills exactly its square. */
 fill(again,SENTINEL);xz_ui_logo_draw(screen(again),NULL,160,10,10,80);
 for(int y=0;y<100;y++)for(int x=0;x<100;x++)assert((again[y*W+x]==SENTINEL)==!(x>=10&&x<90&&y>=10&&y<90));

 /* A DJ's own logo still wins exactly as before: its pixels, 1:1, nothing of the built-in. */
 for(int i=0;i<160*160;i++)user[i]=(uint16_t)(0xf81f^i);
 fill(again,SENTINEL);xz_ui_logo_draw(screen(again),user,160,320,132,160);
 for(int y=0;y<160;y++)assert(!memcmp(again+(132+y)*W+320,user+y*160,160*sizeof *user));
 fill(again,SENTINEL);xz_ui_logo_draw(screen(again),user,160,100,100,80);
 for(int y=0;y<80;y++)for(int x=0;x<80;x++)assert(again[(100+y)*W+100+x]==user[(y*2)*160+x*2]);

 /* ABOUT: the logo is made on the panel's colour, so no box shows around it. */
 fill(buf,SENTINEL);xz_about_render(screen(buf),0,0,0,"Loader L-TEST-0000");
 uint16_t panel=rgb565(0x0a0d15);
 assert(buf[46*W+140]==panel);
 /* ABOUT keeps the mark's own colours: a solid yellow XZ and white MODS. */
 int yellow=0,white=0;
 for(int y=50;y<162;y++)for(int x=148;x<260;x++){
  uint16_t v=buf[y*W+x];int row=y-50;
  assert(v!=SENTINEL);
  if(row<xz_logo_about.top||row>=xz_logo_about.top+xz_logo_about.rows)assert(v==panel);
  else assert(v==xz_logo_about.band[(row-xz_logo_about.top)*112+(x-148)]);
  yellow+=v==YELLOW;white+=v==WHITE;
 }
 for(int x=148;x<260;x++)assert(buf[50*W+x]==panel&&buf[161*W+x]==panel);
 for(int y=50;y<162;y++)assert(buf[y*W+148]==panel&&buf[y*W+259]==panel);
 assert(yellow>2500&&white>400);
 save("logo-about.ppm",buf,136,42,656,362);
 printf("PASS built-in logo: exact square, small grey PLAY mark on black, clipping, scaling, DJ logo wins, "
        "ABOUT yellow and white on its panel (PLAY grey %d, ABOUT yellow %d white %d)\n",grey,yellow,white);
 return 0;
}
