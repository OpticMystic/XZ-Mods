#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
#define XZ_LED_PORTABLE_TEST
#include "../ui/native_led.c"
int main(void){
 const unsigned ids[]={XZ_LED_ID_CUE,XZ_LED_ID_PLAY_PAUSE,XZ_LED_ID_VINYL,XZ_LED_ID_BEAT_SYNC,XZ_LED_ID_LOOP_IN,XZ_LED_ID_LOOP_OUT,XZ_LED_ID_SLIP};
 const unsigned offsets[]={5,5,4,0x56,5,5,4},masks[]={1,2,2,0x30,4,8,4};
 const unsigned times[]={0,249,250,499,500,0xfffffff0u};
 unsigned checks=0,failed=0;
 for(unsigned held=0;held<2;held++)for(unsigned side=0;side<2;side++)for(unsigned state=0;state<12;state++)for(unsigned t=0;t<sizeof times/sizeof times[0];t++){
  reset(0);struct xz_extra_deck *d=&values.deck[side];
  d->playing=state&1;d->held_playing=held&&!d->playing;d->cue_ms=(state&2)?1000:30000;d->vinyl=state&4;
  d->sync.requested=1;d->sync.locked=state&1;
  d->slip=state&2;d->slip_active=!!(state&8);
  d->loop.active=!!(state&4);d->loop.in_ms=29000;d->loop.out_ms=40000;d->loop.edit=state/4;
  clock_ms=times[t];d->timeline.at_ms=(uint32_t)clock_ms;publish();for(unsigned n=0;n<12;n++)tick(side);
  for(unsigned i=0;i<sizeof ids/sizeof ids[0];i++){
   int lamp=xz_extra_lamp(d,ids[i]);unsigned expected=lamp==XZ_LED_STATE_BLINK?((uint32_t)clock_ms/250u)&1u:xz_led_written_state(lamp);
   unsigned actual=!!(frame(side)[offsets[i]]&masks[i]);checks++;
   if(expected!=actual){failed++;if(failed<14)printf("RED side=%u state=%u time=%u lamp=%u expected=%u actual=%u\n",side,state,(unsigned)clock_ms,ids[i],expected,actual);}
  }
 }
 printf("RESULT Jog final lamp parity checks=%u failures=%u\n",checks,failed);return !!failed;
}
