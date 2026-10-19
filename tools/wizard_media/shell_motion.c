/* The new interface in motion, for the desktop setup wizard: the FLAGSHIP
   shell's own renderer (mods/shell) driven through fixture decks frame by
   frame. Both decks play, the beat counters and playheads advance, deck 2's
   vocal stem mutes and unmutes, then BROWSE opens, the sidebar and the track
   cursor move, and PLAY returns. Host only, fixture data, never the deck.

   Usage: shell_motion OUT.rgb FPS SECONDS POSTER.ppm
   Writes raw RGB24 800x480 frames to OUT.rgb and one PLAY frame to POSTER.ppm. */
#include "browse_fixtures.h"
#include "shell_source.h"
#include "shell_settings.h"
#include "settings_fixtures.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static unsigned char rgb[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT*3];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static struct xz_shell_source_model sources;

static void key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static void turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1},out);
}
static void push(struct xz_shell *u, const struct xz_shell_model *m) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1},out);
}
static void to_rgb(void) {
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  rgb[i*3]=(unsigned char)(((p>>11)&31)*255/31);rgb[i*3+1]=(unsigned char)(((p>>5)&63)*255/63);rgb[i*3+2]=(unsigned char)((p&31)*255/31);
 }
}
/* Both decks advance with the clock, as the runtime stamps them each frame. */
static void play(struct xz_shell_model *m, const int32_t start[2], uint32_t t_ms) {
 m->clock_ms=123456u+t_ms;
 for(int d=0;d<2;d++){
  struct xz_shell_deck *k=&m->deck[d];
  int32_t at=start[d]+(int32_t)((int64_t)t_ms*(10000+k->tempo_x100)/10000);
  if(at>=k->total_ms-1000)at=start[d];
  k->elapsed_ms=at;k->elapsed_at_ms=m->clock_ms;
  k->bar_phase=(int)(((int64_t)at*100/FX_BEAT_MS)%4)+1;
 }
}
int main(int argc, char **argv) {
 if(argc!=5){fputs("usage: shell_motion OUT.rgb FPS SECONDS POSTER.ppm\n",stderr);return 2;}
 int fps=atoi(argv[2]),seconds=atoi(argv[3]);
 if(fps<10||fps>60||seconds<2||seconds>20)return 2;
 FILE *raw=fopen(argv[1],"wb");if(!raw){perror(argv[1]);return 1;}
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 fx_track_build(&track,7);bx_build(&lib);
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SOURCE|
  XZ_SHELL_CAP_PLAYLIST_EDIT|XZ_SHELL_CAP_FILTER|XZ_SHELL_CAP_STEM_MUTE;
 for(int i=0;i<2;i++){sources.device[i].known=(1u<<XZ_SHELL_D_STATUS)|(1u<<XZ_SHELL_D_NAME)|(1u<<XZ_SHELL_D_COLOR);sources.device[i].status=XZ_SHELL_DEVICE_READY;}
 sources.device[0].name="XZ SET 2026";sources.device[0].color=0x8c7a00;sources.device[1].name="BACKUP";sources.device[1].color=0x2a6cf0;
 sources.current_known=1;sources.current=XZ_SHELL_SRC_USB1;
 m.source=&sources;
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);m.browse=&b;(void)bx_folders;
 m.settings_screen=fxs_reset();(void)fxs_item;
 for(int d=0;d<2;d++){m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);m.deck[d].stem_muted=0;}
 m.deck[1].key="Cm";
 /* Deck 2 sits in the breakdown, so its vocal stem is singing when it mutes. */
 const int32_t start[2]={82523,(int32_t)((int64_t)FX_BEAT_MS*4*8*3/100+FX_BEAT_MS/100)};
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;xz_shell_browse_reset();
 int total=fps*seconds;
 /* Script, in frames: mute 1.4 s, unmute 3.0 s, BROWSE 4.0 s, sidebar down 4.4 and back 4.8, enter 5.2,
    tracks every 0.2 s, PLAY again at seconds-2. */
 int f_mute=fps*14/10,f_unmute=fps*3,f_browse=fps*4,f_play=total-2*fps;
 for(int f=0;f<total;f++){
  uint32_t t=(uint32_t)((int64_t)f*1000/fps);
  play(&m,start,t);
  if(f==f_mute)m.deck[1].stem_muted=4;
  if(f==f_unmute)m.deck[1].stem_muted=0;
  if(f==f_browse)key(&u,&m,XZ_SHELL_KEY_SOURCE_USB1);
  if(f==f_browse+fps*4/10)turn(&u,&m,1);      /* the sidebar cursor steps down and back to TRACK */
  if(f==f_browse+fps*8/10)turn(&u,&m,-1);
  if(f==f_browse+fps*12/10)push(&u,&m);
  if(f>f_browse+fps*14/10&&f<f_play-fps/5&&(f-f_browse)%(fps/5)==0)turn(&u,&m,1);
  if(f==f_play)key(&u,&m,XZ_SHELL_KEY_BROWSE);   /* BROWSE again returns to PLAY */
  if(!xz_shell_render(&u,&m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed at frame %d\n",f);return 1;}
  to_rgb();
  if(fwrite(rgb,1,sizeof rgb,raw)!=sizeof rgb){perror("write");return 1;}
  if(f==fps*2){
   FILE *poster=fopen(argv[4],"wb");if(!poster){perror(argv[4]);return 1;}
   fprintf(poster,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);fwrite(rgb,1,sizeof rgb,poster);fclose(poster);
  }
 }
 fclose(raw);
 return 0;
}
