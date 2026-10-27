/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Classic column acceptance requires active assertions
#endif
#include "classic_column.h"
#include "deck_colour.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=807, H=480, SENTINEL=0x5aa5 };
static uint16_t pixels[W*H+2];
static uint16_t rgb(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void clear(void){for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=SENTINEL;}
static uint32_t fnv(void){uint32_t h=2166136261u;for(size_t i=0;i<W*H;i++){h^=pixels[i];h*=16777619u;}return h;}
static int count(int x0,int y0,int x1,int y1,uint32_t c){int n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)n+=pixels[y*W+x]==rgb(c);return n;}
/* Text ink of a hue: red high, blue low (orange, amber) on black. */
static int warm(int x0,int y0,int x1,int y1){int n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){unsigned p=pixels[y*W+x];n+=((p>>11)&31)>=20&&(p&31)<=8;}return n;}
static struct xz_classic_column_view fixture(int layout,int live34){
 struct xz_classic_column_view v;memset(&v,0,sizeof v);v.layout=layout;
 for(int d=0;d<4;d++){
  struct xz_classic_column_deck *k=&v.deck[d];
  k->live=d<2||live34;k->driven=d==0||(d==3&&live34);k->source=d<2?2:1;
  snprintf(k->key,sizeof k->key,"%s",d==0?"11A":d==1?"4A":"8A");
  if(d<2){snprintf(k->original,sizeof k->original,"%s",d?"4A":"2A");k->cents=d?-23:-21;k->cents_known=1;}
 }
 return v;
}
/* 2 DECKS following the sides: a software deck's half of the stock column. */
static void halves(void){
 for(int side=0;side<2;side++){
  int y0=xz_classic_column_half_y(side),y1=y0+xz_classic_column_half_h(side);
  assert(xz_classic_column_half_at(0,y0)==side&&xz_classic_column_half_at(129,y1-1)==side&&xz_classic_column_half_at(130,y0)==-1);
  struct xz_classic_column_deck d;memset(&d,0,sizeof d);d.live=d.driven=1;d.source=1+side;strcpy(d.key,side?"5A":"8A");
  clear();assert(xz_classic_column_half_draw(pixels,W,(size_t)W*H,2+side,&d));
  int inside=0;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   int in=x<130&&y>=y0&&y<y1;
   if(!in)assert(pixels[y*W+x]==SENTINEL);else inside+=pixels[y*W+x]!=SENTINEL;
  }
  assert(inside==130*(y1-y0)&&pixels[W*H]==SENTINEL&&pixels[W*H+1]==SENTINEL);
  /* The tab where the stock DECK 1 / DECK 2 tab is, its number in the badge (black on white), the key slot's line. */
  int ty=18+side*XZ_CLASSIC_HALF_STEP;
  assert(count(4,ty+5,28,ty+33,xz_deck_box_fill(2+side,1,0))>300&&count(4,ty+5,28,ty+33,xz_deck_box_ink(2+side,1,0))>20);
  assert(count(0,ty+1,100,ty+38,0x293c52)+count(0,ty+1,100,ty+38,0x203041)<3800);
  int slot=0;for(int y=119+side*143;y<154+side*143;y++)for(int x=4;x<111;x++)slot+=pixels[y*W+x]!=0;assert(slot>100);
  uint32_t drawn=fnv();
  clear();xz_classic_column_half_identity(pixels,W,(size_t)W*H,2+side);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(pixels[y*W+x]!=SENTINEL)assert(x>=4&&x<28&&y>=ty+5&&y<ty+33);
  assert(count(4,ty+5,28,ty+33,xz_deck_box_fill(2+side,1,0))>300&&count(4,ty+5,28,ty+33,xz_deck_box_ink(2+side,1,0))>20);
  clear();assert(xz_classic_column_half_draw(pixels,W,(size_t)W*H,2+side,&d)&&fnv()==drawn);
 }
 struct xz_classic_column_deck d;memset(&d,0,sizeof d);
 assert(!xz_classic_column_half_draw(pixels,W,(size_t)W*H,1,&d)&&!xz_classic_column_half_draw(pixels,W,(size_t)W*H,4,&d));
 assert(!xz_classic_column_half_draw(NULL,W,(size_t)W*H,2,&d)&&!xz_classic_column_half_draw(pixels,W,(size_t)W*H,2,NULL));
 assert(!xz_classic_column_half_draw(pixels,799,(size_t)W*H,2,&d)&&!xz_classic_column_half_draw(pixels,W,(size_t)W*H-1,2,&d));
 puts("PASS 2 DECKS column half: exact half writes, stock tab place with the deck's box, key slot drawn, identity box only, guards");
}
int main(void){
 halves();
 /* Rows beside the lanes: 22 + 68 k, 64 tall; the gaps and the outside are nothing. */
 for(int k=0;k<4;k++){
  assert(xz_classic_column_row_at(0,22+68*k)==k&&xz_classic_column_row_at(129,22+68*k+63)==k);
  if(k<3)assert(xz_classic_column_row_at(10,22+68*k+65)==-1);
 }
 assert(xz_classic_column_row_at(130,40)==-1&&xz_classic_column_row_at(10,21)==-1&&xz_classic_column_row_at(10,309)==-1);
 static const int mixer[4]={2,0,1,3};
 for(int k=0;k<4;k++){
  assert(xz_classic_column_deck_at(XZ_LAYOUT_FOUR,40,22+68*k+10)==k);
  assert(xz_classic_column_deck_at(XZ_LAYOUT_MIXER,40,22+68*k+10)==mixer[k]);
  assert(xz_classic_column_deck_at(XZ_LAYOUT_TWO,40,22+68*k+10)==-1);
 }
 /* Writes only x 0..129, y 22..308, and only in a four-deck view. */
 struct xz_classic_column_view v=fixture(XZ_LAYOUT_MIXER,1);
 clear();assert(xz_classic_column_draw(pixels,W,W*H,&v));
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(x>=130||y<22||y>=309)assert(pixels[y*W+x]==SENTINEL);
 assert(pixels[W*H]==SENTINEL&&pixels[W*H+1]==SENTINEL);
 uint32_t mixer_live=fnv();
 /* The driven decks (1 left, 4 right) wear the pale blue tab; in mixer order DECK 1's tag is second. */
 assert(count(3,22+68+2,100,22+68+20,0x94bef6)>800&&count(3,22+3*68+2,100,22+3*68+20,0x94bef6)>800);
 assert(count(3,22+2,100,22+20,0x94bef6)==0&&count(3,22+2,100,22+20,0x29344a)>800);
 /* Each tag's number in its badge, the same as its channel's: mixer order reads 3 1 2 4 down, 1 and 2 white on
    grey, 3 and 4 black on white; the light badge on the pale blue tab (DECK 4, driven) carries a dark edge. */
 for(int k=0;k<4;k++)assert(count(4,22+68*k+2,24,22+68*k+20,xz_deck_box_fill(mixer[k],1,0))>200&&count(4,22+68*k+2,24,22+68*k+20,xz_deck_box_ink(mixer[k],1,0))>10);
 assert(count(4,22+3*68+2,24,22+3*68+20,XZ_DECK_BADGE_EDGE)>=72&&pixels[(22+3*68+2)*W+4]==rgb(XZ_DECK_BADGE_EDGE));
 assert(pixels[(22+2)*W+4]==rgb(xz_deck_box_fill(2,1,0)));
 /* Cents in orange beside the native keys. */
 assert(warm(60,22+68+44,130,22+68+62)>10&&!warm(0,22+44,130,22+62));
 /* Number order puts DECK 1 first. */
 v.layout=XZ_LAYOUT_FOUR;clear();assert(xz_classic_column_draw(pixels,W,W*H,&v));
 assert(fnv()!=mixer_live&&count(3,22+2,100,22+20,0x94bef6)>800);
 /* Decks 3 and 4 not connected: dim tabs, amber NOT CONNECTED. */
 v=fixture(XZ_LAYOUT_MIXER,0);clear();assert(xz_classic_column_draw(pixels,W,W*H,&v));
 assert(count(3,22+2,100,22+20,0x161c26)>800&&count(3,22+3*68+2,100,22+3*68+20,0x161c26)>800);
 assert(warm(0,22+26,130,22+40)>20&&warm(0,22+3*68+26,130,22+3*68+40)>20);
 assert(count(3,22+68+2,100,22+68+20,0x94bef6)>800);
 /* Waiting decks keep their badge, dimmed. */
 assert(count(4,22+2,24,22+20,xz_deck_box_fill(2,0,0x161c26))>200&&count(4,22+2,24,22+20,xz_deck_box_fill(2,1,0))==0);
 assert(count(4,22+3*68+2,24,22+3*68+20,xz_deck_box_fill(3,0,0x161c26))>200);
 /* Guards. */
 v.layout=XZ_LAYOUT_TWO;clear();assert(!xz_classic_column_draw(pixels,W,W*H,&v));
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)assert(pixels[i]==SENTINEL);
 v.layout=XZ_LAYOUT_MIXER;assert(!xz_classic_column_draw(pixels,799,W*H,&v)&&!xz_classic_column_draw(pixels,W,W*H-1,&v)&&!xz_classic_column_draw(NULL,W,W*H,&v));
 v.layout=7;assert(!xz_classic_column_draw(pixels,W,W*H,&v));
 puts("PASS Classic side column: four tags beside the lanes in DECK VIEW order, driven sides lit, deck badges, cents, NOT CONNECTED decks 3/4, writes only x 0..129 y 22..308, nothing in 2 DECKS");
 return 0;
}
