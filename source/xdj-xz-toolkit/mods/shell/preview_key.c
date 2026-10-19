/* Renders the deck's live key and the KEY SYNC blink to key-*.ppm in argv[1]: KEY SYNC in both blink
   phases, a key moved by tempo with MASTER TEMPO off, an older runtime without the analysed key, and the
   stock indicators hidden. Host only. */
#include "fixtures.h"
#include "shell_settings.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;

static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 char path[1024];FILE *f;
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 snprintf(path,sizeof path,"%s/key-%s.ppm",out_dir,name);
 f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_key OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 xz_shell_init(&u);fx_model_full(&m,&track);
 /* Deck 2: KEY SYNC holds Bbm (from Am, +12 cents); MASTER TEMPO lit, then dark half a second later. */
 m.clock_ms=40000;shot(&u,&m,"sync-lit");
 m.clock_ms=40500;shot(&u,&m,"sync-dark");
 /* MASTER TEMPO off, no KEY SYNC: +2.20 % lifts A minor by 38 cents, still Am; at +6.10 % it reaches Bbm. */
 m.deck[1].key_synced=0;m.deck[1].key_shift=0;m.deck[1].master_tempo=0;
 m.deck[1].key="Am";m.deck[1].key_cents=38;shot(&u,&m,"tempo-cents");
 m.deck[1].tempo_x100=610;m.deck[1].bpm_x100=13581;m.deck[1].sync=0;
 m.deck[1].key="Bbm";m.deck[1].key_cents=2;shot(&u,&m,"tempo-semitone");
 /* Deck 1 drops below its analysed key with a key-shift effect; deck 2 tunes in flat. Wheel notation. */
 m.deck[0].key="Ebm";m.deck[0].key_shift=-2;m.deck[0].key_cents=0;
 m.deck[1].key="Bbm";m.deck[1].key_cents=-23;
 u.display.key_display=XZ_SHELL_KEY_ALPHANUMERIC;shot(&u,&m,"alphanumeric");
 u.display.key_display=XZ_SHELL_KEY_AS_DECK;
 /* An older runtime: no analysed key and no KEY SYNC bit. The key and its key-shift step, as before. */
 fx_model_full(&m,&track);
 for(int d=0;d<XZ_SHELL_DECKS;d++)m.deck[d].known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_KEY_ORIGINAL)|XZ_SHELL_KNOWN(XZ_SHELL_F_KEY_SYNC));
 shot(&u,&m,"old-runtime");
 /* Nothing about the key read, and a key name the key face cannot draw. */
 fx_model_full(&m,&track);
 m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_KEY);
 m.deck[1].key="Abmaj7/Eb";m.deck[1].key_original="Abm";m.deck[1].key_cents=-50;
 shot(&u,&m,"unknown-and-odd");
 /* The stock indicators hidden: no mode lamps (the FX strip takes their room) and no QUANTIZE lamps. */
 fx_model_full(&m,&track);
 xz_shell_restore_setting(&u,XZ_SHELL_ITEM_MODE_LAMPS,1);xz_shell_restore_setting(&u,XZ_SHELL_ITEM_QUANTIZE_LAMP,1);
 shot(&u,&m,"indicators-hidden");
 fx_beat_fx(&m.fx,1,"ECHO",1,2,234);shot(&u,&m,"indicators-hidden-fx-on");
 return 0;
}
