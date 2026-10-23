#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
int main(int argc,char **argv){
 unsigned first=argc>1?(unsigned)atoi(argv[1]):0,last=argc>1?first+1:2;
 unsigned fm=argc>2?(unsigned)atoi(argv[2]):0,lm=argc>2?fm+1:2;
 assert(last<=2&&lm<=2);
 for(unsigned side=first;side<last;side++)for(unsigned missing=fm;missing<lm;missing++){
  reset(1025);complete();
  for(unsigned n=0;n<170;n++)tick(side);
  uint32_t accepted=content[side];uint64_t captured=values.deck[side].timeline.media_generation;
  media[side]=missing?0:captured+1;
  for(unsigned cycle=0;cycle<12;cycle++){
   selection.deck[side]=side+1;selection.epoch[side]++;for(unsigned n=0;n<20;n++)tick(side);
   selection.deck[side]=side+3;selection.epoch[side]++;
   clock_ms+=50;values.deck[side].timeline.at_ms=(uint32_t)clock_ms;
   values.deck[side].timeline.source_frame=30000+1000*cycle;
   values.deck[side].elapsed_ms=30000+1000*cycle;
   publish();complete();for(unsigned n=0;n<170;n++)tick(side);
   assert(content[side]==accepted&&values.deck[side].timeline.media_generation==captured);
   assert(jd_side[side].cached&&jd_side[side].have_wave);
   uint8_t out[64];packet(side,out);assert((frame(side)[0x53]>>4)==side+3);
   assert(jd_state[side].software_generation);
   assert(jd_state[side].last_info.position_ms==30000+1000*cycle);
  }
  content[side]++;publish();for(unsigned n=0;n<10;n++)tick(side);
  assert(!jd_side[side].cached&&!jd_side[side].have_wave);
 }
 puts("PASS loaded track remains visible through library-generation changes and repeated side selection; accepted LOAD fences old content");return 0;
}
