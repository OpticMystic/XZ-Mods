/* SPDX-License-Identifier: MIT */
/* The CLASSIC top bar (top_bar.h): in every theme, every state of VJ.TOOLS / EXIT VJ, DECKS, the metrics
   readout, METRICS, STEMS, MODS, the LOAD ERR notice, the RAM readout and the LCARS play header paints only
   rows 0..17 and only inside its own columns; labels fit; the row shares one height and two-pixel gaps; and
   each touch target answers exactly as far down as top_bar.h says. */
#ifdef NDEBUG
#error Top bar acceptance requires assertions
#endif
#include "ui_skin.h"
#include "native_touch.h"
#include "deck_view.h"
#include "transient_notice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { STRIDE=832, SENTINEL=0x1234 };
static uint16_t frame[STRIDE*480];
struct column { int x,w; const char *name; };

static void clear(void){for(size_t i=0;i<sizeof frame/sizeof frame[0];i++)frame[i]=SENTINEL;}
/* Every changed pixel lies in rows 0..XZ_TOP_BAR_H-1 and inside one of the allowed columns. A selected Aqua
   button's glow ring may take one pixel of the two-pixel gap beside it, never the neighbour's column. */
static void painted_within(const struct column *allowed,int n,const char *what,int theme){
 for(int y=0;y<480;y++)for(int x=0;x<STRIDE;x++){
  if(frame[y*STRIDE+x]==SENTINEL)continue;
  int inside=0;for(int i=0;i<n;i++)inside|=x>=allowed[i].x-1&&x<allowed[i].x+allowed[i].w+1;
  if(y>=XZ_TOP_BAR_H||!inside){fprintf(stderr,"FAIL %s theme %d paints (%d,%d)\n",what,theme,x,y);assert(0);}
 }
}
/* Rows a column paints: the first and last row with a changed pixel. */
static void rows(struct column c,int *top,int *bottom){
 *top=480;*bottom=-1;
 for(int y=0;y<480;y++)for(int x=c.x;x<c.x+c.w;x++)if(frame[y*STRIDE+x]!=SENTINEL){if(y<*top)*top=y;if(y>*bottom)*bottom=y;}
}

static const struct column VJ={XZ_TOP_VJ_X,XZ_TOP_VJ_W,"VJ.TOOLS"},STEMS={XZ_TOP_STEMS_X,XZ_TOP_STEMS_W,"STEMS"},
 MODS={XZ_TOP_MODS_X,XZ_TOP_MODS_W,"MODS"},METRICS={XZ_METRICS_X,XZ_METRICS_W,"METRICS"},
 READOUT={XZ_METRICS_READOUT_X,XZ_METRICS_READOUT_W,"READOUT"};

