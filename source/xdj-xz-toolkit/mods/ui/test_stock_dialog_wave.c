/* SPDX-License-Identifier: MIT */
#define main original_pair_main
#define xz_read_memory pair_fixture_read
#include "test_two_pair_runtime.c"
#undef main
#undef xz_read_memory
#define xz_read_memory dialog_fixture_read
#include "../stock_dialog_fixture.h"
#undef xz_read_memory
int xz_read_memory(uint32_t,void *,size_t);
#include "../stock_dialog.c"
int xz_read_memory(uint32_t at,void *out,size_t n){return dialog_fixture_read(at,out,n);}
int main(void){
 assert(!original_pair_main());
 for(unsigned i=0;i<536*268;i++)stock_frame[i]=(uint16_t)(0x1000+i*7919u%4096u);
 browse=0x1adb0d8;
 for(layout=XZ_LAYOUT_TWO;layout<=XZ_LAYOUT_MIXER;layout++){
  sides(1,1,1);frame(30000+layout*1000);
  assert(stock_rows(0,267)&&!xz_native_two_pair()&&xz_native_four_order()<0);
  sides(0,0,0);frame(34000+layout*1000);assert(stock_rows(0,267));
 }
 browse=0;status=0x1adb118;frame(38000);assert(stock_rows(0,267));status=0;
 ids[0]=102;frame(39000);assert(stock_rows(0,267));ids[0]=0;
 puts("PASS actual native wave: cable/route card, inline, side pair, four lanes, mixer/detail/notices yield stock pixels");
 return 0;
}
