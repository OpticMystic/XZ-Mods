#include "deck_select.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifdef XZ_DS_HAS_SELECTION_SNAPSHOT
static int installed=1,observer;
static uint32_t route_epoch=1;
static int route_up=1;
static struct xz_deck_select deck_select;
static pthread_mutex_t deck_mutex=PTHREAD_MUTEX_INITIALIZER;
uint32_t xz_extra_decks_epoch(void){return route_epoch;}
struct xz_extra_route xz_extra_decks_route(void){return (struct xz_extra_route){1,route_up};}
#include "runtime-selection.inc"
#endif
int main(void){
#ifndef XZ_DS_HAS_SELECTION_SNAPSHOT
 puts("RED: no authoritative selection epoch; away/back cannot be distinguished");return 1;
#else
 struct xz_ds_selection a,b,unchanged;
 xz_deck_select_init(&deck_select);
 assert(xz_runtime_deck_selection_snapshot(&a));assert(a.route_up&&a.route_epoch==1&&a.deck[0]==1&&a.deck[1]==2);
 for(int side=0;side<2;side++){
  assert(xz_runtime_deck_selection_snapshot(&a));
  xz_deck_select_toggle(&deck_select,side,NULL);xz_deck_select_toggle(&deck_select,side,NULL);
  assert(xz_runtime_deck_selection_snapshot(&b));assert(a.deck[side]==b.deck[side]&&b.epoch[side]==a.epoch[side]+2);
  assert(b.epoch[1-side]==a.epoch[1-side]);
 }
 assert(xz_runtime_deck_selection_snapshot(&a));route_up=0;
 assert(xz_runtime_deck_selection_snapshot(&b));assert(!b.route_up&&b.epoch[0]>a.epoch[0]&&b.epoch[1]>a.epoch[1]);
 a=b;assert(xz_runtime_deck_selection_snapshot(&b));assert(!memcmp(&a,&b,sizeof a));
 route_up=1;route_epoch=2;assert(xz_runtime_deck_selection_snapshot(&a));
 xz_deck_select_toggle(&deck_select,0,NULL);assert(xz_runtime_deck_selection_snapshot(&a));assert(a.deck[0]==3);
 route_epoch=3;assert(xz_runtime_deck_selection_snapshot(&b));assert(b.deck[0]==1&&b.epoch[0]>a.epoch[0]&&b.epoch[1]>a.epoch[1]);
 memset(&unchanged,0xa5,sizeof unchanged);b=unchanged;
 assert(!pthread_mutex_lock(&deck_mutex));assert(!xz_runtime_deck_selection_snapshot(&b));assert(!memcmp(&b,&unchanged,sizeof b));assert(!pthread_mutex_unlock(&deck_mutex));
 observer=1;assert(!xz_runtime_deck_selection_snapshot(&b));assert(!memcmp(&b,&unchanged,sizeof b));observer=0;
 puts("PASS selection away/back, per-side isolation, route invalidation/ABA, idempotence, trylock contention and output preservation");return 0;
#endif
}
