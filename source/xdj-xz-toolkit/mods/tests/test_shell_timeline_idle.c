#ifdef NDEBUG
#error Timeline wake tests require assertions
#endif
#include "../shell_runtime.c"
#include <assert.h>
static uint32_t player_revision,control_revision;
uint32_t xz_extra_decks_revision(void){return player_revision;}
uint32_t xz_extra_decks_control_revision(void){return control_revision;}
int main(void){
 owns_flag=1;dirty_flag=0;period_ms=100;
 shown_extra_revision=player_revision=19;
 shown_extra_control=control_revision=12;
 last_drawn_ms=now_ms();assert(xz_shell_runtime_idle());
 player_revision++;assert(!xz_shell_runtime_idle());
 shown_extra_revision=player_revision;last_drawn_ms=now_ms();assert(xz_shell_runtime_idle());
 control_revision^=1u;assert(!xz_shell_runtime_idle());
 shown_extra_control=control_revision;last_drawn_ms=now_ms();assert(xz_shell_runtime_idle());
 xz_shell_runtime_native_input(0,(uint32_t)(mono_checked_us()/1000u));
 assert(!xz_shell_runtime_idle()); /* native paused jog must wake before the 100 ms idle interval */
 shown_native_input=native_input_revision;assert(xz_shell_runtime_idle());
 native_input_at[0]=1000;native_input_at[1]=0;
 assert(native_input_mask_at(1000)==1&&native_input_mask_at(1120)==1);
 assert(native_input_mask_at(1121)==0&&!native_input_at[0]);
 native_input_at[0]=1001;assert(native_input_mask_at(1000)==0); /* future timestamp */
 native_input_at[0]=UINT32_MAX-10;assert(native_input_mask_at(5)==1); /* wrap */
 native_input_at[0]=0;
 xz_shell_runtime_native_input(0,1190);xz_shell_runtime_native_input(0,1000);
 assert(native_input_mask_at(1200)==1&&native_input_at[0]==1190);
 native_input_at[0]=UINT32_MAX-10;xz_shell_runtime_native_input(0,5);
 assert(native_input_mask_at(5)==1&&native_input_at[0]==5);
 native_input_at[0]=0;
 uint32_t revision=native_input_revision;
 xz_shell_runtime_native_input(-1,1000);xz_shell_runtime_native_input(2,1000);xz_shell_runtime_native_input(0,0);
 assert(native_input_revision==revision);
 struct xz_shell_deck deck={.known=K(LOADED)|K(PLAYING)|K(ELAPSED),.loaded=1,.playing=0,.elapsed_ms=1000};
 for(int playing=0;playing<2;playing++){
  deck.playing=playing;uint64_t known=deck.known;
  position[0].valid=1;position[0].following=1;position[0].base_us=10000000;
  native_observed_position(0,&deck,720);
  assert(deck.elapsed_ms==720&&!deck.elapsed_at_ms&&deck.playing==playing&&deck.known==known);
  assert(!position[0].valid&&!position[0].following);
 }
 dirty_flag=1;assert(!xz_shell_runtime_idle());
 dirty_flag=0;owns_flag=0;assert(!xz_shell_runtime_idle());
 struct xz_sync_native_snapshot copy;
 native_sync_clear();assert(!xz_extra_native_sync_clock(&copy)&&copy.reason==XZ_SYNC_NATIVE_MASTER_UNAVAILABLE);
 native_sync_cache=(struct xz_sync_native_snapshot){.available=1,.native_deck=1,.master_id=1,.source_revision=9,
  .captured_ms=(uint32_t)(mono_us()/1000u),.position_ms=12345};
 assert(xz_extra_native_sync_clock(&copy)&&copy.source_revision==9&&copy.position_ms==12345);
 native_sync_cache.source_revision=0;assert(!xz_extra_native_sync_clock(&copy)&&copy.reason==XZ_SYNC_NATIVE_SOURCE_CHANGED);
 native_sync_cache.source_revision=9;native_sync_cache.captured_ms=(uint32_t)(mono_us()/1000u)-1000;
 assert(!xz_extra_native_sync_clock(&copy)&&copy.reason==XZ_SYNC_NATIVE_TIME_INVALID);
 native_sync_cache.captured_ms=(uint32_t)(mono_us()/1000u)+1000;
 assert(!xz_extra_native_sync_clock(&copy)&&copy.reason==XZ_SYNC_NATIVE_TIME_INVALID);
 puts("PASS native SYNC cache getter preserves unavailability reasons and rejects unknown load generation, stale and future snapshots without native-memory reads");
 puts("PASS actual runtime idle gate wakes for PCM revision and side-only switches without waiting for the 100 ms idle interval");
 return 0;
}
