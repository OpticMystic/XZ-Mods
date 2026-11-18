/* SPDX-License-Identifier: MIT */
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "native.c"

static uint32_t bank_header[4][3]={{1,100,100},{1,100,100},{1,100,100},{1,100,100}};
static int fail_hook, bad_guard, calls;
static unsigned seen_bank, seen_extent, seen_total;
static int reload(unsigned bank){
 if(bank<4){
  struct xz_pc_wave_stamp stamp;
  assert(!xz_pc_wave_native_stamp(bank,&stamp));
  memset(bank_header[bank],0,sizeof(bank_header[bank]));
  /* The next track has identical geometry and arrives between worker polls. */
  bank_header[bank][0]=1;bank_header[bank][1]=100;bank_header[bank][2]=100;
 }
 return 73;
}
static int original_clear(unsigned bank){seen_bank=bank;return reload(bank);}
static int original_discard(unsigned bank,unsigned extent,unsigned total){
 seen_bank=bank;seen_extent=extent;seen_total=total;return reload(bank);
}
static int original_receive(const uint8_t *packet,int bytes){(void)packet;(void)bytes;return 29;}
int xz_read_memory(uint32_t address,void *out,size_t size){
 static const uint8_t guards[3][8]={{0xf0,0x4f,0x2d,0xe9,0x00,0x50,0xa0,0xe1},
  {0x03,0x00,0x50,0xe3,0xf0,0x41,0x2d,0xe9},{0x03,0x00,0x50,0xe3,0xf0,0x47,0x2d,0xe9}};
 const uint32_t addresses[3]={0xe52d0,0xe8194,0xe8604};
 for(unsigned i=0;i<3;i++)if(address==addresses[i]&&size==8){
  memcpy(out,guards[i],8);if(bad_guard)((uint8_t*)out)[0]^=1;return 0;
 }
 for(unsigned i=0;i<4;i++)if(address==0x11ed0c8u+i*0x107ad4u&&size==12){memcpy(out,bank_header[i],12);return 0;}
 return -1;
}
int xz_hook_arm(uint32_t address,const unsigned char expected[8],void *replacement,void **original){
 (void)expected;(void)replacement;calls++;
 if(fail_hook&&calls==fail_hook)return -1;
 if(address==0xe8194)*original=(void*)original_clear;
 else if(address==0xe8604)*original=(void*)original_discard;
 else if(address==0xe52d0)*original=(void*)original_receive;
 else return -1;
 return 0;
}
int main(void){
 struct xz_pc_wave_stamp before,after;
 bad_guard=1;assert(xz_pc_stems_native_observe()==-1&&calls==0);
 bad_guard=0;fail_hook=2;assert(xz_pc_stems_native_observe()==-1);
 assert(!xz_pc_wave_native_stamp(0,&before));
 fail_hook=0;assert(!xz_pc_stems_native_observe());
 int prior=calls;assert(!xz_pc_stems_native_observe()&&calls==prior);
 for(unsigned bank=0;bank<4;bank++){
  assert(xz_pc_wave_native_stamp(bank,&before));uint32_t stem_epoch=epoch[bank];
  assert(clear(bank)==73&&seen_bank==bank);
  assert(xz_pc_wave_native_stamp(bank,&after));
  assert(after.count==before.count&&after.epoch!=before.epoch&&after.serial==before.serial+2);
  assert(epoch[bank]!=stem_epoch&&!after.busy);
  before=after;stem_epoch=epoch[bank];
  assert(discard(bank,41,42)==73&&seen_bank==bank&&seen_extent==41&&seen_total==42);
  assert(xz_pc_wave_native_stamp(bank,&after));
  assert(after.count==before.count&&after.epoch!=before.epoch&&after.serial==before.serial+2);
  assert(epoch[bank]!=stem_epoch&&!after.busy);
 }
 uint32_t first=wave_epoch[0];assert(clear(4)==73&&wave_epoch[0]==first);
 assert(discard(4,2,3)==73&&seen_extent==2&&seen_total==3&&wave_epoch[0]==first);
 const uint8_t title[6]={0x10,0x22,1,0,1,0};
 assert(receive(title,6)==29&&wave_epoch[0]!=first);
 return 0;
}
