/* Host benchmark: microseconds per PLAY frame with DECK VIEW (ui/deck_view.h, ui/route_card.h): four decks
   up, four slots with decks 3 and 4 down and the card moving at 20 fps, 2 DECKS, and the open picker.
   Both native decks play, the clock advances one 60 fps frame per render. Median and mean. Host only. */
#include "four_deck_fixtures.h"
#include "../extra_decks.h"
#include "../ui/deck_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { FRAMES=600 };
static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static double took[FRAMES];
static double now_us(void) { struct timespec t;timespec_get(&t,TIME_UTC);return (double)t.tv_sec*1e6+(double)t.tv_nsec/1e3; }
static int by_value(const void *a, const void *b) { double x=*(const double *)a,y=*(const double *)b;return x<y?-1:x>y; }
static void run(const char *name, struct xz_shell *u, struct xz_shell_model *m) {
 double sum=0;unsigned check=0;
 for(int i=0;i<FRAMES;i++){
  m->clock_ms+=16;u->deck_view.input_at=u->deck_view.open?m->clock_ms:u->deck_view.input_at;
  for(int d=0;d<4;d++){m->deck[d].elapsed_ms+=16;m->deck[d].elapsed_at_ms=m->clock_ms-8;}
  double t0=now_us();xz_shell_render(u,m,frame,XZ_SHELL_WIDTH);took[i]=now_us()-t0;sum+=took[i];
  check+=frame[(size_t)(i%480)*XZ_SHELL_WIDTH+(size_t)(i%800)];
 }
 qsort(took,FRAMES,sizeof *took,by_value);
 printf("DECK VIEW %-34s median %.0f us/frame, mean %.0f us/frame (check %u)\n",name,took[FRAMES/2],sum/FRAMES,check&255u);
}
int main(void) {
 struct xz_shell u;struct xz_shell_model m;
 fx_track_build(&track,7);xz_shell_init(&u);
 fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;run("4 DECKS, 3 and 4 up",&u,&m);
 fx_model_full(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;m.route_state=XZ_EXTRA_ROUTE_NOT_DETECTED;
 snprintf(m.route_reason,sizeof m.route_reason,"The loopback audio route has not been detected.");m.route_card_since_ms=m.clock_ms;
 run("4 DECKS, 3 and 4 down, card moving",&u,&m);
 m.route_state=XZ_EXTRA_ROUTE_SCANNING;run("4 DECKS, 3 and 4 down, scanning",&u,&m);
 m.layout_preference=XZ_LAYOUT_TWO+1;run("2 DECKS",&u,&m);
 u.deck_view.open=1;u.deck_view.tile=1;u.deck_view.input_at=m.clock_ms;run("2 DECKS, picker open",&u,&m);
 return 0;
}
