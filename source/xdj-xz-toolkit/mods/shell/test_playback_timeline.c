#ifdef NDEBUG
#error Playback timeline tests require assertions
#endif
#include "fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static uint16_t actual[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],reference[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
#define K(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
int main(void) {
 struct xz_shell_model m;struct xz_shell u;
 fx_track_build(&track,7);fx_model_full(&m,&track);xz_shell_init(&u);m.deck_count=4;
 for(int i=0;i<4;i++){
  struct xz_shell_deck *d=&m.deck[i];*d=m.deck[0];
  d->known|=K(PLAYBACK_RATE)|K(MOTION)|K(ELAPSED)|K(LOADED)|K(PLAYING)|K(SYNC);
  d->loaded=1;d->playing=0;d->sync=1;d->moving=1;d->tempo_x100=0;
  d->elapsed_ms=30000+i*10000;d->elapsed_at_ms=1000;d->loop.active=0;
 }
 const int32_t rate[]={1900000,-2000000,0,500000};
 const int32_t expected[]={30076,39920,50000,60020};
 m.clock_ms=1040;
 for(int i=0;i<4;i++){m.deck[i].playback_rate_ppm=rate[i];assert(xz_shell_elapsed_now(&m,i)==expected[i]);}
 /* A SYNC lamp cannot align unrelated anchors. The actual frame must equal
    a reference whose positions are explicitly set to the PCM-timeline result. */
 assert(xz_shell_render(&u,&m,actual,XZ_SHELL_WIDTH));
 struct xz_shell_model exact=m;
 for(int i=0;i<4;i++){exact.deck[i].elapsed_ms=expected[i];exact.deck[i].moving=0;}
 assert(xz_shell_render(&u,&exact,reference,XZ_SHELL_WIDTH));
 assert(!memcmp(actual,reference,sizeof actual));
 struct xz_shell_deck *d=&m.deck[0];
 d->known&=~K(PLAYBACK_RATE);assert(xz_shell_elapsed_now(&m,0)==30000);
 d->known|=K(PLAYBACK_RATE);d->moving=0;assert(xz_shell_elapsed_now(&m,0)==30000);
 d->moving=1;d->elapsed_at_ms=1041;assert(xz_shell_elapsed_now(&m,0)==30000);
 d->elapsed_at_ms=0;assert(xz_shell_elapsed_now(&m,0)==30000);
 d->elapsed_at_ms=1000;m.clock_ms=9000;assert(xz_shell_elapsed_now(&m,0)==30095);
 d->elapsed_at_ms=0xfffffff0u;m.clock_ms=0x10;d->playback_rate_ppm=-1000000;
 assert(xz_shell_elapsed_now(&m,0)==29968);
 d->elapsed_ms=1010;d->elapsed_at_ms=1000;m.clock_ms=1040;
 d->loop=(struct xz_shell_loop){1,1,1,1000,2000};d->known|=K(LOOP);
 assert(xz_shell_elapsed_now(&m,0)==1970);
 d->elapsed_ms=1990;d->playback_rate_ppm=1000000;assert(xz_shell_elapsed_now(&m,0)==1030);
 d->loop.active=0;d->elapsed_ms=10;d->playback_rate_ppm=-1000000;assert(xz_shell_elapsed_now(&m,0)==0);
 d->elapsed_ms=d->total_ms-10;d->playback_rate_ppm=1000000;assert(xz_shell_elapsed_now(&m,0)==d->total_ms);
 puts("PASS signed PCM-timeline interpolation, paused scratching, full-frame reference parity, no cosmetic SYNC alignment, unknown/stale/future holds, wrap and bounds");
 return 0;
}
