/* Renders the SOURCE screen in its main states to PPM files in argv[1]: the rear USB port with and without
   the DECK 3/4 loopback cable, starting, failed, four decks absent, a stick that cannot be read, and the
   LINK / REKORDBOX tiles while the cable blocks their USB part. Host only. */
#include "four_deck_fixtures.h"
#include "shell_source.h"
#include "shell_info_chrome.h"
#include "../extra_decks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;

static void save(const char *name) {
 char path[1024];FILE *f;
 snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 save(name);
}
static void key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static void open_source(struct xz_shell *u, const struct xz_shell_model *m) { xz_shell_init(u);key(u,m,XZ_SHELL_KEY_SOURCE); }
static void turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1},out);
}
/* The runtime's picture of a working set: USB 1 holds the music, LINK and REKORDBOX open stock's screens. */
static void sources(struct xz_shell_source_model *sm) {
 memset(sm,0,sizeof *sm);
 struct xz_shell_device *u1=&sm->device[0];
 u1->known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1;u1->status=XZ_SHELL_DEVICE_READY;u1->name="XZ SET 2026";
 u1->color=0xff8c1a;u1->free_bytes=44345298944ull;u1->total_bytes=63999836160ull;u1->tracks=1284;
 sm->device[1].known=1u<<XZ_SHELL_D_STATUS;sm->device[1].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 for(int i=2;i<4;i++){sm->device[i].known=1u<<XZ_SHELL_D_STATUS;sm->device[i].status=XZ_SHELL_DEVICE_STOCK;}
 sm->current_known=1;sm->current=XZ_SHELL_SRC_USB1;
}
static void route(struct xz_shell_model *m, int state, int part, const char *reason) {
 m->route_state=state;m->route_fail_part=part;
 snprintf(m->route_reason,sizeof m->route_reason,"%s",reason?reason:"");
}
int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_source_model sm;
 if(argc<2){fputs("usage: preview_source OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);

 /* Four decks up: the cable is in the rear USB port and in USB 2 (the music is in USB 1). */
 fx_model_four(&m,&track);m.deck[2].source=m.deck[3].source=XZ_SHELL_SRC_USB1;
 sources(&sm);m.source=&sm;m.capabilities|=XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_BROWSE;
 sm.device[1].status=XZ_SHELL_DEVICE_LOOPBACK;
 route(&m,XZ_EXTRA_ROUTE_READY,XZ_EXTRA_ROUTE_PART_NONE,"Decks 3 and 4 are ready.");
 open_source(&u,&m);shot(&u,&m,"source-01-cable-in");
 turn(&u,&m,1);shot(&u,&m,"source-02-cable-in-link");
 turn(&u,&m,1);shot(&u,&m,"source-03-cable-in-rekordbox");
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SOURCE_USB2);shot(&u,&m,"source-04-cable-in-usb2-key");
 /* Cable in, no music stick yet: the runtime cannot tell which top port holds the cable. */
 sm.device[0].known=1u<<XZ_SHELL_D_STATUS;sm.device[0].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 sm.device[1].status=XZ_SHELL_DEVICE_NOT_CONNECTED;sm.current_known=0;
 open_source(&u,&m);shot(&u,&m,"source-05-cable-in-no-music");

 /* Two decks: no cable found. */
 fx_model_full(&m,&track);sources(&sm);m.source=&sm;m.capabilities|=XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_BROWSE;
 sm.device[1].known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1;sm.device[1].status=XZ_SHELL_DEVICE_LOADING;sm.device[1].name="BACKUP";
 sm.device[1].free_bytes=3900000000ull;sm.device[1].total_bytes=31000000000ull;sm.device[1].tracks=212;sm.device[1].color=0x2fa84f;
 route(&m,XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"The loopback cable has not been detected.");
 m.clock_ms=900;
 open_source(&u,&m);shot(&u,&m,"source-06-no-cable");
 turn(&u,&m,1);turn(&u,&m,1);shot(&u,&m,"source-07-no-cable-link");
 route(&m,XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_NONE,"Starting decks 3 and 4.");
 open_source(&u,&m);shot(&u,&m,"source-08-starting");
 route(&m,XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_PART_CABLE,"The cable could not connect; press RETRY, or reconnect the cable.");
 open_source(&u,&m);shot(&u,&m,"source-09-failed");
 route(&m,XZ_EXTRA_ROUTE_NO_ENGINE,XZ_EXTRA_ROUTE_PART_NONE,NULL);
 open_source(&u,&m);shot(&u,&m,"source-10-no-four-decks");
 /* A stick whose library cannot be read; USB 2 pressed. */
 sm.device[1].known=1u<<XZ_SHELL_D_STATUS;sm.device[1].status=XZ_SHELL_DEVICE_UNREADABLE;
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SOURCE_USB2);shot(&u,&m,"source-11-unreadable");
 return 0;
}
