#ifdef NDEBUG
#error Pixel font acceptance requires assertions
#endif
#include "pixel_font.h"
#include <assert.h>
#include <stdio.h>
static int same(unsigned a,unsigned b){
 for(unsigned r=0;r<7;r++)if(xz_pixel_font_row(a,r)!=xz_pixel_font_row(b,r))return 0;
 return 1;
}
int main(void){
 unsigned covered=0;
 for(unsigned ch=0;ch<512;ch++){
  for(unsigned r=0;r<12;r++){unsigned bits=xz_pixel_font_row(ch,r);assert(bits<32);if(r>=7)assert(bits==0);}
  int want=(ch>=32&&ch<=126)||(ch>=0xC0&&ch<=0xFF&&ch!=0xD7&&ch!=0xF7);
  assert(xz_pixel_font_has(ch)==want);covered+=(unsigned)want;
  if(!want)assert(same(ch,'?'));
 }
 assert(covered==95+62);
 for(unsigned ch='a';ch<='z';ch++)assert(!same(ch,ch-32));
 assert(same(0xC9,'E'));assert(same(0xFC,'u'));assert(same(0xDF,'s'));
 assert(same(0xC6,'A'));assert(same(0xFF,'y'));assert(same(0xDE,'P'));
 assert(!same(0xE9,'E'));assert(same(0xE9,'e'));
 for(unsigned r=0;r<7;r++){assert(!xz_pixel_font_row(' ',r));assert(xz_pixel_font_row(0,r)==xz_pixel_font_row('?',r));}
 assert(xz_pixel_font_row('A',3)==31);assert(xz_pixel_font_row('_',6)==31);
 for(const char *d="gjpqy";*d;d++)assert(xz_pixel_font_row((unsigned char)*d,6));
 for(const char *x="acemnorsuvwxz";*x;x++)assert(!xz_pixel_font_row((unsigned char)*x,0)&&!xz_pixel_font_row((unsigned char)*x,1));
 puts("PASS bounded pixel glyph rows, lowercase, Latin-1 folding and coverage");return 0;
}