static void paint_band(void){
 static const struct xz_metrics_sample samples[]={
  {.sampled_ms=1000,.valid=7,.cpu_permille=531,.used_mib=380,.temperature_c=55},
  {.sampled_ms=1000,.valid=7,.cpu_permille=1000,.used_mib=99999,.temperature_c=85},
  {.sampled_ms=1000,.valid=7,.cpu_permille=999,.used_mib=993,.temperature_c=95},
  {.sampled_ms=1000,.valid=XZ_METRICS_RAM,.used_mib=380},
  {0}};
 const struct xz_dv_rect b=xz_deck_view_classic.button;
 const struct column DECKS={b.x,b.w,"DECKS"};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  /* VJ.TOOLS, STEMS and MODS in every stem layout, with VJ hidden, shown and active. */
  for(int layout=0;layout<5;layout++)for(int vj=0;vj<3;vj++){
   clear();xz_ui_render_native_buttons(frame,STRIDE,theme,layout,vj>0,vj==2);
   const struct column allowed[3]={STEMS,MODS,VJ};painted_within(allowed,vj?3:2,"buttons",theme);
   for(int i=0;i<(vj?3:2);i++){
    int top,bottom;rows(allowed[i],&top,&bottom);
    if(top>1||bottom<XZ_TOP_BAR_H-2){fprintf(stderr,"FAIL %s theme %d rows %d..%d\n",allowed[i].name,theme,top,bottom);assert(0);}
   }
  }
  /* METRICS off and on; the readout fresh, warm, hot (both blink phases), partial, stale and starting. */
  clear();xz_metrics_draw(frame,STRIDE,theme,0,&samples[0],1000);painted_within(&METRICS,1,"metrics off",theme);
  for(size_t i=0;i<sizeof samples/sizeof samples[0];i++)for(uint32_t now=1000;now<=6000;now+=500){
   clear();xz_metrics_draw(frame,STRIDE,theme,1,&samples[i],now);
   const struct column allowed[2]={METRICS,READOUT};painted_within(allowed,2,"metrics",theme);
  }
  clear();xz_ui_render_ram_debug(frame,STRIDE,theme,1,1,540);painted_within(&READOUT,1,"ram",theme);
  clear();xz_ui_render_ram_debug(frame,STRIDE,theme,1,0,0);painted_within(&READOUT,1,"ram",theme);
  /* The LOAD ERR notice covers MODS and restores it exactly. */
  struct xz_ui_transient_notice notice={0};struct xz_ui_notice_pixels saved={0};
  xz_ui_notice_set(&notice,"LOAD REFUSED",100);
  clear();xz_ui_notice_badge(frame,STRIDE,theme,&notice,100,&saved);painted_within(&MODS,1,"notice",theme);
  assert(saved.valid);xz_ui_notice_restore(&saved,frame,STRIDE);
  for(size_t i=0;i<sizeof frame/sizeof frame[0];i++)assert(frame[i]==SENTINEL);
  /* DECKS in every view, ready or not, open, focused, with either deck pair. */
  for(int view=0;view<3;view++)for(int state=0;state<16;state++){
   clear();xz_deck_view_button_draw((struct xz_canvas){frame,STRIDE,800,480,theme},&xz_deck_view_classic,view,state&1,(state>>1)&3,state&8?1:0,(state>>2)&1);
   painted_within(&DECKS,1,"decks",theme);
  }
 }
 /* The LCARS play header lives in the same band, between VJ.TOOLS and STEMS. */
 clear();xz_ui_render_lcars_play_header(frame,STRIDE);
 painted_within(&(struct column){XZ_TOP_VJ_X+XZ_TOP_VJ_W,XZ_TOP_STEMS_X-XZ_TOP_VJ_X-XZ_TOP_VJ_W,"LCARS"},1,"lcars header",XZ_THEME_LCARS);
 puts("PASS top bar paints rows 0..17 only, inside its own columns, for all 25 themes and every state");
}

/* One row: the same height as DECKS, two-pixel gaps from DECKS to MODS, MODS on the right edge, labels that fit
   above the STEMS marks. */
static void row_geometry(void){
 const struct xz_dv_rect b=xz_deck_view_classic.button;
 assert(b.y==0&&b.h==XZ_TOP_BAR_H&&xz_deck_view_classic.button_touch_h==XZ_TOP_BAR_DEEP_H);
 assert(XZ_TOP_VJ_X+XZ_TOP_VJ_W<b.x);
 assert(b.x+b.w+2==XZ_METRICS_READOUT_X&&XZ_METRICS_READOUT_X+XZ_METRICS_READOUT_W+2==XZ_METRICS_X);
 assert(XZ_METRICS_X+XZ_METRICS_W+2==XZ_TOP_STEMS_X&&XZ_TOP_STEMS_X+XZ_TOP_STEMS_W+2==XZ_TOP_MODS_X);
 assert(XZ_TOP_MODS_X+XZ_TOP_MODS_W==800);
 /* Atlas capitals are 9 rows, the pixel face 7; both clear the marks by a row, the marks clear the outline. */
 assert(XZ_TOP_BAR_LABEL_Y+9<XZ_TOP_BAR_MARK_Y&&XZ_TOP_BAR_PIXEL_Y+7<XZ_TOP_BAR_MARK_Y&&XZ_TOP_BAR_MARK_Y+2<XZ_TOP_BAR_H);
 assert(XZ_TOP_BAR_LABEL_Y==b.y+(b.h-9)/2);
 /* The marks stay inside STEMS. */
 assert(XZ_TOP_STEMS_X+8>XZ_TOP_STEMS_X+1&&XZ_TOP_STEMS_X+8+4*11+7<XZ_TOP_STEMS_X+XZ_TOP_STEMS_W-1);
 /* lamp: the ANALOG plate gives its first 9 columns to the lamp strip. */
 static const struct { const char *label; int w,lamp; } labels[]={
  {"VJ.TOOLS",XZ_TOP_VJ_W,1},{"EXIT VJ",XZ_TOP_VJ_W,1},{"STEMS",XZ_TOP_STEMS_W,1},{"MODS",XZ_TOP_MODS_W,1},
  {"LOAD ERR",XZ_TOP_MODS_W,0},{"METRICS",XZ_METRICS_W,1}};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  struct xz_canvas c={frame,STRIDE,800,480,theme};
  for(size_t i=0;i<sizeof labels/sizeof labels[0];i++){
   int w=xz_text_width(c,labels[i].label,1,40),room=labels[i].w-(theme==XZ_THEME_ANALOG&&labels[i].lamp?9:0)-8;
   if(w>room){fprintf(stderr,"FAIL %s is %d wide in %d (theme %d)\n",labels[i].label,w,room,theme);assert(0);}
  }
  int right=XZ_METRICS_READOUT_X+XZ_METRICS_READOUT_W-2;
  assert(292+xz_text_width(c,"METRICS STARTING - TAP TO SET UP",1,48)<=right);
  assert(497+xz_text_width(c,"95C HOT",1,14)<=right);
 }
 puts("PASS one row: DECKS height, two-pixel gaps, labels fit above the STEMS marks in every theme");
}

