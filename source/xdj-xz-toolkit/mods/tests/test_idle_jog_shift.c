#ifdef NDEBUG
#error Deck-switch regression requires assertions
#endif
#include "../deck_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void shift(struct xz_deck_select*d,int side,unsigned op,uint32_t t){xz_deck_select_global_key(d,side+1,XZ_DS_KEY_SHIFT,op,t);}
static void turn(struct xz_deck_select*d,int side,int delta,int physical,uint32_t t){
 struct xz_ds_key k={.side=side,.code=0x4305,.op=4,.delta=delta,.value=.125f};
 if(physical)(void)xz_deck_select_key(d,&k,t);else(void)xz_deck_select_global_player(d,&k,t);
}
int main(void){unsigned checks=0;
 for(int mask=0;mask<4;mask++)for(int side=0;side<2;side++)for(int path=0;path<2;path++)for(int timing=0;timing<2;timing++){
  struct xz_deck_select d;struct xz_ds_outcome out={0};xz_deck_select_init(&d);xz_deck_select_route(&d,1);
  for(int s=0;s<2;s++)if(mask>>s&1)xz_deck_select_toggle(&d,s,&out);
  struct xz_ds_side other=d.side[1-side];enum xz_ds_target before=d.side[side].target;
  uint32_t t=timing?UINT32_MAX-50:100;
  shift(&d,side,0,t);for(unsigned n=0;n<8;n++)turn(&d,side,0,path,t+1+n);
  shift(&d,side,2,t+10);for(unsigned n=0;n<8;n++)turn(&d,side,0,path,t+11+n);
  shift(&d,side,0,t+40);shift(&d,side,2,t+50);
  assert(d.side[side].target!=before);assert(!memcmp(&other,&d.side[1-side],sizeof other));checks++;
  /* Real movement remains a modifier and cancels a pending double press. */
  before=d.side[side].target;t+=1000;shift(&d,side,0,t);turn(&d,side,1,path,t+1);
  shift(&d,side,2,t+2);shift(&d,side,0,t+40);shift(&d,side,2,t+50);assert(d.side[side].target==before);checks++;
  /* A button used while SHIFT is held also remains a modifier. */
  struct xz_ds_key cue={.side=side,.code=0x4102,.op=0,.shift=1};
  t+=1000;shift(&d,side,0,t);(void)xz_deck_select_global_player(&d,&cue,t+1);
  shift(&d,side,2,t+2);cue.op=2;(void)xz_deck_select_global_player(&d,&cue,t+3);
  shift(&d,side,0,t+40);shift(&d,side,2,t+50);assert(d.side[side].target==before);checks++;
 }
 printf("PASS %u deck-switch checks: idle jog, real modifiers, both paths, clock wrap and independent sides\n",checks);
}
