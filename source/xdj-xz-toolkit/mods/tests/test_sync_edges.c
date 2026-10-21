#include "../deck_select.h"
#include <assert.h>
int main(void){for(int side=0;side<2;side++){
 struct xz_deck_select d;struct xz_ds_outcome o;struct xz_ds_key k={.side=side,.code=0x4313};xz_deck_select_init(&d);xz_deck_select_route(&d,1);xz_deck_select_toggle(&d,side,&o);
 assert(xz_deck_select_global_player(&d,&k,1).verdict==XZ_DS_SWALLOW);
 assert(xz_deck_select_global_player(&d,&k,2).verdict==XZ_DS_SWALLOW);
 k.op=1;assert(xz_deck_select_global_player(&d,&k,3).verdict==XZ_DS_SWALLOW);
 k.op=2;o=xz_deck_select_global_player(&d,&k,4);assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_SYNC&&o.event.pressed);
 assert(xz_deck_select_global_player(&d,&k,5).verdict==XZ_DS_SWALLOW);
 k.op=0;assert(xz_deck_select_global_player(&d,&k,6).verdict==XZ_DS_SWALLOW);
 k.op=3;assert(xz_deck_select_global_player(&d,&k,7).verdict==XZ_DS_SWALLOW);
 k.op=2;assert(xz_deck_select_global_player(&d,&k,8).verdict==XZ_DS_SWALLOW);
 }return 0;}
