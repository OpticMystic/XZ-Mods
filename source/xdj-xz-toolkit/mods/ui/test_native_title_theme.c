#ifdef NDEBUG
#error Title acceptance requires assertions
#endif
#include "native_title_theme.h"
#include "native_lcars_assets.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 const int themes[]={9,14,13,11,16,17,18};
 const uint32_t papers[]={0x000080,0x800080,0x2a48b0,0xbcbcbc,0x48484c,0x33334a,0x0e0e10};
 const uint32_t inks[]={0xffffff,0xffffff,0xffffff,0x202020,0xf0f0f0,0xd0d0e0,0xb0b0b8};
 for(unsigned i=0;i<sizeof(themes)/sizeof(*themes);i++)for(int deck=0;deck<2;deck++){
  uint32_t paper,ink;assert(xz_native_title_colors(themes[i],deck,&paper,&ink));
  assert(paper==papers[i]&&ink==inks[i]);
  assert(xz_native_title_pixel(0,paper,ink,0,0)==xz_theme_rgb565(paper));
  assert(xz_native_title_pixel(0xffff,paper,ink,0,0)==xz_theme_rgb565(ink));
  uint16_t mid=xz_native_title_pixel(0x8410,paper,ink,0,0);
  assert(mid!=xz_theme_rgb565(paper)&&mid!=xz_theme_rgb565(ink));
  assert(xz_native_title_pixel(0xf81f,paper,ink,1,0xf81f)==0xf81f);
  assert(xz_native_title_pixel(0,paper,ink,1,xz_theme_rgb565(paper))!=xz_theme_rgb565(paper));
  for(unsigned alpha=0;alpha<256;alpha++){
   uint32_t mapped=xz_native_title_rgba((alpha<<24)|0xffffff,ink,0,0);
   assert(mapped>>24==alpha);
   assert((mapped&0xffffff)==((ink>>16)|((ink&255)<<16)|(ink&0xff00)));
  }
 }
 uint32_t paper,ink;
 for(int deck=0;deck<2;deck++){
  assert(xz_native_title_colors(XZ_THEME_LCARS,deck,&paper,&ink)&&ink==0);
  assert(paper==(deck?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER));
  for(unsigned p=0;p<65536;p++)assert(xz_native_title_pixel((uint16_t)p,paper,0,1,0xf81f)==xz_lcars_title_pixel((uint16_t)p,paper,1,0xf81f));
 }
 assert(!xz_native_title_colors(19,0,&paper,&ink)&&!xz_native_title_colors(9,-1,&paper,&ink));
 assert(xz_native_title_deck(649,400,172)==0&&xz_native_title_deck(687,400,172)==1);
 assert(xz_native_title_deck(725,400,70)==0&&xz_native_title_deck(726,400,70)==1);
 assert(xz_native_title_deck(649,400,70)<0&&xz_native_title_deck(649,399,172)<0&&xz_native_title_deck(650,400,30)<0);
 assert(xz_native_title_rgba(0x80ff00ff,0xffffff,1,0xf81f)==0x80ff00ff);
 assert(xz_native_title_rgba(0x80123456,0xffffff,-1,0)==0x80123456);
 assert(xz_native_title_rgba(0x80ffffff,0,1,0)!=0x80000000);
 puts("PASS semantic title colors, per-deck LCARS parity, alpha, coverage, keys and exact core roles");
}
