/* DECK VIEW on PLAY (ui/deck_view.h, ui/route_card.h): the button, the picker, both four-deck orders, and
   every decks 3 and 4 card state, to PPM files in argv[1]. Host only, fixture data. */
#include "four_deck_fixtures.h"
#include "../extra_decks.h"
#include "../ui/deck_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;
static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 char path[1024];
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
/* Decks 3 and 4 down: the runtime publishes two decks and the route's own state and sentence. */
static void route_down(struct xz_shell_model *m, int view, int state, int part, const char *reason) {
 fx_model_full(m,&track);
 m->layout_preference=view+1;m->route_state=state;m->route_fail_part=part;
 snprintf(m->route_reason,sizeof m->route_reason,"%s",reason?reason:"");
 m->route_card_since_ms=m->clock_ms-4000;
}
/* The music USB went out while deck d plays its whole track from RAM: the runtime drops what it read from the USB
   (waveform, overview, grid, memory cues) and marks the deck RAM ONLY; everything the player reports stays. */
static void ram_only(struct xz_shell_model *m, int d) {
 struct xz_shell_deck *k=&m->deck[d];
 k->known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE)|XZ_SHELL_KNOWN(XZ_SHELL_F_OVERVIEW)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE)
  |XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|XZ_SHELL_KNOWN(XZ_SHELL_F_MEMORY_CUES));
 memset(&k->wave,0,sizeof k->wave);memset(&k->overview,0,sizeof k->overview);memset(&k->stems,0,sizeof k->stems);
 k->grid=NULL;k->grid_count=0;k->memory_count=0;
 k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_RAM_ONLY);k->ram_only=1;
}
int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_deck_view OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];fx_track_build(&track,7);
 xz_shell_init(&u);
 route_down(&m,XZ_LAYOUT_TWO,XZ_EXTRA_ROUTE_NOT_DETECTED,0,"The loopback audio route has not been detected.");
 shot(&u,&m,"deckview-01-two-decks");
 u.deck_view.open=1;u.deck_view.tile=1;u.deck_view.input_at=m.clock_ms;
 shot(&u,&m,"deckview-02-picker");
 xz_shell_init(&u);
 fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;shot(&u,&m,"deckview-03-four-1234-ready");
 m.layout_preference=XZ_LAYOUT_MIXER+1;shot(&u,&m,"deckview-04-four-3124-ready");
 m.layout_preference=XZ_LAYOUT_TWO+1;m.sides_extra=1;shot(&u,&m,"deckview-05-two-decks-pair-3-2");
 m.layout_preference=XZ_LAYOUT_TWO+1;m.sides_extra=0;m.notice=XZ_DECK_VIEW_READY_NOTICE;m.notice_at_ms=m.clock_ms-200;
 shot(&u,&m,"deckview-06-two-decks-ready-notice");
 m.clock_ms=123456+450;
 route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"The loopback audio route has not been detected.");
 m.clock_ms+=650;shot(&u,&m,"card-01-not-detected");
 route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_SCANNING,0,"Checking the loopback cable and audio route now.");
 m.clock_ms+=550;shot(&u,&m,"card-02-scanning");
 route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_SOCKET,"The cable was found and the audio input is starting.");
 m.clock_ms+=300;shot(&u,&m,"card-03-starting");
 route_down(&m,XZ_LAYOUT_MIXER,XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP,XZ_EXTRA_ROUTE_PART_SELECTOR,"Stop or pause decks 1 and 2 so decks 3 and 4 can start.");
 m.clock_ms+=700;shot(&u,&m,"card-04-waiting-decks-1-2-stop-mixer");
 route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_PART_PORT,"The cable is in USB 2. Move it to USB 1 and keep music in USB 2.");
 shot(&u,&m,"card-05-failed-port");
 route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_LOST,XZ_EXTRA_ROUTE_PART_CABLE,"The audio route was disconnected; check the loopback cable.");
 shot(&u,&m,"card-06-lost");
 route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_NO_ENGINE,0,"The software players are unavailable or have not been started.");
 shot(&u,&m,"card-07-no-engine");
 route_down(&m,XZ_LAYOUT_MIXER,XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"The loopback audio route has not been detected.");
 m.clock_ms+=900;shot(&u,&m,"card-08-not-detected-mixer");
 u.deck_view.focus=XZ_SHELL_FOCUS_CARD_PRIMARY;u.deck_view.input_at=m.clock_ms;
 shot(&u,&m,"card-09-encoder-focus-rescan");
 xz_shell_init(&u);
 fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;m.route_state=XZ_EXTRA_ROUTE_READY;
 m.route_card_ready_at_ms=m.clock_ms-300;shot(&u,&m,"card-10-ready-hold");
 m.route_card_ready_at_ms=m.clock_ms-800;shot(&u,&m,"card-11-ready-wipe");
 /* Whole track in RAM, USB out: the RAM card in each affected lane, every other readout kept. */
 xz_shell_init(&u);
 fx_model_full(&m,&track);m.layout_preference=XZ_LAYOUT_TWO+1;ram_only(&m,0);shot(&u,&m,"ram-01-two-decks-deck-1");
 ram_only(&m,1);shot(&u,&m,"ram-02-two-decks-both");
 fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;m.route_state=XZ_EXTRA_ROUTE_READY;
 ram_only(&m,1);ram_only(&m,3);shot(&u,&m,"ram-03-four-decks-2-4");
 m.layout_preference=XZ_LAYOUT_MIXER+1;shot(&u,&m,"ram-04-mixer-2-4");
 return 0;
}
