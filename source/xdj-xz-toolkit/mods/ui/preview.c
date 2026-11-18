#include "ui.h"
#include "wave_viewport.h"
#include "ram_notice.h"
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
/* REPORT AN ISSUE: each step and each result, and the error strip's REPORT THIS that opens it. */
static void report_previews(const char *directory,struct xz_ui_model base,int theme){
 static const char *const steps[]={"support-text","support-typed","support-send","support-sending","support-sent","support-saved","support-unsaved","support-strip"};
 char name[80];
 for(size_t k=0;k<sizeof steps/sizeof *steps;k++){
  struct xz_ui u;struct xz_ui_model m=base;xz_ui_init(&u);u.page=XZ_UI_SUPPORT;m.theme=theme;
  u.support.topic=XZ_SUPPORT_NOT_WORKING+1;u.support.entry=XZ_SUPPORT_FROM_EXTRAS;u.kb_shift=1;
  u.focus_kind=XZ_UI_SUPPORT_SKIP;u.focus_index=0;u.focus_deck=-1;
  if(k>=1){snprintf(u.support.text,sizeof u.support.text,"Deck 2 stopped when I loaded a track from the second USB");u.kb_shift=0;u.focus_kind=XZ_UI_SUPPORT_NEXT;}
  u.support.step=k<2?1:k==2?2:3;
  if(k==2){u.focus_kind=XZ_UI_SUPPORT_SEND;}
  if(k==3)m.support.phase=XZ_SUPPORT_WORKING;
  if(k==4){m.support.phase=XZ_SUPPORT_SENT;snprintf(m.support.reference,sizeof m.support.reference,"R-4F7K-2M9Q");
   u.focus_kind=XZ_UI_SUPPORT_DONE;}
  if(k==5){m.support.phase=XZ_SUPPORT_SAVED;snprintf(m.support.result,sizeof m.support.result,"OFFLINE");u.focus_kind=XZ_UI_SUPPORT_DONE;}
  if(k==6){m.support.phase=XZ_SUPPORT_UNSAVED;snprintf(m.support.result,sizeof m.support.result,"READ_ONLY");
   snprintf(m.support.detail,sizeof m.support.detail,"OFFLINE");u.focus_kind=XZ_UI_SUPPORT_RETRY;}
  if(k==7){u.page=XZ_UI_PLAY;u.focus_kind=XZ_UI_NONE;m.stock_window="E-8307: USB ACCESS ERROR";}
  if(k==3||k==4||k==5||k==6)u.support.step=3;
  if(!xz_ui_render(&u,&m,pixels,800*480,800))exit(1);
  snprintf(name,sizeof(name),"theme-%d-%s",theme,steps[k]);save(directory,name,800,480);
 }
 /* MODERN: a deck's counter error takes the strip, with REPORT THIS beside SHOW DECK. */
 if(theme==XZ_THEME_FLAGSHIP){
  struct xz_ui u;struct xz_ui_model m=base;xz_ui_init(&u);u.page=XZ_UI_PLAY;m.theme=theme;
  m.ui_interface=XZ_UI_INTERFACE_MODERN;m.modern_look.known=1;
  m.counter_error[1]=(struct xz_ui_counter_error){"E-8302","CANNOT PLAY TRACK"};
  if(!xz_ui_render(&u,&m,pixels,800*480,800))exit(1);
  snprintf(name,sizeof(name),"theme-%d-support-strip-counter",theme);save(directory,name,800,480);
 }
}
int main(int argc,char **argv){
 if(argc!=2)return 1;
 struct xz_ui ui;struct xz_ui_model model={0};xz_ui_init(&ui);
 model.browse_columns[0]=0;model.browse_columns[1]=2;model.browse_columns[2]=3;model.browse_key_prefix=model.browse_traffic=1;model.jump=xz_jump_defaults();model.jump.enabled=1;ui.jump_edit_page=1;
 model.pad_feedback=1;model.settings_status="SETTINGS SAVED TO USB 1";
 model.enabled=XZ_UI_STEM;model.server_auto=1;
 for(int deck=0;deck<4;deck++){
  struct xz_ui_deck *d=&model.deck[deck];
  d->ready=deck<2?XZ_UI_GATE|XZ_UI_SMART|XZ_UI_THEME|XZ_UI_STEM:XZ_UI_THEME;
  d->groove_active=d->sample_active=-1;d->sample_volume=1;
  d->levels[0]=d->levels[1]=d->levels[2]=1;
  d->track=deck<2?"Night Drive (Extended Mix)":"EXTERNAL USB AUDIO";
  d->status=deck<2?"STEMS READY":"CONTROL EXTERNAL AUDIO IN DJ SOFTWARE";
  d->bpm=deck<2?128:0;
 }
 model.connection.ready=1;model.connection.enabled=1;
 model.connection.device_ip="169.254.0.2";model.connection.port=50005;
 model.connection.protocol="VJFS / VJVP / VJTE + VJXZ";
 model.connection.status="WAITING FOR VJ.Tools";
 snprintf(model.support.loader_id,sizeof model.support.loader_id,"L-4F7K-2M9Q");model.support.loader_state=1;
 model.rekordbox_assign[0]=XZ_UI_RB_INTERFACE;model.rekordbox_assign[1]=XZ_UI_RB_STOCK; /* the defaults */
 model.route.state=XZ_UI_ROUTE_NOT_DETECTED;model.deck_view=0;
 for(int deck=0;deck<4;deck++)model.jump_page[deck]=deck<2?0:-1;
 static const char *names[]={"stems","xpad","settings","themes","connection","controls","beat-jump","extras","doom","doom-setup","wad-loader","network-updates","update-setup","games","myhouse-setup","key-setup","browse-setup","pc-stems","stem-colors","applications","applications-setup","ram-setup","stem-setup","metrics-setup","profile-setup","profile-edit","general","feature-setup","theme-pick","buttons","network","network-setup","network-edit","about","support",
  "home","play","stems-section","screen","four-decks","connect","stem-pads","screen-look","band-colors","language"};
 _Static_assert(sizeof(names)/sizeof(*names)==XZ_UI_PAGE_COUNT,"preview names every page");
 char name[80];
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  model.theme=theme;
  for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
   if(xz_ui_page_current((enum xz_ui_page)page)!=(enum xz_ui_page)page)continue; /* retired: opens its replacement */
   ui.page=(enum xz_ui_page)page;ui.feature_cap=XZ_UI_GATE; /* FEATURE SETUP checks gate cue or smart cue */
   model.metrics=(struct xz_metrics_sample){0};
   if(page==XZ_UI_METRICS_SETUP){
    model.clock_ms=1000;
    model.metrics=(struct xz_metrics_sample){.sampled_ms=1000,.valid=15,.cpu_permille=380,
     .used_mib=380,.total_mib=993,.free_mib=240,.temperature_c=64,.key_active_mask=15,
     .worker={{.samples=256,.p99_us=610,.max_us=720,.sampled_ms=1000,.key_active=1},
              {.samples=256,.p99_us=640,.max_us=760,.sampled_ms=1000,.key_active=1}}};
   }
   if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
   snprintf(name,sizeof(name),"theme-%d-%s",theme,names[page]);save(argv[1],name,800,480);
  }
  model.deck[0].muted=1;model.deck[0].levels[1]=.7f;
  report_previews(argv[1],model,theme);
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
  for(int page=0;page<XZ_UI_PAGE_COUNT;page++){if(xz_ui_page_current((enum xz_ui_page)page)!=(enum xz_ui_page)page)continue;ui.page=(enum xz_ui_page)page;xz_ui_render(&ui,&model,pixels,800*480,800);snprintf(name,sizeof(name),"theme-%d-active-%s",theme,names[page]);save(argv[1],name,800,480);}
  model=baseline;model.theme=theme;
  ui.deck=0;model.enabled=0;model.deck[0].ready=0;model.deck[0].track="NO TRACK LOADED";model.deck[0].bpm=0;model.deck[0].status="LOAD A TRACK WITH A STEM CACHE";
  xz_ui_inline_render(&ui,&model,pixels,536*XZ_WAVE_INLINE_HEIGHT,536,536,XZ_WAVE_INLINE_HEIGHT);snprintf(name,sizeof(name),"theme-%d-inline-unavailable",theme);save(argv[1],name,536,XZ_WAVE_INLINE_HEIGHT);
 }
 /* PC stems: each source/setup state in 3- and 4-stem mode on deck 1, the
    XZ USB row on deck 2 below it, plus the setup page. */
 enum {PC_SETUP_STATE,PC_READY,PC_FEEDBACK,PC_PENDING,PC_PRESSED,PC_LINK_LOST,PC_UNKNOWN,PC_STATES};
 static const char *pc_names[PC_STATES]={"setup","ready","feedback","pending","pressed","link-lost","unknown"};
 static const unsigned pc_masks[2]={11,15};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int mode=0;mode<2;mode++)for(int k=0;k<PC_STATES;k++){
  model=baseline;model.theme=theme;model.stems_overlay=1;xz_ui_init(&ui);ui.deck=0;
  struct xz_ui_pc_stems pc={.source=XZ_UI_SOURCE_PC,.connected=1,.verified=1,.target=0,.epoch=1,.caps=XZ_UI_PC_CAP_TOGGLE,.available=pc_masks[mode],.setup_step=XZ_UI_PC_STEP_DONE};
  if(k==PC_SETUP_STATE){pc.verified=0;pc.caps=0;pc.setup_step=XZ_UI_PC_STEP_TEST;}
  if(k>=PC_FEEDBACK&&k<=PC_PRESSED){pc.caps|=XZ_UI_PC_CAP_FEEDBACK;pc.known=15;pc.active=(1u<<XZ_UI_PC_DRUMS)|(1u<<XZ_UI_PC_VOCAL);}
  if(k==PC_PENDING)pc.pending=1u<<XZ_UI_PC_INST;
  if(k==PC_LINK_LOST){pc.connected=0;pc.verified=0;pc.caps=0;}
  if(k==PC_UNKNOWN)pc=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_UNKNOWN,.setup_step=XZ_UI_PC_STEP_DETECT};
  model.deck[0].pc_stems=pc;
  if(k==PC_PRESSED){
   struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_action a[XZ_UI_ACTIONS];size_t n=xz_ui_inline_layout(&ui,&model,536,XZ_WAVE_INLINE_HEIGHT,w);
   for(size_t i=0;i<n;i++)if(w[i].deck==0&&w[i].kind==XZ_UI_PC_TOGGLE&&w[i].index==XZ_UI_PC_INST)xz_ui_inline_touch(&ui,&model,536,XZ_WAVE_INLINE_HEIGHT,w[i].x+10,w[i].y+10,1,a);
  }
  if(!xz_ui_inline_render(&ui,&model,pixels,536*XZ_WAVE_INLINE_HEIGHT,536,536,XZ_WAVE_INLINE_HEIGHT))return 1;
  snprintf(name,sizeof(name),"theme-%d-inline-pc%d-%s",theme,mode?4:3,pc_names[k]);save(argv[1],name,536,XZ_WAVE_INLINE_HEIGHT);
  if(k==PC_PRESSED)continue;
  xz_ui_init(&ui);ui.page=XZ_UI_PC_STEMS;if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
  snprintf(name,sizeof(name),"theme-%d-pc%d-setup-%s",theme,mode?4:3,pc_names[k]);save(argv[1],name,800,480);
 }
 /* USB stems: every reason on deck 1's inline row, plus its setup page and
    General for two themes. Deck 2 stays ready unless the reason is global. */
 static const char *usb_names[]={"unknown","ready","engine","off","no-track","not-prepared","loading","aligning","error"};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int why=XZ_UI_USB_WHY_NONE;why<=XZ_UI_USB_WHY_ERROR;why++){
  model=baseline;model.theme=theme;model.stems_overlay=1;xz_ui_init(&ui);ui.deck=0;
  struct xz_ui_deck *d=&model.deck[0];d->usb_reason=(enum xz_ui_usb_reason)why;d->stem_format="OVERCUE 3";
  if(why==XZ_UI_USB_WHY_ENGINE){model.enabled=0;d->ready=model.deck[1].ready=XZ_UI_THEME;model.deck[1].usb_reason=XZ_UI_USB_WHY_ENGINE;d->stem_format=NULL;}
  if(why==XZ_UI_USB_WHY_OFF){model.enabled=0;model.deck[1].usb_reason=XZ_UI_USB_WHY_OFF;}
  if(why==XZ_UI_USB_WHY_NO_TRACK){d->ready&=~(uint32_t)XZ_UI_STEM;d->track=NULL;d->stem_format=NULL;}
  if(why==XZ_UI_USB_WHY_NOT_PREPARED){d->ready&=~(uint32_t)XZ_UI_STEM;d->stem_format=NULL;}
  if(why==XZ_UI_USB_WHY_LOADING||why==XZ_UI_USB_WHY_ALIGNING)d->stem_loading=1;
  if(why==XZ_UI_USB_WHY_ERROR){d->ready&=~(uint32_t)XZ_UI_STEM;d->stem_error="4-STEM SET INCOMPLETE: BASS FILE MISSING";}
  if(!xz_ui_inline_render(&ui,&model,pixels,536*XZ_WAVE_INLINE_HEIGHT,536,536,XZ_WAVE_INLINE_HEIGHT))return 1;
  snprintf(name,sizeof(name),"theme-%d-inline-usb-%s",theme,usb_names[why]);save(argv[1],name,536,XZ_WAVE_INLINE_HEIGHT);
  if(theme>1)continue;
  ui.page=XZ_UI_LOCAL_STEM_SETUP;if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
  snprintf(name,sizeof(name),"theme-%d-usb-setup-%s",theme,usb_names[why]);save(argv[1],name,800,480);
  ui.page=XZ_UI_GENERAL;if(!xz_ui_render(&ui,&model,pixels,800*480,800))return 1;
  snprintf(name,sizeof(name),"theme-%d-general-usb-%s",theme,usb_names[why]);save(argv[1],name,800,480);
 }
 /* CLASSIC, whole track in RAM after the USB went out: the card in DECK 1's lane on the wave surface (BELOW / HALF
    stem rows, as manual/captures/classic-play.png), over the key colour preview.py swaps for that capture. Then the
    calm card the player's own E-8307 window shows while the loopback cable explains it, at two window sizes. */
 {
  for(int i=0;i<XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT;i++)pixels[i]=0xf81f;
  struct xz_canvas surface={pixels,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0};struct xz_wave_layout g;
  if(!xz_wave_layout_get(XZ_STEM_LAYOUT_BELOW_HALF,&g))return 1;
  xz_ram_notice_two_lane(surface,(int)g.lane_y[0],(int)g.lane_height);
  save(argv[1],"classic-ram-surface-two",XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT);
  static const int sizes[2][2]={{304,60},{420,100}};static const char *const hub_names[2]={"hub-card","hub-card-large"};
  for(int k=0;k<2;k++){
   memset(pixels,0,sizeof(pixels));
   xz_hub_card_draw((struct xz_canvas){pixels,(size_t)sizes[k][0],sizes[k][0],sizes[k][1],0});
   save(argv[1],hub_names[k],sizes[k][0],sizes[k][1]);
  }
 }
 memset(pixels,0,sizeof(pixels));xz_ui_render_badge(pixels,800);save(argv[1],"badge",800,480);
 char path[1024];snprintf(path,sizeof(path),"%s/themes.txt",argv[1]);FILE *list=fopen(path,"w");if(!list)return 1;
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)fprintf(list,"%d\t%s\n",theme,xz_theme_name(theme));
 if(fclose(list))return 1;
 printf("Rendered all pages, %d themes, inline and PC stem states\n",XZ_THEME_COUNT);
}
