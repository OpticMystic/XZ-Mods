/* Execute our complete callback on a 32-bit host. Native edges are modeled;
 * this does not execute stock firmware or qualify the physical LED sender. */
#ifdef NDEBUG
#error LED delivery acceptance requires assertions
#endif
#ifndef LED_SOURCE
#define LED_SOURCE "../ui/native_led.c"
#endif
#define XZ_LED_CALLBACK_TEST
#include LED_SOURCE
#include "../deck_select.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static struct xz_deck_select selection;
static struct xz_extra_decks supplied;
static int route_up=1,snapshot_ok=1,stock_sync[2],stock_master[2],late_write;
static unsigned stock_calls;
struct xz_extra_route xz_extra_decks_route(void){return (struct xz_extra_route){.route_up=route_up};}
int xz_extra_decks_snapshot(struct xz_extra_decks *out){
 *out=supplied;
 out->left_on_3=selection.side[0].target==XZ_DS_EXTRA;
 out->right_on_4=selection.side[1].target==XZ_DS_EXTRA;
 return snapshot_ok;
}
int xz_ui_runtime_pad_color(int d,int p,unsigned *rgb,int *lit){(void)d;(void)p;(void)rgb;(void)lit;return 0;}
void xz_ui_runtime_pad_native_page(int d,int p){(void)d;(void)p;}
int xz_ui_runtime_key_synced(int d){(void)d;return 0;}
int xz_read_memory(uint32_t a,void *p,size_t n){(void)a;(void)p;(void)n;return -1;}
int xz_hook_slot(uint32_t a,uint32_t b,void *c,void **d){(void)a;(void)b;(void)c;(void)d;return -1;}
void xz_log(const char *s){(void)s;}