/* Every button's label reads against its own fill, off and on, in every theme: the label's full-coverage
   pixels are there and differ in brightness from the fill around them. */
static int luma(uint16_t v){return (((v>>11)&31)*255/31*299+((v>>5)&63)*255/63*587+(v&31)*255/31*114)/1000;}
static uint16_t to565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void label_contrast(void){
 static const struct column buttons[4]={{XZ_TOP_VJ_X,XZ_TOP_VJ_W,"VJ.TOOLS"},{XZ_METRICS_X,XZ_METRICS_W,"METRICS"},
  {XZ_TOP_STEMS_X,XZ_TOP_STEMS_W,"STEMS"},{XZ_TOP_MODS_X,XZ_TOP_MODS_W,"MODS"}};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int i=0;i<4;i++)for(int lit=0;lit<2;lit++){
  clear();struct xz_canvas c=xz_top_bar_canvas(frame,STRIDE,theme);
  struct xz_top_face f=xz_ui_top_button(c,buttons[i].x,buttons[i].w,buttons[i].name,lit);
  uint16_t ink=to565(f.ink);int glyph=0;
  for(int y=0;y<XZ_TOP_BAR_H;y++)for(int x=f.text_x;x<f.text_x+f.text_w;x++)glyph+=frame[y*STRIDE+x]==ink;
  /* The fill on the label's middle row, just left and right of the label. */
  int mid=(xz_theme_pixel_text(theme)?XZ_TOP_BAR_PIXEL_Y+3:XZ_TOP_BAR_LABEL_Y+4)*STRIDE,diff=255;
  for(int side=0;side<2;side++){
   int d=luma(ink)-luma(frame[mid+(side?f.text_x+f.text_w+1:f.text_x-2)]);if(d<0)d=-d;if(d<diff)diff=d;
  }
  if(glyph<15||diff<48){
   fprintf(stderr,"FAIL %s lit %d theme %d: %d label pixels, brightness difference %d\n",buttons[i].name,lit,theme,glyph,diff);assert(0);
  }
 }
 puts("PASS every top-bar label reads against its fill, off and on, in all 25 themes");
}

