/* SPDX-License-Identifier: MIT */
/* The real native LED hook (led_hook, release, sync_master_apply, extra_deck)
 * with a modelled stock packet and stock player writer. Records hold 32-bit
 * addresses: on the host the binary is non-PIE, so static buffers sit below
 * 4 GiB and keep the unchanged callback pointer layout. */
#include <assert.h>
#include <stdio.h>
#ifndef __arm__
#define _Static_assert(c,m)
#endif
#define XZ_LED_CALLBACK_TEST
#include "../ui/native_led.c"
#ifndef __arm__
#undef _Static_assert
#endif
#include "sync_master_sim.h"

int xz_ui_runtime_pad_color(int d,int p,unsigned *rgb,int *lit){(void)d;(void)p;(void)rgb;(void)lit;return 0;}
void xz_ui_runtime_pad_native_page(int d,int p){(void)d;(void)p;}
int xz_ui_runtime_key_synced(int d){(void)d;return 0;}
int xz_read_memory(uint32_t a,void *p,size_t n){(void)a;(void)p;(void)n;return -1;}
int xz_hook_slot(uint32_t a,uint32_t b,void *c,void **d){(void)a;(void)b;(void)c;(void)d;return -1;}
void xz_log(const char *s){(void)s;}

#define SIM_ROWS 16
static unsigned char sim_records[SIM_ROWS*XZ_LED_RECORD_BYTES] __attribute__((aligned(8)));
static unsigned char sim_players[2][0x140] __attribute__((aligned(8)));
static unsigned char sim_innards[2][0x84] __attribute__((aligned(8)));
static unsigned char sim_packet[0x18] __attribute__((aligned(8)));
static int sim_rebuild,sim_layout_valid;
static enum sim_packet sim_layout_variant[2];
static const struct sim_led_stock *sim_stock;
static unsigned sim_count;

/* Stock rows: PLAY, SYNC, MASTER and MASTER TEMPO for both channels. A
 * variant changes the SYNC and MASTER rows of one channel only. */
static void layout(enum sim_packet v1,enum sim_packet v2){
 memset(sim_records,0,sizeof sim_records);sim_count=0;
 const enum sim_packet v[2]={v1,v2};
 for(unsigned ch=1;ch<=2;ch++){
  static const unsigned ids[]={XZ_LED_ID_PLAY_PAUSE,XZ_LED_ID_BEAT_SYNC,XZ_LED_ID_MASTER,XZ_LED_ID_MASTER_TEMPO};
  for(unsigned k=0;k<4;k++){
   unsigned id=ids[k],lamp=id==XZ_LED_ID_BEAT_SYNC||id==XZ_LED_ID_MASTER;
   unsigned copies=lamp&&v[ch-1]==SIM_PACKET_MISSING?0:lamp&&v[ch-1]==SIM_PACKET_DUPLICATE?2:1;
   for(unsigned c=0;c<copies;c++){
    assert(sim_count<SIM_ROWS);
    unsigned char *r=sim_records+(size_t)sim_count++*XZ_LED_RECORD_BYTES;
    put(r,id);put(r+4,ch);
    if(lamp&&v[ch-1]==SIM_PACKET_FORCED){r[0x18]=1;put(r+0x10,0);r[0x19]=0x5a;}
   }
  }
 }
 uint16_t count=(uint16_t)sim_count;memcpy(sim_packet+8,&count,2);
 put(sim_packet+0xc,(uint32_t)(uintptr_t)sim_records);
 sim_layout_variant[0]=v1;sim_layout_variant[1]=v2;sim_layout_valid=1;
}
static void stock_write(unsigned ch){
 if(!sim_stock)return;
 for(unsigned i=0;i<sim_count;i++){
  unsigned char *r=sim_records+(size_t)i*XZ_LED_RECORD_BYTES;
  if(word(r+4)!=ch||r[0x18])continue;
  if(word(r)==XZ_LED_ID_BEAT_SYNC)put(r+0x10,sim_stock->sync[ch-1]);
  if(word(r)==XZ_LED_ID_MASTER)put(r+0x10,sim_stock->master[ch-1]);
 }
}
static void stock_player(void *player,void *packet){(void)packet;stock_write(((unsigned char *)player)[0x26]);}

void sim_led_init(int rebuild_each_callback){
 assert((uintptr_t)sim_records<=UINT32_MAX&&(uintptr_t)sim_innards<=UINT32_MAX);
 memset(sync_master_owner,0,sizeof sync_master_owner);memset(&extra,0,sizeof extra);extra_side=0;
 for(unsigned s=0;s<2;s++){
  memset(sim_players[s],0,sizeof sim_players[s]);memset(sim_innards[s],0,sizeof sim_innards[s]);
  sim_players[s][0x26]=sim_innards[s][0x26]=(unsigned char)(s+1);
  put(sim_players[s]+0x138,(uint32_t)(uintptr_t)sim_innards[s]);
 }
 sim_rebuild=rebuild_each_callback;sim_layout_valid=0;sim_stock=NULL;
 original_led=stock_player;
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);
}
void sim_led_stop(void){xz_native_led_stop();}
void sim_led_start(void){__atomic_store_n(&enabled,1,__ATOMIC_RELEASE);}
void sim_led_callback(unsigned channel,enum sim_packet variant,int late,const struct sim_led_stock *stock){
 assert(channel==1||channel==2);
 sim_stock=stock;
 enum sim_packet v1=channel==1?variant:sim_layout_valid?sim_layout_variant[0]:SIM_PACKET_ONE;
 enum sim_packet v2=channel==2?variant:sim_layout_valid?sim_layout_variant[1]:SIM_PACKET_ONE;
 /* The stock serializer either rebuilds the list before each callback or
    keeps it and updates rows in place; a changed row set is always rebuilt. */
 if(sim_rebuild||!sim_layout_valid||v1!=sim_layout_variant[0]||v2!=sim_layout_variant[1])layout(v1,v2);
 led_hook(sim_players[channel-1],sim_packet);
 if(late)stock_write(channel);
}
static void row_state(unsigned channel,unsigned id,struct sim_led_row *out){
 *out=(struct sim_led_row){0};
 for(unsigned i=0;i<sim_count;i++){
  const unsigned char *r=sim_records+(size_t)i*XZ_LED_RECORD_BYTES;
  if(word(r)!=id||word(r+4)!=channel)continue;
  out->present++;out->state=word(r+0x10);
  if(r[0x19]==0x5a)out->forced_by_stock=1;
 }
}
void sim_led_rows(unsigned channel,struct sim_led_row *sync,struct sim_led_row *master){
 row_state(channel,XZ_LED_ID_BEAT_SYNC,sync);row_state(channel,XZ_LED_ID_MASTER,master);
}