static unsigned char records[6*XZ_LED_RECORD_BYTES],players[2][0x140],innards[2][0x84],packet[0x18];
static unsigned char *row(unsigned side,unsigned id){return records+((side-1)*3+id-4)*XZ_LED_RECORD_BYTES;}
static void stock_write(unsigned side){
 for(unsigned id=4;id<=5;id++){
  unsigned char *r=row(side,id);
  /* Same force-priority contract as native_led_apply/extra_deck. */
  if(!r[0x18])put(r+0x10,id==4?(unsigned)stock_sync[side-1]:(unsigned)stock_master[side-1]);
 }
}
static void stock_player(void *player,void *p){
 (void)p;unsigned side=((unsigned char *)player)[0x26];stock_calls++;
 if(!late_write)stock_write(side);
}
static void frame(void){
 for(unsigned side=1;side<=2;side++){
  led_hook(players[side-1],packet);
  if(late_write)stock_write(side);
 }
}
static void expect(unsigned side,unsigned sync,unsigned master){
 assert(word(row(side,4)+0x10)==sync);assert(word(row(side,5)+0x10)==master);
}
static void select_side(unsigned side,int extra){
 if((selection.side[side].target==XZ_DS_EXTRA)!=extra)xz_deck_select_toggle(&selection,(int)side,NULL);
}
static void set_master(int deck){
 stock_master[0]=deck==1;stock_master[1]=deck==2;
 supplied.deck[0].master=deck==3;supplied.deck[1].master=deck==4;
}
static void reset(void){
 memset(records,0,sizeof records);memset(players,0,sizeof players);memset(innards,0,sizeof innards);
 memset(packet,0,sizeof packet);memset(&supplied,0,sizeof supplied);
 xz_deck_select_init(&selection);xz_deck_select_route(&selection,1);route_up=snapshot_ok=1;
 uint16_t count=6;memcpy(packet+8,&count,2);put(packet+0xc,(uint32_t)(uintptr_t)records);
 for(unsigned side=1;side<=2;side++){
  players[side-1][0x26]=innards[side-1][0x26]=(unsigned char)side;
  put(players[side-1]+0x138,(uint32_t)(uintptr_t)innards[side-1]);
  for(unsigned id=4;id<=6;id++){unsigned char *r=row(side,id);put(r,id);put(r+4,side);}
  supplied.deck[side-1].loaded=1;supplied.deck[side-1].sync.requested=1;
  stock_sync[side-1]=0;stock_master[side-1]=0;
 }
 original_led=stock_player;enabled=1;
}
int main(void){
 for(int order=0;order<2;order++){
  late_write=1-order;
  reset();select_side(0,1);select_side(1,1);set_master(1);
  frame();expect(1,1,0);expect(2,1,0);
  /* Steady requested SYNC is independent of transport, phase acquisition and clock. */
  for(unsigned t=0;t<8;t++){put(packet+4,t*125);frame();expect(1,1,0);expect(2,1,0);}
  /* MASTER presses and automatic handoff publish the same confirmed deck facts. */
  for(int m=1;m<=4;m++){set_master(m);frame();expect(1,1,m==3);expect(2,1,m==4);}
  set_master(3);frame();expect(1,1,1);expect(2,1,0);
  set_master(4);frame();expect(1,1,0);expect(2,1,1);
  set_master(2);frame();expect(1,1,0);expect(2,1,0);
  for(int left=0;left<=1;left++)for(int right=0;right<=1;right++){
   select_side(0,left);select_side(1,right);
   for(int m=1;m<=4;m++){
    set_master(m);stock_sync[0]=0;stock_sync[1]=1;frame();
    expect(1,left?1:0,m==(left?3:1));expect(2,1,m==(right?4:2));
    assert(row(1,4)[0x18]==left&&row(2,4)[0x18]==right);
   }
  }
  supplied.deck[0].sync.requested=0;supplied.deck[1].loaded=0;set_master(4);
  frame();expect(1,0,0);expect(2,0,0);
  /* Route loss and stop restore native rows before ordinary stock writes. */
  route_up=0;frame();expect(1,0,0);expect(2,1,0);assert(!row(1,4)[0x18]&&!row(2,5)[0x18]);
  route_up=1;supplied.deck[1].loaded=1;frame();expect(2,1,1);
  xz_native_led_stop();frame();expect(1,0,0);expect(2,1,0);assert(!row(2,5)[0x18]);
  enabled=1;frame();expect(2,1,1);
  /* A failed snapshot cannot force a zero-filled or stale software model. */
  snapshot_ok=0;frame();expect(1,0,0);expect(2,1,0);assert(!row(2,5)[0x18]);
  snapshot_ok=1;
  /* A later foreign forced write survives release and reacquisition byte-for-byte. */
  frame();unsigned char *r=row(2,5);put(r+0x10,7);r[0x19]=0x55;
  unsigned char foreign[XZ_LED_RECORD_BYTES];memcpy(foreign,r,sizeof foreign);
  frame();assert(!memcmp(r,foreign,sizeof foreign));
  select_side(1,0);frame();assert(!memcmp(r,foreign,sizeof foreign));
  /* Existing foreign force, missing IDs, the opposite side and allocation canaries. */
  reset();select_side(0,1);row(1,4)[0x18]=1;put(row(1,4)+0x10,9);
  memcpy(foreign,row(1,4),sizeof foreign);frame();assert(!memcmp(row(1,4),foreign,sizeof foreign));
  reset();select_side(0,1);put(row(1,4),99);put(row(1,5),98);
  unsigned char saved[sizeof records];memcpy(saved,records,sizeof saved);
  /* Disable modeled global writes for the deliberately absent IDs. */
  led_hook(players[0],packet);
  assert(!memcmp(records,saved,2*XZ_LED_RECORD_BYTES));
  uint16_t n;memcpy(&n,packet+8,2);assert(n==6);
  /* Rebuilt/reordered packets must never restore the saved row into another LED. */
  reset();select_side(0,1);frame();
  put(row(1,4),99);row(1,4)[0x19]=0x42;memcpy(foreign,row(1,4),sizeof foreign);
  led_hook(players[0],packet);assert(!memcmp(row(1,4),foreign,sizeof foreign));
  /* A shorter new packet cannot expose the previous owned row. */
  reset();select_side(1,1);frame();memcpy(saved,records,sizeof saved);
  n=0;memcpy(packet+8,&n,2);late_write=1;led_hook(players[1],packet);
  assert(!memcmp(saved,records,sizeof saved));
  /* Missing/duplicate records remain unclaimed, with no count/index/allocation changes. */
  struct xz_led_sync_master_owner bounded={0};
  unsigned char tiny[XZ_LED_RECORD_BYTES+1]={0};put(tiny,4);put(tiny+4,1);tiny[44]=0xa5;
  assert(!xz_native_led_sync_master_apply(&bounded,tiny,43,1,1,&supplied.deck[0]));
  assert(!xz_native_led_sync_master_apply(&bounded,tiny,sizeof tiny,257,1,&supplied.deck[0]));
  assert(!xz_native_led_sync_master_apply(&bounded,tiny,sizeof tiny,1,3,&supplied.deck[0]));
  assert(!tiny[0x18]&&tiny[44]==0xa5);
  unsigned char duplicate[2*XZ_LED_RECORD_BYTES]={0};
  memcpy(duplicate,tiny,44);memcpy(duplicate+44,tiny,44);
  assert(!xz_native_led_sync_master_apply(&bounded,duplicate,sizeof duplicate,2,1,&supplied.deck[0]));
  assert(!duplicate[0x18]&&!duplicate[44+0x18]);
 }
 printf("PASS actual LED hook: before/after stock writes, steady SYNC, 4 masters x 4 side selections, handoff, route/stop/busy restore, foreign force, absent records; stock calls=%u\n",stock_calls);
 return 0;
}
