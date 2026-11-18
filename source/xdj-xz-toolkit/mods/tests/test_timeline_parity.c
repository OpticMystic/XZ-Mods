#ifdef NDEBUG
#error Cross-interface timeline tests require assertions
#endif
#include "../shell_runtime.h"
#include "../ui/extra_lane.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 const int32_t rates[]={-8000000,-1000000,0,500000,1000000,2000000,INT32_MAX};
 const uint32_t ages[]={0,1,40,50,200,251,1000,UINT32_MAX};
 unsigned cases=0;
 for(int deck=0;deck<2;deck++)for(int rate=0;rate<7;rate++)for(int age=0;age<8;age++)
 for(unsigned flags=0;flags<16;flags++)for(int playing=0;playing<2;playing++)for(int legacy=0;legacy<2;legacy++){
  struct xz_extra_decks input={.route_up=1};struct xz_shell_model model={0};
  struct xz_shrt_extra_parts parts[2]={{0}};
  struct xz_extra_deck *d=&input.deck[deck];d->loaded=1;d->total_ms=300000;
  d->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING)|
   XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_LOOP);
  if(legacy)d->known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED);
  d->elapsed_ms=1550;d->playing=playing;d->tempo_x100=1000;
  d->loop.active=1;d->loop.in_ms=1000;d->loop.out_ms=2000;
  d->timeline=(struct xz_extra_timeline){.source_frame=66150,.source_rate=44100,
   .at_ms=10000,.rate_ppm=rates[rate],.moving=playing,.flags=flags};
  uint32_t now=10000+ages[age];int32_t classic=-1;
  xz_shrt_extra_map(&model,&input,parts,now,now);model.clock_ms=now;
  int have=xz_extra_lane_position_ms(d,now,&classic);
  int32_t flagship=xz_shell_elapsed_now(&model,deck+2);
  assert(have==(flagship>=0));if(have)assert(classic==flagship);
  cases++;
 }
 printf("PASS %u Classic/FLAGSHIP timeline parity cases: signed rates, loops, stale/future, missing fields and motion flags\n",cases);
 return 0;
}
