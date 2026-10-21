#ifdef NDEBUG
#error "Global jog tests require assertions"
#endif
#include "../deck_select.h"
#include <assert.h>
#include <string.h>
static struct xz_ds_key key(int side,unsigned code,unsigned op){struct xz_ds_key k={0};k.side=side;k.code=code;k.op=op;k.delta=4;k.value=.75f;return k;}
int main(void){
 for(int side=0;side<2;side++){
  struct xz_deck_select d;struct xz_ds_outcome o; xz_deck_select_init(&d);xz_deck_select_route(&d,1);
  struct xz_ds_key touch=key(side,0x4306,0),turn=key(side,0x4305,4);
  xz_deck_select_shift(&d,side,1,1,&o);
  assert(xz_deck_select_global_player(&d,&turn,2).verdict==XZ_DS_PASS);
  assert(d.side[side].phase==XZ_DS_MODIFIER);
  xz_deck_select_shift(&d,side,0,3,&o);
  /* Native global pass must leave the later physical callback its one ownership update. */
  o=xz_deck_select_global_player(&d,&touch,1);assert(o.verdict==XZ_DS_PASS);
  assert(!d.side[side].owner[XZ_DS_NATIVE]);
  assert(xz_deck_select_key(&d,&touch,1).verdict==XZ_DS_PASS);
  uint64_t native_owner=d.side[side].owner[XZ_DS_NATIVE];assert(native_owner);
  xz_deck_select_toggle(&d,side,&o);touch.op=2;
  assert(xz_deck_select_global_player(&d,&touch,2).verdict==XZ_DS_PASS);
  assert(d.side[side].owner[XZ_DS_NATIVE]==native_owner);
  assert(xz_deck_select_key(&d,&touch,2).verdict==XZ_DS_PASS);assert(!d.side[side].owner[XZ_DS_NATIVE]);
  assert(xz_deck_select_global_player(&d,&turn,3).verdict==XZ_DS_DELIVER);
  touch.op=0;assert(xz_deck_select_global_player(&d,&touch,4).verdict==XZ_DS_DELIVER);
  assert(xz_deck_select_global_player(&d,&touch,5).verdict==XZ_DS_SWALLOW);
  xz_deck_select_toggle(&d,side,&o);touch.op=2;
  assert(xz_deck_select_global_player(&d,&touch,6).verdict==XZ_DS_DELIVER);
  assert(xz_deck_select_global_player(&d,&touch,7).verdict==XZ_DS_PASS);
  xz_deck_select_toggle(&d,side,&o);touch.op=0;
  assert(xz_deck_select_global_player(&d,&touch,8).verdict==XZ_DS_DELIVER);
  xz_deck_select_route(&d,0);touch.op=3;
  assert(xz_deck_select_global_player(&d,&touch,9).verdict==XZ_DS_DELIVER);
  assert(xz_deck_select_global_player(&d,&touch,10).verdict==XZ_DS_PASS);
  assert(xz_deck_select_global_player(&d,&turn,11).verdict==XZ_DS_PASS);
 }
 return 0;
}
