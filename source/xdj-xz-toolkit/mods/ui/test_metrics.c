/* SPDX-License-Identifier: MIT */
#include "ui.h"
#include "native_touch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Metrics checks require assertions
#endif
static uint16_t pixels[800*480+2],other[800*480+2];
static uint16_t rgb(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static size_t color_count(uint16_t color){size_t n=0;for(int y=0;y<24;y++)for(int x=488;x<592;x++)n+=pixels[1+y*800+x]==color;return n;}
static void screenshot(const char *path){
 memcpy(other,pixels,sizeof(pixels));xz_ui_render_native_buttons(other+1,800,0,1,0,0);
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(other+1,2,800*480,f)==800*480);fclose(f);
}
static struct xz_ui_action action;static int calls;
struct stock_solver {uint32_t unused;struct xz_touch_status previous;};
static void apply(void *ctx,const struct xz_ui_action *a,size_t n){(void)ctx;if(n){action=a[0];calls++;}}
static void stock(void *ctx,const struct xz_touch_status *s,const void *m){(void)m;((struct stock_solver*)ctx)->previous=*s;}
int main(void){
 struct xz_metrics_counters previous={0};struct xz_metrics_sample sample;
 const char *mem="MemTotal: 1024000 kB\nMemFree: 102400 kB\nCached: 409600 kB\nShmem: 102400 kB\nBuffers: 102400 kB\nSReclaimable: 102400 kB\n";
 xz_metrics_parse(&previous,&sample,"cpu 0 0 0 0 0 0 0 0\n",mem,"79\n","anatoptz\n",1000);
 assert(sample.valid==(XZ_METRICS_RAM|XZ_METRICS_TEMP));assert(sample.used_mib==400&&sample.cache_mib==500&&sample.free_mib==100);
 xz_metrics_parse(&previous,&sample,"cpu 40 0 10 40 10 0 0 0 20 0\n",mem,"80\n","anatoptz\n",2000);
 assert(sample.cpu_permille==500&&sample.valid==7);assert(xz_metrics_fresh(&sample,5499)&&!xz_metrics_fresh(&sample,5500));
 assert(!xz_metrics_temperature_level(79)&&xz_metrics_temperature_level(80)==1&&xz_metrics_temperature_level(89)==1&&xz_metrics_temperature_level(90)==2);
 xz_metrics_parse(&previous,&sample,"\n",mem,"55000","imx_thermal",3000);assert(!(sample.valid&XZ_METRICS_TEMP)&&!(sample.valid&XZ_METRICS_CPU));
 xz_metrics_parse(&previous,&sample,"",mem,"80junk","anatoptz",3000);assert(!(sample.valid&XZ_METRICS_TEMP));
 xz_metrics_parse(&previous,&sample,"cpu 1 0 0 1 0 0 0 0\ncpu0 0 0 0 0 0 0 0 0\ncpu1 0 0 0 0 0 0 0 0\ncpu2 0 0 0 0 0 0 0 0\ncpu3 0 0 0 0 0 0 0 0\n",mem,"55","anatoptz",4000);
 xz_metrics_parse(&previous,&sample,"cpu 21 0 0 81 0 0 0 0\ncpu0 100 0 0 0 0 0 0 0\ncpu1 0 0 0 100 0 0 0 0\ncpu2 30 0 0 70 0 0 0 0\ncpu3 50 0 0 50 0 0 0 0\n",mem,"55","anatoptz",5000);
 assert(sample.cpu_permille==200&&sample.core_permille[0]==1000&&sample.core_permille[1]==0&&sample.core_permille[2]==300&&sample.core_permille[3]==500);
 xz_metrics_parse(&previous,&sample,"cpu 0 0 0 0 0 0 0 0",mem,"55","anatoptz",6000);assert(!(sample.valid&XZ_METRICS_CPU));
 memset(pixels,0x5a,sizeof(pixels));sample=(struct xz_metrics_sample){.sampled_ms=1000,.valid=7,.cpu_permille=531,.used_mib=380,.total_mib=993,.temperature_c=79};
 for(int temperature=79;temperature<=90;temperature++){
  sample.temperature_c=temperature;xz_metrics_draw(pixels+1,800,0,1,&sample,1000);
  assert(pixels[0]==0x5a5a&&pixels[800*480+1]==0x5a5a);
  for(int y=24;y<480;y++)for(int x=0;x<800;x++)assert(pixels[1+y*800+x]==0x5a5a);
  for(int y=0;y<24;y++)for(int x=674;x<800;x++)assert(pixels[1+y*800+x]==0x5a5a);
  if(temperature>=80&&temperature<90)assert(color_count(rgb(0xffd43b))>0);
  else assert(!color_count(rgb(0xffd43b)));
  if(temperature==79)screenshot("metrics-79.rgb565");if(temperature==80)screenshot("metrics-80.rgb565");if(temperature==90)screenshot("metrics-90-on.rgb565");
 }
 memcpy(other,pixels,sizeof(pixels));xz_metrics_draw(pixels+1,800,0,1,&sample,1500);assert(memcmp(other,pixels,sizeof(pixels)));screenshot("metrics-90-off.rgb565");
 xz_metrics_draw(pixels+1,800,0,1,&sample,5000);assert(!color_count(rgb(0xff4242)));screenshot("metrics-stale.rgb565");
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)xz_metrics_draw(pixels+1,800,theme,1,&sample,1000);
 assert(xz_metrics_hit(594,0)&&xz_metrics_hit(671,23)&&!xz_metrics_hit(672,10)&&!xz_metrics_hit(620,24));
 struct xz_ui ui;struct xz_ui_model model={0};struct xz_native_touch touch;struct stock_solver solver={0};
 xz_ui_init(&ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);
 struct xz_touch_status s={1,{0,0,0},620,10};
 assert(xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));assert(action.kind==XZ_UI_METRICS_TOGGLE&&calls==1&&!touch.visible);
 s.x=760;assert(xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));assert(calls==1&&!touch.visible);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 model.metrics_enabled=1;s=(struct xz_touch_status){1,{0,0,0},400,10};assert(xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock));assert(action.kind==XZ_UI_METRICS_SETUP_OPEN);
 s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 s=(struct xz_touch_status){1,{0,0,0},675,10};xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(action.kind==XZ_UI_STEMS_OVERLAY);s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 s=(struct xz_touch_status){1,{0,0,0},760,10};xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);assert(touch.visible);s.down=0;xz_native_touch_dispatch(&touch,&solver,&s,NULL,stock);
 xz_ui_navigate(&ui,XZ_UI_METRICS_SETUP);model.metrics=sample;assert(xz_ui_render(&ui,&model,pixels+1,800*480,800));
 struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(&ui,&model,widgets);int verify=0;for(size_t i=0;i<count;i++)verify+=widgets[i].kind==XZ_UI_METRICS_CHECK;assert(verify==1);
 puts("PASS metrics counter units, cache accounting, 79/80/89/90 thresholds, red 1Hz flash, stale suppression, all themes, bounds, tap capture, STEMS/MODS preservation and setup");
 return 0;
}
