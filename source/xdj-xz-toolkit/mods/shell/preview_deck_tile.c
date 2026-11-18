/* Renders the deck tiles and the side mark (which physical side drives a deck) on the shell's screens to PPM
   files in argv[1]. Scenarios: decks 3 and 4 not connected; 2 DECKS with the sides on 1 2, 3 2 and 3 4;
   4 DECKS in number and mixer order with each side on either deck; a stem layout; BROWSE and INFO with the
   four-deck status strip; 4 DECKS waiting for decks 3 and 4. Host only; uses only the shell's public model,
   so the same file renders any version of the mark for comparison. */
#include "four_deck_fixtures.h"
#include "browse_fixtures.h"
#include "shell_info_fixtures.h"
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
/* Decks 3 and 4 connected, the deck select seam present, the sides on sides (bit 0 left on 3, bit 1 right on 4). */
static void four(struct xz_shell_model *m, int layout, int sides) {
 fx_model_four_players(m,&track);m->stems_known=1;
 m->layout_preference=layout+1;m->sides_extra=(uint8_t)sides;
}
static void stems(struct xz_shell *u, struct xz_shell_model *m) {
 u->display.wave_color=XZ_SHELL_WAVE_STEMS;
 m->capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;m->stems_known=m->stems_on=1;
 m->stem_layout_known=1;m->stem_layout=XZ_SHELL_STEMS_BELOW_FULL;
 for(int d=0;d<4;d++){m->deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
  m->deck[d].stem_gain[0]=85;m->deck[d].stem_gain[1]=60;m->deck[d].stem_gain[2]=100;}
}

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_info_model im;
 if(argc<2){fputs("usage: preview_deck_tile OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 (void)bx_folders;(void)fxi_source_full;
 fx_track_build(&track,7);bx_build(&lib);
 static const char *const pair[4]={"1-2","3-2","1-4","3-4"};
 char name[96];
 /* Decks 3 and 4 not connected (or a unit without them): two decks, no choice of deck per side. */
 xz_shell_init(&u);fx_model_full(&m,&track);m.stems_known=1;shot(&u,&m,"route-down");
 xz_shell_init(&u);u.deck=1;shot(&u,&m,"route-down-focus2");
 /* 2 DECKS: the pair the sides drive. */
 for(int s=0;s<4;s++){xz_shell_init(&u);four(&m,XZ_LAYOUT_TWO,s);snprintf(name,sizeof name,"two-%s",pair[s]);shot(&u,&m,name);}
 xz_shell_init(&u);u.deck=1;four(&m,XZ_LAYOUT_TWO,0);shot(&u,&m,"two-1-2-focus2");
 /* 4 DECKS, number and mixer order. */
 for(int s=0;s<4;s++){
  xz_shell_init(&u);four(&m,XZ_LAYOUT_FOUR,s);snprintf(name,sizeof name,"four-%s",pair[s]);shot(&u,&m,name);
  xz_shell_init(&u);four(&m,XZ_LAYOUT_MIXER,s);snprintf(name,sizeof name,"mixer-%s",pair[s]);shot(&u,&m,name);
 }
 /* The shell's focus on a deck no side drives (BROWSE loads there). */
 xz_shell_init(&u);u.deck=2;four(&m,XZ_LAYOUT_FOUR,0);shot(&u,&m,"four-1-2-focus3");
 /* 4 DECKS chosen while decks 3 and 4 are not connected: placeholders. */
 xz_shell_init(&u);fx_model_full(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_FOUR+1;shot(&u,&m,"four-waiting");
 /* A stem layout. */
 xz_shell_init(&u);four(&m,XZ_LAYOUT_TWO,0);stems(&u,&m);shot(&u,&m,"stems-two-1-2");
 xz_shell_init(&u);four(&m,XZ_LAYOUT_TWO,1);stems(&u,&m);shot(&u,&m,"stems-two-3-2");
 xz_shell_init(&u);fx_model_full(&m,&track);stems(&u,&m);shot(&u,&m,"stems-route-down");
 /* BROWSE and INFO under four decks: the status strip's quarters. */
 for(int s=0;s<4;s+=3){
  four(&m,XZ_LAYOUT_TWO,s==3?1:0);
  m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD;
  m.browse=&b;bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
  open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);snprintf(name,sizeof name,"browse-%s",s==3?"3-2":"1-2");shot(&u,&m,name);
  fxi_artwork(art);fxi_info_full(&im,art);m.info=&im;
  open_screen(&u,&m,XZ_SHELL_KEY_INFO);snprintf(name,sizeof name,"info-%s",s==3?"3-2":"1-2");shot(&u,&m,name);
 }
 return 0;
}
