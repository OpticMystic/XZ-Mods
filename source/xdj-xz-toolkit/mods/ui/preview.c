#include "ui.h"
#include "wave_viewport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t pixels[800*480];
static void save(const char *directory,const char *name,int width,int height){
 char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
 FILE *file=fopen(path,"wb");if(!file)exit(1);
 fprintf(file,"P6\n%d %d\n255\n",width,height);
 for(int i=0;i<width*height;i++){
  unsigned value=pixels[i];unsigned char rgb[3]={((value>>11)&31)*255/31,((value>>5)&63)*255/63,(value&31)*255/31};
  if(fwrite(rgb,1,3,file)!=3)exit(1);
 }
 if(fclose(file)!=0)exit(1);
}
int main(int argc,char **argv){
 if(argc!=2)return 1;
 struct xz_ui ui;struct xz_ui_model model={0};xz_ui_init(&ui);
 model.jump=xz_jump_defaults();model.jump.enabled=1;ui.jump_edit_page=1;
 model.pad_feedback=1;model.settings_status="SETTINGS SAVED TO USB 1";
 model.enabled=XZ_UI_STEM;model.server_auto=1;
 for(int deck=0;deck<4;deck++){
  struct xz_ui_deck *d=&model.deck[deck];
  d->ready=deck<2?XZ_UI_GATE|XZ_UI_SMART|XZ_UI_THEME|XZ_UI_STEM:XZ_UI_THEME;
  d->groove_active=d->sample_active=-1;d->sample_volume=1;
  d->levels[0]=d->levels[1]=d->levels[2]=1;
  d->track=deck<2?"Night Drive (Extended Mix)":"EXTERNAL USB AUDIO";
  d->status=deck<2?"STEMS READY":"CONTROL EXTERNAL AUDIO IN REKORDBOX OR SERATO";
  d->bpm=deck<2?128:0;
 }
 model.connection.ready=1;model.connection.enabled=1;
 model.connection.device_ip="169.254.0.2";model.connection.port=50005;
 model.connection.protocol="VJFS / VJVP / VJTE + VJXZ";
 model.connection.status="WAITING FOR VJ.Tools";
 static const char *names[]={"stems","xpad","settings","themes","connection","controls","beat-jump","extras","doom","doom-setup","wad-loader","network-updates","update-setup","games","myhouse-setup"};
 char name[80];
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  model.theme=theme;
  for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
   ui.page=(enum xz_ui_page)page;
   if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
   snprintf(name,sizeof(name),"theme-%d-%s",theme,names[page]);save(argv[1],name,800,480);
  }
  ui.page=XZ_UI_STEMS;model.deck[0].muted=1;model.deck[0].levels[1]=.7f;
  if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
  snprintf(name,sizeof(name),"theme-%d-muted",theme);save(argv[1],name,800,480);
  if(!xz_ui_inline_render(&ui,&model,pixels,536*XZ_WAVE_INLINE_HEIGHT,536,536,XZ_WAVE_INLINE_HEIGHT))return 1;
  snprintf(name,sizeof(name),"theme-%d-inline",theme);save(argv[1],name,536,XZ_WAVE_INLINE_HEIGHT);
  model.deck[0].muted=0;model.deck[0].levels[1]=1;
 }
 struct xz_ui_model baseline=model;
 static unsigned char peaks[256];for(int j=0;j<256;j++)peaks[j]=(unsigned char)(20+(j*37)%210);
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  model=baseline;model.theme=theme;model.enabled=XZ_UI_STEM|XZ_UI_SAMPLE;model.stems_overlay=1;
  for(int j=0;j<2;j++){model.deck[j].ready=0x3fff;model.deck[j].groove_assigned=255;model.deck[j].groove_loaded=255;model.deck[j].sample_loaded=255;model.deck[j].groove_active=2;model.deck[j].sample_active=3;model.deck[j].wave_peaks=peaks;model.deck[j].wave_count=256;model.deck[j].sample_volume=.62f;model.deck[j].pitch=3.5f;model.deck[j].loop_index=3;for(int k=0;k<8;k++)model.deck[j].groove_stem[k]=(unsigned char)(k%3);}
  model.connection.connected=model.connection.stats_valid=model.connection.can_enable=model.connection.can_discover=1;model.connection.frame_hz=47.8f;model.connection.status="CONNECTED / PREVIEW FIXTURE";
  for(int page=0;page<XZ_UI_PAGE_COUNT;page++){ui.page=(enum xz_ui_page)page;xz_ui_render(&ui,&model,pixels,800*480,800);snprintf(name,sizeof(name),"theme-%d-active-%s",theme,names[page]);save(argv[1],name,800,480);}
  model=baseline;model.theme=theme;ui.page=XZ_UI_STEMS;ui.deck=2;xz_ui_render(&ui,&model,pixels,800*480,800);snprintf(name,sizeof(name),"theme-%d-external",theme);save(argv[1],name,800,480);
  ui.deck=0;model.enabled=0;model.deck[0].ready=0;model.deck[0].track="NO TRACK LOADED";model.deck[0].bpm=0;model.deck[0].status="LOAD A TRACK WITH A STEM CACHE";
  xz_ui_render(&ui,&model,pixels,800*480,800);snprintf(name,sizeof(name),"theme-%d-unavailable",theme);save(argv[1],name,800,480);
  xz_ui_inline_render(&ui,&model,pixels,536*XZ_WAVE_INLINE_HEIGHT,536,536,XZ_WAVE_INLINE_HEIGHT);snprintf(name,sizeof(name),"theme-%d-inline-unavailable",theme);save(argv[1],name,536,XZ_WAVE_INLINE_HEIGHT);
 }
 model=baseline;
 model.theme=0;ui.deck=2;
 xz_ui_render(&ui,&model,pixels,800*480,800);save(argv[1],"external",800,480);
 ui.deck=0;model.enabled=0;model.deck[0].ready=XZ_UI_GATE|XZ_UI_SMART|XZ_UI_THEME;
 model.deck[0].bpm=0;model.deck[0].track="NO TRACK LOADED";model.deck[0].status="LOAD A TRACK WITH A STEM CACHE";
 xz_ui_render(&ui,&model,pixels,800*480,800);save(argv[1],"unavailable",800,480);
 memset(pixels,0,sizeof(pixels));xz_ui_render_badge(pixels,800);save(argv[1],"badge",800,480);
 char path[1024];snprintf(path,sizeof(path),"%s/themes.txt",argv[1]);FILE *list=fopen(path,"w");if(!list)return 1;
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)fprintf(list,"%d\t%s\n",theme,xz_theme_name(theme));
 if(fclose(list))return 1;
 printf("Rendered all pages, %d themes, mute/inline/external/unavailable states\n",XZ_THEME_COUNT);
}
