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
int main(void){
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
 /* Each tag's number box in its deck's colour, the same as its channel's: mixer order reads 3 1 2 4 down. */
 for(int k=0;k<4;k++)assert(count(4,22+68*k+2,24,22+68*k+20,xz_deck_colour(mixer[k]))>200);
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
 /* Waiting decks keep their colour, dimmed. */
 assert(count(4,22+2,24,22+20,xz_deck_colour_dim_fill(2,0x161c26))>200&&count(4,22+2,24,22+20,xz_deck_colour(2))==0);
 assert(count(4,22+3*68+2,24,22+3*68+20,xz_deck_colour_dim_fill(3,0x161c26))>200);
 /* Guards. */
 v.layout=XZ_LAYOUT_TWO;clear();assert(!xz_classic_column_draw(pixels,W,W*H,&v));
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)assert(pixels[i]==SENTINEL);
 v.layout=XZ_LAYOUT_MIXER;assert(!xz_classic_column_draw(pixels,799,W*H,&v)&&!xz_classic_column_draw(pixels,W,W*H-1,&v)&&!xz_classic_column_draw(NULL,W,W*H,&v));
 v.layout=7;assert(!xz_classic_column_draw(pixels,W,W*H,&v));
 puts("PASS Classic side column: four tags beside the lanes in DECK VIEW order, driven sides lit, deck colour boxes, cents, NOT CONNECTED decks 3/4, writes only x 0..129 y 22..308, nothing in 2 DECKS");
 return 0;
}
