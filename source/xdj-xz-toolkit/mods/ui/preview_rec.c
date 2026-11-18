/* Renders the REC mark (rec_mark.h) where the MODS runtime draws it, to PPM files in argv[1]:
   - CLASSIC: the top band over a stock play-screen capture (argv[2], raw RGB565 800x480), rebuilt with
     today's VJ.TOOLS, DECKS, METRICS, STEMS and MODS, in a spread of themes, VJ.TOOLS shown and hidden;
   - the MODS panel's HOME page header in the same themes.
   Recorder states from the host stub: not recording, 00:03, 1:12:40, unknown. Host only. */
#include "ui.h"
#include "ui_skin.h"
#include "deck_view.h"
#include "rec_mark.h"
#include "../recorder_state_stub.h"
#include "../recorder_state_stub.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t pixels[800*480],capture[800*480];
static const char *out_dir;
static void save(const char *name){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n800 480\n255\n");
 for(int i=0;i<800*480;i++){unsigned v=pixels[i];unsigned char rgb[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};fwrite(rgb,1,3,f);}
 fclose(f);
}
static const char *const state_names[4]={"unknown","idle","0003","11240"};
static struct xz_rec_reading reading(enum xz_recorder_stub_case which,uint32_t now){
 struct xz_rec_tracker t={0};struct xz_recorder_state s;
 xz_recorder_state_stub_case(XZ_RECORDER_STUB_IDLE);xz_recorder_state(&s);xz_rec_observe(&t,&s,now-100);
 xz_recorder_state_stub_case(which);xz_recorder_state(&s);
 return xz_rec_observe(&t,&s,now);
}
/* The CLASSIC band as ui_runtime.c badge() paints it, then the REC mark last. */
static void classic(int theme,int vj,int metrics,enum xz_recorder_stub_case which,const char *name){
 const uint32_t now=5000;
 memcpy(pixels,capture,sizeof pixels);
 for(int y=0;y<XZ_TOP_BAR_H;y++)memset(pixels+y*800,0,800*sizeof *pixels); /* the stock band is black */
 if(theme==XZ_THEME_LCARS)xz_ui_render_lcars_play_header(pixels,800);
 xz_ui_render_native_buttons(pixels,800,theme,1,vj,0);
 struct xz_metrics_sample sample={.sampled_ms=now,.valid=7,.cpu_permille=630,.used_mib=393,.temperature_c=56};
 xz_metrics_draw(pixels,800,theme,metrics,&sample,now);
 xz_deck_view_button_draw((struct xz_canvas){pixels,800,800,480,theme},&xz_deck_view_classic,0,0,0,0,0);
 static struct xz_rec_band_pixels saved;
 struct xz_rec_reading r=reading(which,now);struct xz_rec_view v=xz_rec_view_at(&r,now);
 xz_rec_band_draw(pixels,800,theme,vj,&v,&saved);saved.valid=0;
 save(name);
}
int main(int argc,char **argv){
 if(argc!=3){fputs("usage: preview_rec OUTPUT_DIR CAPTURE.rgb565\n",stderr);return 2;}
 out_dir=argv[1];
 FILE *f=fopen(argv[2],"rb");if(!f||fread(capture,sizeof capture,1,f)!=1){perror(argv[2]);return 1;}fclose(f);
 static const int themes[]={0,1,7,9,19,21,22,23,24};
 char name[96];
 for(size_t t=0;t<sizeof themes/sizeof *themes;t++){
  int theme=themes[t];
  for(int s=0;s<4;s++){
   snprintf(name,sizeof name,"rec-a-classic-theme-%d-vj-%s",theme,state_names[s]);classic(theme,1,1,(enum xz_recorder_stub_case)s,name);
   snprintf(name,sizeof name,"rec-a-classic-theme-%d-novj-%s",theme,state_names[s]);classic(theme,0,0,(enum xz_recorder_stub_case)s,name);
  }
 }
 /* The MODS panel, HOME page. */
 struct xz_ui ui;struct xz_ui_model model={0};
 xz_ui_init(&ui);model.settings_status="SETTINGS SAVED TO USB 1";model.jump=xz_jump_defaults();
 for(int d=0;d<4;d++){model.deck[d].track="Night Drive (Extended Mix)";model.deck[d].status="STEMS READY";model.deck[d].levels[0]=model.deck[d].levels[1]=model.deck[d].levels[2]=1;model.deck[d].groove_active=model.deck[d].sample_active=-1;}
 model.route.state=XZ_UI_ROUTE_NOT_DETECTED;model.clock_ms=5000;
 for(size_t t=0;t<sizeof themes/sizeof *themes;t++){
  model.theme=themes[t];ui.page=XZ_UI_HOME;
  for(int s=0;s<4;s++){
   model.recorder=reading((enum xz_recorder_stub_case)s,model.clock_ms);
   if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
   snprintf(name,sizeof name,"rec-a-mods-theme-%d-%s",themes[t],state_names[s]);save(name);
  }
 }
 return 0;
}