/* Touch: the depths top_bar.h gives, and nothing over stock's DECK 1 select rect {0,18,115,140}. */
static struct xz_ui_action last;static int actions;
struct solver { uint32_t owner; struct xz_touch_status previous; int calls; };
static void apply(void *ctx,const struct xz_ui_action *a,size_t n){(void)ctx;if(n){last=a[0];actions++;}}
static void stock(void *ctx,const struct xz_touch_status *s,const void *m){(void)m;struct solver *f=ctx;f->calls++;f->previous=*s;}
/* A fresh tap: returns the action kind it raised, XZ_UI_NONE when stock got it, -2 when it opened MODS. */
static int tap(struct xz_native_touch *t,struct solver *f,int x,int y){
 struct xz_touch_status s={1,{0,0,0},(uint32_t)x,(uint32_t)y};int before=actions,calls=f->calls,opened;
 xz_native_touch_dispatch(t,f,&s,NULL,stock);opened=t->visible;
 int kind=actions>before?(int)last.kind:opened?-2:XZ_UI_NONE;
 if(kind==XZ_UI_NONE)assert(f->calls==calls+1);else assert(f->calls==calls);
 s.down=0;xz_native_touch_dispatch(t,f,&s,NULL,stock);
 if(opened)xz_native_touch_visible(t,0);
 return kind;
}
static void touch_targets(void){
 struct xz_ui ui;struct xz_ui_model model;memset(&model,0,sizeof model);struct xz_native_touch t;struct solver f={0};
 xz_ui_init(&ui);xz_native_touch_init(&t,&ui,&model,apply,NULL);
 model.connection.enabled=model.connection.connected=1;model.metrics_enabled=1;
 assert(t.reach==XZ_TOP_BAR_TOUCH_H&&t.vj_btn_h==XZ_TOP_BAR_H);
 for(int deep=0;deep<2;deep++){
  t.reach=deep?XZ_TOP_BAR_DEEP_H:XZ_TOP_BAR_TOUCH_H;int reach=t.reach;
  /* VJ.TOOLS: rows 0..17 only, whatever the reach. */
  assert(tap(&t,&f,0,0)==XZ_UI_TAKEOVER_TOGGLE&&tap(&t,&f,XZ_TOP_VJ_W-1,XZ_TOP_BAR_H-1)==XZ_UI_TAKEOVER_TOGGLE);
  assert(tap(&t,&f,50,XZ_TOP_BAR_H)==XZ_UI_NONE&&tap(&t,&f,XZ_TOP_VJ_W,5)==XZ_UI_NONE);
  /* STEMS and MODS: down to the reach. */
  assert(tap(&t,&f,XZ_TOP_STEMS_X,0)==XZ_UI_STEMS_OVERLAY&&tap(&t,&f,XZ_TOP_STEMS_X+XZ_TOP_STEMS_W-1,reach-1)==XZ_UI_STEMS_OVERLAY);
  assert(tap(&t,&f,XZ_TOP_STEMS_X+20,reach)==XZ_UI_NONE);
  assert(tap(&t,&f,XZ_TOP_MODS_X,0)==-2&&tap(&t,&f,799,reach-1)==-2&&tap(&t,&f,770,reach)==XZ_UI_NONE);
  /* The two-pixel gap between STEMS and MODS stays stock's. */
  assert(tap(&t,&f,XZ_TOP_STEMS_X+XZ_TOP_STEMS_W,5)==XZ_UI_NONE&&tap(&t,&f,XZ_TOP_MODS_X-1,5)==XZ_UI_NONE);
  /* METRICS and the readout: rows 0..23 whatever the reach. */
  assert(tap(&t,&f,XZ_METRICS_X,XZ_TOP_BAR_TOUCH_H-1)==XZ_UI_METRICS_SETUP_OPEN&&tap(&t,&f,XZ_METRICS_X,XZ_TOP_BAR_TOUCH_H)==XZ_UI_NONE);
  assert(tap(&t,&f,XZ_METRICS_READOUT_X,0)==XZ_UI_METRICS_SETUP_OPEN&&tap(&t,&f,300,XZ_TOP_BAR_TOUCH_H)==XZ_UI_NONE);
  /* Stock DECK 1 select keeps every one of its points. */
  for(int y=18;y<18+140;y+=3)for(int x=0;x<115;x+=7)assert(tap(&t,&f,x,y)==XZ_UI_NONE);
 }
 /* VJ.TOOLS hidden (no link): its columns are stock's. */
 model.connection.connected=0;assert(tap(&t,&f,50,5)==XZ_UI_NONE);
 /* The readout answers only while it shows. */
 model.metrics_enabled=0;assert(tap(&t,&f,300,5)==XZ_UI_NONE);
 puts("PASS touch: VJ.TOOLS rows 0..17, STEMS and MODS to their reach, METRICS 0..23, stock DECK 1 select untouched");
}

int main(void){
 paint_band();row_geometry();label_contrast();touch_targets();
 return 0;
}
