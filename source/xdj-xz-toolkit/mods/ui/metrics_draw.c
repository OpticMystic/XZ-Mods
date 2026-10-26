/* SPDX-License-Identifier: MIT */
#include "metrics.h"
#include "ui_skin.h"
#include <stdio.h>

void xz_metrics_draw(uint16_t *pixels,size_t stride,int theme,int enabled,const struct xz_metrics_sample *s,uint32_t now){
 if(!pixels||stride<800)return;
 struct xz_canvas c=xz_top_bar_canvas(pixels,stride,theme);const struct xz_theme_palette *p=xz_theme_palette(theme);
 xz_ui_top_button(c,XZ_METRICS_X,XZ_METRICS_W,"METRICS",enabled);
 if(!enabled)return;
 int ty=xz_theme_pixel_text(theme)?XZ_TOP_BAR_PIXEL_Y:XZ_TOP_BAR_LABEL_Y,h=XZ_TOP_BAR_H;
 xz_rect(c,XZ_METRICS_READOUT_X,0,XZ_METRICS_READOUT_W,h,p->bg);
 xz_border(c,XZ_METRICS_READOUT_X,0,XZ_METRICS_READOUT_W,h,xz_blend(p->bg,p->accent,150));
 if(!xz_metrics_fresh(s,now)){
  xz_text(c,292,ty,s&&s->valid?"METRICS STALE - TAP TO VERIFY":"METRICS STARTING - TAP TO SET UP",1,48,p->ink);return;
 }
 char text[40];
 if(s->valid&XZ_METRICS_CPU)snprintf(text,sizeof(text),"CPU %u%%",(s->cpu_permille+5)/10);else snprintf(text,sizeof(text),"CPU --");
 xz_text(c,292,ty,text,1,10,p->ink);
 if(s->valid&XZ_METRICS_RAM)snprintf(text,sizeof(text),"RAM %uM",s->used_mib);else snprintf(text,sizeof(text),"RAM --");
 xz_text(c,386,ty,text,1,12,p->ink);
 int level=(s->valid&XZ_METRICS_TEMP)?xz_metrics_temperature_level(s->temperature_c):0;
 uint32_t tone=level==2?0xff4242:level==1?0xffd43b:p->ink;
 if(level)xz_rect(c,488,1,103,h-2,level==1?0x302500:0x120304);
 if(level==2&&(now/500)%2==0){xz_rect(c,488,1,103,h-2,0x350509);xz_border(c,488,0,104,h,tone);}
 if(s->valid&XZ_METRICS_TEMP)snprintf(text,sizeof(text),"%dC%s",s->temperature_c,level==2?" HOT":level==1?" WARM":"");
 else snprintf(text,sizeof(text),"TEMP --");
 xz_text(c,497,ty,text,1,14,tone);
}
