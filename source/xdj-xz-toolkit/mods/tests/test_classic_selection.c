#ifdef NDEBUG
#error Classic selection checks require assertions
#endif
#include "deck_select.h"
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int installed=1,observer,hardware_switch=-1,published[2];
static pthread_mutex_t deck_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_deck_select deck_select;
static struct xz_extra_route route={1,1};
static uint32_t route_epoch=1;
uint32_t xz_extra_decks_epoch(void){return route_epoch;}
struct xz_extra_route xz_extra_decks_route(void){return route;}
int xz_extra_decks_snapshot(struct xz_extra_decks *out){
 memset(out,0,sizeof *out);out->route_up=route.route_up;
 /* A physical switch occurs after feedback is captured but before the selection acquires its lock. */
 if(hardware_switch>=0){
  struct xz_ds_outcome o={0};
  pthread_mutex_lock(&deck_mutex);xz_deck_select_toggle(&deck_select,hardware_switch,&o);pthread_mutex_unlock(&deck_mutex);
  assert(o.toggled);hardware_switch=-1;
 }
 return route.route_up;
}
void xz_extra_decks_publish_sides(int left,int right){published[0]=left;published[1]=right;}
void xz_log(const char *message){assert(message&&*message);}
#include "runtime-selection.inc"

int main(void){
 xz_deck_select_init(&deck_select);xz_deck_select_route_instance(&deck_select,1,route_epoch);
 assert(xz_runtime_deck_select_target(0,1)==1&&published[0]==1&&published[1]==0);
 assert(xz_runtime_deck_select_target(0,1)==1&&published[0]==1);
 assert(xz_runtime_deck_select_target(0,0)==1&&published[0]==0);
 hardware_switch=0;
 assert(xz_runtime_deck_select_target(0,1)==1&&published[0]==1);
 hardware_switch=0;
 assert(xz_runtime_deck_select_target(0,0)==1&&published[0]==0);
 assert(xz_runtime_deck_select_target(1,1)==1&&published[0]==0&&published[1]==1);
 assert(xz_runtime_deck_select_toggle(1)==1&&published[1]==0);
 assert(xz_runtime_deck_select_toggle(1)==1&&published[1]==1);
 /* A replacement route invalidates the old selection epoch even if it stays up. */
 struct xz_ds_selection old_selection,new_selection;
 xz_deck_select_snapshot(&deck_select,&old_selection);route_epoch++;
 assert(xz_runtime_deck_select_target(1,1)==1&&published[1]==1);
 xz_deck_select_snapshot(&deck_select,&new_selection);
 assert(new_selection.route_epoch==route_epoch&&new_selection.epoch[1]!=old_selection.epoch[1]);
 route.route_up=0;
 assert(xz_runtime_deck_select_target(1,1)==0&&published[1]==0);
 assert(xz_runtime_deck_select_target(1,0)==1&&published[1]==0);
 assert(xz_runtime_deck_select_target(2,1)==-1&&xz_runtime_deck_select_target(0,2)==-1);
 route.available=0;assert(xz_runtime_deck_select_target(0,0)==-1);
 route.available=route.route_up=1;observer=1;assert(xz_runtime_deck_select_target(0,1)==-1);
 observer=0;installed=0;assert(xz_runtime_deck_select_target(0,1)==-1);
 puts("PASS actual runtime selection: idempotent targets, physical switch between snapshot and lock, independent sides, legacy toggle and route loss");
 return 0;
}
