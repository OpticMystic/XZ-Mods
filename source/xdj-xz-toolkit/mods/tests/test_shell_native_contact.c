#ifdef NDEBUG
#error Native contact tests require assertions
#endif
#include "../shell_runtime.c"
#include <assert.h>
int main(void){
 owns_flag=1;
 xz_shell_runtime_native_contact(0,1000,1);
 assert(native_input_mask_at(2000)==1);
 uint32_t revision=native_input_revision;
 xz_shell_runtime_native_contact(0,1000,1);assert(native_input_revision==revision);
 xz_shell_runtime_native_contact(1,2100,1);assert(native_input_mask_at(2200)==3);
 xz_shell_runtime_native_contact(0,2300,0);assert(native_input_mask_at(2500)==2);
 xz_shell_runtime_native_contact(1,2600,0);assert(native_input_mask_at(2800)==0);
 struct xz_shell_deck deck={.playing=1};
 native_observed_position(0,&deck,720,3000,720);
 assert(deck.elapsed_ms==720&&!deck.elapsed_at_ms&&deck.playing);
 native_observed_position(0,&deck,-16000,3100,-16000);
 assert(deck.elapsed_ms==-16000&&!deck.elapsed_at_ms&&position[0].base_us==-16000000);
 int32_t ms;uint32_t at;struct xz_shrt_position p={0};
 xz_shrt_position_step(&p,0,-16000,1000,1000000,0,&ms,&at);assert(ms==-16000);
 puts("PASS held platter follows observed position until release, duplicate delivery, both decks and signed pre-start time");
 return 0;
}
