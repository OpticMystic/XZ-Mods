/* Physical software SHIFT+SYNC enters the same non-audio control adapter. */
#include "extra_decks.c"
#include <assert.h>
static int requested,logical,forwarded;
int xz_key_control_request(int deck,enum xz_key_command_kind kind,int step){
 assert(kind==XZ_KEY_SYNC_TOGGLE&&!step);logical=deck;requested++;return 1;
}
int xz_xp_input(struct xz_xp_players *p,const struct xz_extra_deck_event *e){(void)p;assert(!e->shift);forwarded++;return 1;}
int main(void){
 __atomic_store_n(&players,(struct xz_xp_players *)(uintptr_t)1,__ATOMIC_RELEASE);
 for(int deck=0;deck<2;deck++){
  struct xz_extra_deck_event e={.deck=deck,.kind=XZ_EXTRA_IN_SYNC,.shift=1,.pressed=1};
  int before=requested;assert(xz_extra_deck_input(&e)&&requested==before+1&&logical==deck+2);
  e.held=1;assert(xz_extra_deck_input(&e)&&requested==before+1);
  e.pressed=0;assert(xz_extra_deck_input(&e)&&requested==before+1);
  e.shift=0;e.held=0;e.pressed=1;assert(xz_extra_deck_input(&e));
 }
 assert(requested==2&&forwarded==2);puts("PASS physical 3/4 typed key admission, held/release consumption and plain SYNC forwarding");
}
