/* Renders the stem rows (every layout), a fader mid-slide, muted and playing stems, the two-tone STEMS
   waveform and the header STEMS marks to PPM files in argv[1]. Host only. */
#include "fixtures.h"
#include "shell_internal.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
static void model(struct xz_shell_model *m, enum xz_shell_stem_layout layout) {
 fx_model_full(m,&track);
 m->capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;
 m->stems_known=1;m->stems_on=layout!=XZ_SHELL_STEMS_HIDDEN;
 m->stem_layout_known=1;m->stem_layout=layout;
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  m->deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
  m->deck[d].stem_gain[0]=85;m->deck[d].stem_gain[1]=60;m->deck[d].stem_gain[2]=100;
 }
 /* deck 2 in the break, where the vocal sings */
 m->deck[1].elapsed_ms=FX_BEAT_MS*4*8*3/100+FX_BEAT_MS/100;
}

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_stems OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 static const char *const names[XZ_SHELL_STEM_LAYOUTS]={"stems-layout-hidden","stems-layout-below-full","stems-layout-below-half",
  "stems-layout-outside-full","stems-layout-outside-half"};
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;
  model(&m,(enum xz_shell_stem_layout)layout);
  shot(&u,&m,names[layout]);
 }
 /* A fader mid-slide: deck 1 DRUMS dragged down to 35 %, deck 2 INST muted. */
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);
 struct xz_shell_rect b=xz_shell_stem_button(&m,0,0);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,b.x+b.w-30,b.y+b.h/2,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_MOVE,b.x+b.w-30-b.w/2,b.y+b.h/2,0,0,-1},out);
 if(out[0].kind==XZ_SHELL_ACT_STEM_GAIN)m.deck[0].stem_gain[0]=(uint8_t)out[0].value;
 m.deck[1].stem_muted=2;
 shot(&u,&m,"stems-fader-mid-drag");
 /* Muted against playing: deck 1 all playing, deck 2 drums and vocal muted. */
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;
 model(&m,XZ_SHELL_STEMS_OUTSIDE_FULL);
 m.deck[1].stem_muted=5;
 shot(&u,&m,"stems-muted-vs-playing");
 /* The two-tone waveform, zoomed out and in, with the stem rows hidden so the lanes are full height. */
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;
 model(&m,XZ_SHELL_STEMS_HIDDEN);
 u.zoom=1;shot(&u,&m,"stems-waveform-two-tone-wide");
 u.zoom=5;shot(&u,&m,"stems-waveform-two-tone-close");
 /* The header marks through the cycle the runtime steps: 1, 2, 3, 4, 0. */
 static const char *const header[XZ_SHELL_STEM_LAYOUTS]={"stems-header-0-hidden","stems-header-1","stems-header-2","stems-header-3","stems-header-4"};
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  xz_shell_init(&u);model(&m,(enum xz_shell_stem_layout)layout);
  shot(&u,&m,header[layout]);
 }
 /* A DJ's own stem colours win. */
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;
 model(&m,XZ_SHELL_STEMS_BELOW_HALF);
 m.stem_rgb_known=1;m.stem_rgb_custom=7;m.stem_rgb[0]=0xe8b000;m.stem_rgb[1]=0x00b4c8;m.stem_rgb[2]=0xe040c0;
 shot(&u,&m,"stems-custom-colours");
 return 0;
}
