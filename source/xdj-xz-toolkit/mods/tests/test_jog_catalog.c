#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
extern void fixture_catalog_reset(void);
extern uint32_t fixture_catalog_update(unsigned side,unsigned event);
int main(int argc,char **argv){
 unsigned p0=argc>1?(unsigned)atoi(argv[1]):0,p1=argc>1?p0+1:2;
 unsigned s0=argc>2?(unsigned)atoi(argv[2]):0,s1=argc>2?s0+1:2;
 assert(p1<=2&&s1<=2);
 for(unsigned port=p0;port<p1;port++)for(unsigned side=s0;side<s1;side++){
  fixture_catalog_reset();reset(1025);
  for(unsigned d=0;d<2;d++)media[d]=fixture_catalog_update(d,0);
  for(unsigned d=0;d<2;d++){values.deck[d].source=(int)port+1;values.deck[d].timeline.media_generation=media[port];}
  publish();complete();
  for(unsigned cycle=0;cycle<12;cycle++){
   uint64_t accepted=values.deck[side].timeline.media_generation;
   media[port]=fixture_catalog_update(port,1);
   selection.deck[side]=side+1;selection.epoch[side]++;for(unsigned n=0;n<20;n++)tick(side);
   selection.deck[side]=side+3;selection.epoch[side]++;
   clock_ms+=50;values.deck[side].timeline.at_ms=(uint32_t)clock_ms;
   values.deck[side].timeline.source_frame=values.deck[side].elapsed_ms=30000+1000*cycle;
   publish();for(unsigned n=0;n<8;n++)tick(side);
   if(!jd_side[side].cached){printf("RED catalog update blanked INFO: port=%u side=%u generation=%llu->%llu unavailable=%u\n",port+1,side+1,(unsigned long long)accepted,(unsigned long long)media[port],jd_state[side].unavailable);fflush(stdout);}
   assert(jd_side[side].cached);
   complete();for(unsigned n=0;n<170;n++)tick(side);
   if(!jd_side[side].cached||!jd_side[side].have_wave){printf("RED port=%u side=%u catalog generation=%llu->%llu cached=%d wave=%d\n",port+1,side+1,(unsigned long long)accepted,(unsigned long long)media[port],jd_side[side].cached,jd_side[side].have_wave);fflush(stdout);}
   assert(jd_side[side].cached&&jd_side[side].have_wave&&media[port]==accepted);
   assert(jd_state[side].last_info.position_ms==30000+1000*cycle);
  }
  media[port]=fixture_catalog_update(port,2);publish();for(unsigned n=0;n<12;n++){tick(0);tick(1);}
  for(unsigned d=0;d<2;d++)assert(!jd_side[d].cached&&!jd_side[d].have_wave);
 }
 puts("PASS actual media-generation publisher to Jog: catalog writes and 48 side-switch cycles preserve advancing INFO/waves; real mount replacement still rejected");return 0;
}
