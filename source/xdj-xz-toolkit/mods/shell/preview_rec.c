/* Renders the REC mark (ui/rec_mark.h) on the shell's screens to PPM files in argv[1], one per scenario and
   recorder state. Scenarios: PLAY with two and four decks, BEAT FX off and lit (the longest name with TAP,
   and ECHO 1/2), the mode lamps hidden, a stem layout, 2 DECKS showing decks 3 and 2, 4 DECKS in mixer
   order, 4 DECKS waiting for decks 3 and 4; BROWSE and INFO. States: idle, unknown, calm 00:03, long 1:12:40
   (both blink halves), ten hours, a reading without a clock. Host only; uses only the shell's public model,
   so the same file renders any version of the mark for comparison. */
#include "four_deck_fixtures.h"
#include "browse_fixtures.h"
#include "shell_info_fixtures.h"
#include "../ui/rec_mark.h"
#include "../ui/layout_cycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

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
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_init(u);xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}

/* A reading as the runtime's tracker gives it, stamped with the model clock. */
enum state { S_IDLE, S_UNKNOWN, S_CALM, S_LONG, S_LONG_DARK, S_TEN, S_NO_CLOCK, S_COUNT };
static const char *const state_names[S_COUNT]={"idle","unknown","calm","long","long-dark","ten","no-clock"};
static void set_state(struct xz_shell_model *m, enum state s) {
 static const uint32_t ms[S_COUNT]={0,0,3000u,(72u*60u+40u)*1000u,(72u*60u+40u)*1000u+600u,10u*3600000u,0};
 struct xz_rec_reading r={0};
 if(s==S_IDLE){r.known=1;r.read_at_ms=m->clock_ms;}
 else if(s!=S_UNKNOWN){r.known=1;r.recording=1;r.elapsed_known=s!=S_NO_CLOCK;r.elapsed_ms=ms[s];r.read_at_ms=m->clock_ms;r.generation=7;}
 m->recorder=r;
}
static void states(struct xz_shell *u, struct xz_shell_model *m, const char *prefix) {
 for(int i=0;i<S_COUNT;i++){
  char name[96];snprintf(name,sizeof name,"%s-%s",prefix,state_names[i]);
  set_state(m,(enum state)i);shot(u,m,name);
 }
 m->recorder=(struct xz_rec_reading){0};
}
static void beat_fx(struct xz_shell_model *m, int which) {
 if(which==0)fx_beat_fx(&m->fx,0,"ECHO",1,2,234);
 else if(which==1){fx_beat_fx(&m->fx,1,"VINYL BRAKE",64,1,29999);m->fx.tap=1;}
 else fx_beat_fx(&m->fx,1,"ECHO",1,2,234);
}

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 struct xz_shell_info_model im;
 if(argc<2){fputs("usage: preview_rec OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 (void)bx_folders;(void)fxi_source_full;
 fx_track_build(&track,7);bx_build(&lib);
 static const char *const fx_names[3]={"fx-off","fx-lit","fx-echo"};
 for(int fx=0;fx<3;fx++){
  char name[64];
  xz_shell_init(&u);fx_model_full(&m,&track);m.stems_known=1;beat_fx(&m,fx);
  snprintf(name,sizeof name,"play-2deck-%s",fx_names[fx]);states(&u,&m,name);
  xz_shell_init(&u);fx_model_four(&m,&track);m.stems_known=1;beat_fx(&m,fx);
  snprintf(name,sizeof name,"play-4deck-%s",fx_names[fx]);states(&u,&m,name);
  xz_shell_init(&u);u.display.hidden|=1u<<XZ_SHELL_IND_MODE_LAMPS;fx_model_full(&m,&track);m.stems_known=1;beat_fx(&m,fx);
  snprintf(name,sizeof name,"play-nolamps-%s",fx_names[fx]);states(&u,&m,name);
 }
 /* 2 DECKS with the left side on deck 3: the button names "3 2". */
 xz_shell_init(&u);fx_model_four(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_TWO+1;m.sides_extra=1;beat_fx(&m,0);
 states(&u,&m,"play-pair32");
 /* 4 DECKS in mixer order (3 1 2 4). */
 xz_shell_init(&u);fx_model_four(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_MIXER+1;beat_fx(&m,0);
 states(&u,&m,"play-mixer");
 /* 4 DECKS chosen while decks 3 and 4 are not connected: grey numbers and the amber dot. */
 xz_shell_init(&u);fx_model_full(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_FOUR+1;beat_fx(&m,0);
 states(&u,&m,"play-waiting");
 /* A stem layout, rows below the waveforms. */
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;fx_model_full(&m,&track);beat_fx(&m,0);
 m.capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;m.stems_known=m.stems_on=1;
 m.stem_layout_known=1;m.stem_layout=XZ_SHELL_STEMS_BELOW_FULL;
 for(int d=0;d<2;d++){m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);m.deck[d].stem_gain[0]=85;m.deck[d].stem_gain[1]=60;m.deck[d].stem_gain[2]=100;}
 states(&u,&m,"play-stems");

 /* BROWSE and INFO: the header slot left of X. */
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD;
 m.browse=&b;bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);states(&u,&m,"browse");
 fxi_artwork(art);fxi_info_full(&im,art);m.info=&im;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO);states(&u,&m,"info");
 return 0;
}
