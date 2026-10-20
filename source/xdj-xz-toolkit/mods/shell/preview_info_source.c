/* Renders the INFO and SOURCE fixtures to PPM files in argv[1]. Host only. */
#include "shell_info_fixtures.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];
static const char *out_dir;
static int browse_slot;

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
static void model(struct xz_shell_model *m, struct xz_shell_info_model *im, struct xz_shell_source_model *sm) {
 fx_model_full(m,&track);
 fxi_info_full(im,art);
 fxi_source_full(sm);
 m->info=im;m->source=sm;m->browse=(const struct xz_shell_browse_model *)&browse_slot;
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SOURCE;
}
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(u);
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static void turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1},out);
}
int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 if(argc<2){fputs("usage: preview_info_source OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 fxi_artwork(art);

 /* INFO */
 model(&m,&im,&sm);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-deck1-full");
 u.stack[1].focus=2;shot(&u,&m,"info-deck2-long-title");
 u.stack[1].focus=1;turn(&u,&m,3);shot(&u,&m,"info-focus-key");
 turn(&u,&m,9);shot(&u,&m,"info-focus-genre");
 im.has_highlight=1;open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-list-highlight");
 im.has_highlight=0;
 im.deck[0].rating_writable=1;open_screen(&u,&m,XZ_SHELL_KEY_INFO);turn(&u,&m,9);shot(&u,&m,"info-rating-edit");
 im.deck[0].rating_writable=0;
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_BROWSE;open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-no-browse");

 model(&m,&im,&sm);
 im.deck[0].loaded=0;m.deck[0].loaded=0;open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-not-loaded");

 model(&m,&im,&sm);
 memset(&im,0,sizeof im);memset(&m.deck,0,sizeof m.deck);m.settings.known=0;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-unknown");

 model(&m,&im,&sm);
 static char wide[300];memset(wide,'W',sizeof wide-1);
 struct xz_shell_info_track *t=&im.deck[0];
 t->title="An Extremely Long Track Title That Keeps Going (Original Club Mix) [Remastered 2026] feat. Several Example Guests";
 t->artist=wide;t->album="A Compilation Album Name That Is Far Too Long To Fit In One Row Of This Page";t->genre=wide;
 t->label=wide;t->comment="Comment text that runs past the edge of the row and must be cut at the cell edge, not drawn over it";
 t->file_name=wide;t->key="Abmaj7/Eb";t->date_added=wide;t->bpm_x100=17450;t->bitrate_kbps=1411;t->duration_ms=99*60000+59000;
 t->color=XZ_SHELL_COLOR_PURPLE;t->rating=5;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-long-strings");

 /* SOURCE */
 model(&m,&im,&sm);
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);shot(&u,&m,"source-full");
 turn(&u,&m,5);shot(&u,&m,"source-cursor-computer");
 m.clock_ms=1300;sm.current=XZ_SHELL_SRC_USB2;open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);shot(&u,&m,"source-loading-current");

 model(&m,&im,&sm);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){sm.device[i].known=1u<<XZ_SHELL_D_STATUS;sm.device[i].status=XZ_SHELL_DEVICE_NOT_CONNECTED;}
 m.deck[0].loaded=0;m.deck[1].loaded=0;
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);shot(&u,&m,"source-none-connected");

 model(&m,&im,&sm);
 memset(&sm,0,sizeof sm);memset(&m.deck,0,sizeof m.deck);m.settings.known=0;
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);shot(&u,&m,"source-unknown");

 /* LINK and EXPORT MODE open in the deck's own screen (the runtime runs the stock key). */
 model(&m,&im,&sm);
 for(int i=2;i<4;i++){sm.device[i].known=1u<<XZ_SHELL_D_STATUS;sm.device[i].status=XZ_SHELL_DEVICE_STOCK;}
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);turn(&u,&m,2);shot(&u,&m,"source-stock-link");turn(&u,&m,1);shot(&u,&m,"source-stock-export");

 model(&m,&im,&sm);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){
  sm.device[i].known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1;sm.device[i].status=XZ_SHELL_DEVICE_READY;sm.device[i].name=wide;
  sm.device[i].free_bytes=1500000000000ull+(uint64_t)i*777777777ull;sm.device[i].total_bytes=4000000000000ull;sm.device[i].tracks=99999+i;
 }
 sm.device[1].color=0xffe020;sm.device[2].color=0xff64c8;sm.device[3].color=0x28e028;sm.device[0].free_bytes=210000000000ull;
 m.deck[1].source=XZ_SHELL_SRC_LINK;
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);turn(&u,&m,2);shot(&u,&m,"source-long-strings");
 return 0;
}
