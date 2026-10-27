#include "../key/notation.h"
/* Waveforms, playhead extrapolation and key text, shared by every screen so a
   deck reads the same on the play screen, the status strips and the lists.

   Waveforms write RGB565 spans straight into the canvas: one palette lookup per
   colour mode, no division per pixel. Lanes sample the track by absolute beat
   pixel, so an audio column lands on the same
   pixel phase every frame while the lane scrolls and nothing shimmers. */
#include "shell_internal.h"
#include "shell_axis.h"
#include "../ui/deck_colour.h"
#include <stdio.h>
#include <string.h>

#define C565(c) ((uint16_t)((((c)>>8)&0xf800)|(((c)>>5)&0x7e0)|(((c)>>3)&31)))
static inline uint16_t c565(uint32_t c) { return C565(c); }

/* Front to back: high over mid over low, as the flagship's 3BAND draws. The played part of an overview greys out. */
static _Thread_local uint16_t band_ink[2][3]={{C565(SH_WAVE_LOW),C565(SH_WAVE_MID),C565(SH_WAVE_HIGH)},{C565(0x343434),C565(0x5c5c5c),C565(0x8c8c8c)}};
static _Thread_local uint32_t band_rgb[3]={SH_WAVE_LOW,SH_WAVE_MID,SH_WAVE_HIGH};
static _Thread_local uint32_t preview_rgb[3]={SH_WAVE_LOW,SH_WAVE_MID,SH_WHITE};
void sh_wave_colors(const struct xz_shell_model *m){
 static const uint32_t standard[3]={SH_WAVE_LOW,SH_WAVE_MID,SH_WAVE_HIGH};
 for(int i=0;i<3;i++){band_rgb[i]=m->wave_band_rgb[i]?m->wave_band_rgb[i]:standard[i];band_ink[0][i]=c565(band_rgb[i]);preview_rgb[i]=m->wave_band_rgb[i]?m->wave_band_rgb[i]:i==2?SH_WHITE:standard[i];}
}
const uint32_t *sh_wave_band_rgb(void){return preview_rgb;}
enum { SHADES=16 };
/* BLUE: one blue per column, whitening with the share of highs. */
static uint16_t blue_shade(int high, int peak) {
 static const uint32_t from=0x0a46c8,to=0xc8e4ff;
 int t=peak>0?high*(SHADES-1)/peak:0;
 return c565(xz_blend(from,to,(unsigned)(t*256/SHADES)));
}
/* 65536/v, so RGB scales a column to full brightness without dividing. */
static uint32_t reciprocal(int v) {
 static uint32_t table[256];
 if(!table[1])for(int i=1;i<256;i++)table[i]=65536u/(uint32_t)i;
 return table[v<1?1:v>255?255:v];
}
/* RGB: lows red, mids green, highs blue, mixed into one colour per column. */
static uint16_t rgb_mix(int low, int mid, int high, int peak) {
 /* Most of the floor shared by all three bands comes off first, or busy
    columns would wash out to white. */
 int floor=sh_min(low,sh_min(mid,high))*5/8;
 low-=floor;mid-=floor;high-=floor;peak-=floor;
 uint32_t k=reciprocal(peak)*255u;
 uint32_t r=(uint32_t)low*k>>16,g=(uint32_t)mid*k>>16,b=(uint32_t)high*k>>16;
 return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}

struct span_canvas { uint16_t *p; size_t stride; int x0, y0, x1, y1; };
static int clip(struct xz_canvas c, int x, int y, int w, int h, struct span_canvas *s) {
 if(!c.p)return 0;
 s->p=c.p;s->stride=c.stride;
 s->x0=sh_max(0,x);s->y0=sh_max(0,y);s->x1=sh_min(c.width,x+w);s->y1=sh_min(c.height,y+h);
 return s->x1>s->x0&&s->y1>s->y0;
}
static void vspan(const struct span_canvas *s, int x, int y0, int y1, uint16_t ink) {
 if(y0<s->y0)y0=s->y0;
 if(y1>s->y1)y1=s->y1;
 uint16_t *q=s->p+(size_t)y0*s->stride+(size_t)x;
 for(int y=y0;y<y1;y++,q+=s->stride)*q=ink;
}
static void band_max(const struct xz_shell_wave *w, uint32_t from, uint32_t to, int out[3]) {
 if(to<=from)to=from+1;
 if(to>w->count)to=w->count;
 int low=0,mid=0,high=0;
 for(const struct xz_shell_column *q=w->columns+from,*end=w->columns+to;q<end;q++){
  if(q->low>low)low=q->low;
  if(q->mid>mid)mid=q->mid;
  if(q->high>high)high=q->high;
 }
 out[0]=low;out[1]=mid;out[2]=high;
}
static void scale_table(int16_t table[256], int size) {
 for(int v=0;v<256;v++)table[v]=(int16_t)(v*size/255);
}

/* 65536/sum for the sum of three stems, 1..765. */
static uint32_t stem_reciprocal(int sum) {
 static uint32_t table[766];
 if(!table[1])for(int i=1;i<766;i++)table[i]=65536u/(uint32_t)i;
 return table[sum<1?1:sum>765?765:sum];
}
/* Mirrored about cy, size px each way at full scale. */
static void column_centered(const struct span_canvas *s, int x, int cy, const int16_t *scale, const int band[3], enum xz_shell_wave_color mode) {
 if(mode==XZ_SHELL_WAVE_3BAND){
  int sum=band[0]+band[1]+band[2];
  if(!sum)return;
  int outer=scale[sh_max(band[0],sh_max(band[1],band[2]))];
  uint32_t k=stem_reciprocal(sum)*(uint32_t)outer;
  int lo=(int)((uint32_t)band[2]*k>>16),mid=(int)((uint32_t)(band[1]+band[2])*k>>16);
  const uint16_t *ink=band_ink[0];
  /* Paint back to front as nested spans: each pixel written once. */
  if(outer>mid){vspan(s,x,cy-outer,cy-mid,ink[0]);vspan(s,x,cy+mid+1,cy+outer+1,ink[0]);}
  if(mid>lo){vspan(s,x,cy-mid,cy-lo,ink[1]);vspan(s,x,cy+lo+1,cy+mid+1,ink[1]);}
  /* The core: the highs, or on a column without them the front-most band that reaches it. */
  vspan(s,x,cy-lo,cy+lo+1,ink[lo>0?2:mid>0?1:outer>0?0:band[2]?2:band[1]?1:0]);
  return;
 }
 int peak=sh_max(band[0],sh_max(band[1],band[2]));
 if(!peak)return;
 int h=scale[peak];
 uint16_t ink=mode==XZ_SHELL_WAVE_RGB?rgb_mix(band[0],band[1],band[2],peak):blue_shade(band[2],peak);
 vspan(s,x,cy-h,cy+h+1,ink);
}
/* Rising from the baseline (bottom), height px at full scale. */
static void column_bottom(const struct span_canvas *s, int x, int base, const int16_t *scale, const int band[3], enum xz_shell_wave_color mode, int played) {
 if(mode==XZ_SHELL_WAVE_3BAND){
  int sum=band[0]+band[1]+band[2];
  if(!sum)return;
  int outer=scale[sh_max(band[0],sh_max(band[1],band[2]))];
  uint32_t r=stem_reciprocal(sum)*(uint32_t)outer;
  int h2=(int)((uint32_t)band[2]*r>>16),mid=(int)((uint32_t)(band[1]+band[2])*r>>16);
  const uint16_t *k=band_ink[played?1:0];
  if(outer>mid)vspan(s,x,base-outer,base-mid,k[0]);
  if(mid>h2)vspan(s,x,base-mid,base-h2,k[1]);
  if(h2>0)vspan(s,x,base-h2,base,k[2]);
  return;
 }
 int peak=sh_max(band[0],sh_max(band[1],band[2]));
 if(!peak)return;
 uint16_t ink=played?band_ink[1][1]:mode==XZ_SHELL_WAVE_RGB?rgb_mix(band[0],band[1],band[2],peak):blue_shade(band[2],peak);
 vspan(s,x,base-scale[peak],base,ink);
}

/* Columns per track pixel in 16.16, and the columns pixel p shows: the ones whose start falls
   within half a pixel of p's centre, so a sound lands on the pixel its time rounds to, the same
   rounding the playhead and every marker use. */
struct lane_sampler {
 uint64_t centre,step,half; unsigned shift;
 struct sh_wave_axis axis; const struct xz_shell_wave *wave;
 int64_t pixel,until; int segmented,fresh;
};
static uint64_t sample_fixed(double value){return value>=(double)UINT64_MAX?UINT64_MAX:(uint64_t)value;}
/* One setup per visible segment, then integer additions per pixel. Inverse
   sampling uses half-pixel edges, so a peak and its source-time marker share
   the same rounding, including the pixel straddling a tempo-grid transition. */
static void lane_segment(struct lane_sampler *s,double edge){
 struct sh_wave_axis a=s->axis;
 double ms,step;
 s->until=INT64_MAX;
 if(a.grid){
  int ppb=a.pps/2;
  int64_t ordinal=sh_axis_floor((int64_t)(edge<0?edge-1:edge),ppb);
  uint32_t i=a.regular?a.lo:ordinal<(int64_t)a.lo?a.lo:ordinal>=(int64_t)a.hi?a.hi-1:(uint32_t)ordinal;
  int64_t span=a.regular?a.regular:(int64_t)a.grid[i+1].ms-a.grid[i].ms;
  step=(double)span/ppb;ms=a.grid[i].ms+(edge-(double)i*ppb)*step;
  if(!a.regular&&i<a.hi-1)s->until=(int64_t)(i+1)*ppb;
 }else{
  step=a.bpm?12000000./((double)a.pps*a.bpm):1000.*a.rate/((double)a.pps*10000.);
  ms=edge*step;
 }
 double columns=(double)s->wave->count/s->wave->span_ms*4294967296.;
 s->centre=ms>0?sample_fixed(ms*columns):0;
 s->step=sample_fixed(step*columns);s->shift=32;
}
static struct lane_sampler lane_sampler(const struct xz_shell_wave *w,struct sh_wave_axis axis,int64_t first){
 struct lane_sampler s={0};
 int identity=axis.grid?axis.regular==500&&axis.grid[axis.lo].ms==(int64_t)axis.lo*500:!axis.bpm||axis.bpm==12000;
 if(!identity){
  s.axis=axis;s.wave=w;s.pixel=first;s.segmented=1;s.fresh=1;
  lane_segment(&s,(double)first-.5);return s;
 }
 if(first<0)first=0;
 if(axis.rate==10000){
  uint64_t cpp=((uint64_t)w->count*1000u<<16)/((uint64_t)w->span_ms*(uint64_t)axis.pps);
  s.step=cpp?cpp:1;s.shift=16;s.centre=(uint64_t)first*s.step;
 }else{
  /* One division per lane.32 fractional bits retain sparse data at very slow
     positive tempo; anchor before quantizing the step so long source offsets
     do not accumulate step-rounding error. No floating work in the pixel loop. */
  double step=(double)w->count*1000.*4294967296.*axis.rate/((double)w->span_ms*axis.pps*10000.);
  s.step=sample_fixed(step);s.shift=32;s.centre=sample_fixed((double)first*step);
 }
 s.half=s.step/2;return s;
}
static void lane_range(struct lane_sampler *s,uint64_t *from,uint64_t *to){
 if(s->segmented){
  *from=s->centre>>32;
  if(s->fresh||(double)s->pixel+.5>=s->until){lane_segment(s,(double)s->pixel+.5);s->fresh=0;}
  else s->centre=UINT64_MAX-s->centre<s->step?UINT64_MAX:s->centre+s->step;
  *to=s->centre>>32;s->pixel++;return;
 }
 *from=(s->centre>s->half?s->centre-s->half:0)>>s->shift;
 *to=(UINT64_MAX-s->centre<s->half?UINT64_MAX:s->centre+s->half)>>s->shift;
 s->centre=UINT64_MAX-s->centre<s->step?UINT64_MAX:s->centre+s->step;
}
static void wave_lane_axis(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int64_t first_px, struct sh_wave_axis axis) {
 struct span_canvas s;
 if(!w->columns||!w->count||!w->span_ms||axis.pps<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 int16_t scale[256];scale_table(scale,amp);
 int cy=r.y+r.h/2;
 int64_t source_start=sh_axis_px(axis,0),start=first_px+s.x0-r.x;
 if(start<source_start)start=source_start;
 struct lane_sampler sampler=lane_sampler(w,axis,start);
 for(int x=s.x0;x<s.x1;x++){
  int64_t p=first_px+(x-r.x);
  if(p<source_start)continue;
  uint64_t from,to;lane_range(&sampler,&from,&to);
  if(from>=w->count)break;
  int band[3];band_max(w,(uint32_t)from,(uint32_t)to,band);
  column_centered(&s,x,cy,scale,band,mode);
 }
}
void sh_wave_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int64_t first_px, int pps) {
 wave_lane_axis(c,r,amp,w,mode,first_px,(struct sh_wave_axis){.pps=pps,.rate=10000});
}
void sh_wave_overview(struct xz_canvas c, struct xz_shell_rect r, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int played_x) {
 struct span_canvas s;
 if(!w->columns||!w->count||r.w<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 int16_t scale[256];scale_table(scale,r.h);
 uint64_t cpp=((uint64_t)w->count<<16)/(uint64_t)r.w;
 if(!cpp)cpp=1;
 for(int x=s.x0;x<s.x1;x++){
  uint64_t from=((uint64_t)(x-r.x)*cpp)>>16,to=((uint64_t)(x-r.x+1)*cpp)>>16;
  if(from>=w->count)break;
  int band[3];band_max(w,(uint32_t)from,(uint32_t)to,band);
  column_bottom(&s,x,r.y+r.h,scale,band,mode,x<played_x);
 }
}

/* ---- 3-stem layer ----
   Stacked as the stems mod draws it: drums outermost, harmonics, vocal at the core. The column is as
   tall as its loudest stem and each layer as thick as that stem's share. Each layer reads like its stem
   button: a darker interior colour inside, a bright outline along its outer edge. The outline is the
   layer's edge row plus the climb from the previous column's edge, so it runs unbroken as the shape
   rises and falls, at a few pixels per column. A muted stem is a dim grey with no outline. */
struct stem_ink { uint16_t fill[3], stroke[3]; int outline[3]; };
static void stem_inks(const struct xz_shell_model *m, const struct xz_shell_deck *d, int played, struct stem_ink *k) {
 static const uint32_t played_rgb[3]={0x343434,0x5c5c5c,0x8c8c8c};
 for(int stem=0;stem<3;stem++){
  uint32_t bright=sh_stem_bright(m,stem);
  int muted=sh_has(d,XZ_SHELL_F_STEM_MUTE)&&((d->stem_muted>>stem)&1);
  if(played){k->fill[stem]=k->stroke[stem]=c565(played_rgb[stem]);k->outline[stem]=0;}
  else if(muted){k->fill[stem]=k->stroke[stem]=c565(0x2a2a2a);k->outline[stem]=0;}
  else{k->fill[stem]=c565(sh_stem_wave_fill(bright));k->stroke[stem]=c565(bright);k->outline[stem]=1;}
 }
}
/* The loudest source column of [from,to). A narrow transient must survive
   beat-normalized low zoom just as it does in the original waveform. */
static const struct xz_shell_column *stem_pick(const struct xz_shell_wave *w, uint32_t from, uint32_t to, int detail) {
 if(to<=from)to=from+1;
 if(to>w->count)to=w->count;
 uint32_t step=!detail&&to-from>16?(to-from)/16:1;
 const struct xz_shell_column *best=&w->columns[from];int loudest=-1;
 for(uint32_t i=from;i<to;i+=step){
  const struct xz_shell_column *q=&w->columns[i];
  int sum=q->low+q->mid+q->high;
  if(sum>loudest){loudest=sum;best=q;}
 }
 return best;
}
/* Layer boundaries, outward from the centre: vocal to core, harmonics to inner, drums to total. */
static void stem_heights(const struct xz_shell_column *q, const int16_t *scale, int *core, int *inner, int *total) {
 int sum=q->low+q->mid+q->high;
 *total=scale[sh_max(q->low,sh_max(q->mid,q->high))];
 uint32_t k=stem_reciprocal(sum)*(uint32_t)*total;
 *core=sum?(int)(q->high*k>>16):0;
 *inner=sum?(int)((q->mid+q->high)*k>>16):0;
}
/* One mirrored column: three fill spans each side, then each layer's outline from its edge to the
   previous column's edge (prev[layer], -1 when that layer was absent there). */
static void stem_column_centered(const struct span_canvas *s, int x, int cy, int core, int inner, int total, const struct stem_ink *k, int prev[3]) {
 int edge=sh_min(cy-s->y0,s->y1-1-cy);
 if(total>edge)total=edge;
 if(inner>total)inner=total;
 if(core>inner)core=inner;
 const int bound[4]={total,inner,core,0};
 for(int layer=0;layer<3;layer++){
  int outer=bound[layer],in=bound[layer+1];
  if(outer<=in){prev[layer]=-1;continue;}
  vspan(s,x,cy-outer,cy-in,k->fill[layer]);
  vspan(s,x,cy+in+1,cy+outer+1,k->fill[layer]);
 }
 for(int layer=0;layer<3;layer++){
  int outer=bound[layer],in=bound[layer+1];
  if(outer<=in)continue;
  int was=prev[layer];prev[layer]=outer;
  if(!k->outline[layer])continue;
  int lo=outer-1,hi=outer;
  if(was>=0){lo=sh_min(lo,was);hi=sh_max(hi,was);}
  lo=sh_max(lo,in+1);hi=sh_min(hi,edge);
  if(hi<lo)continue;
  vspan(s,x,cy-hi,cy-lo+1,k->stroke[layer]);
  vspan(s,x,cy+lo,cy+hi+1,k->stroke[layer]);
 }
}
static void stem_lane(const struct xz_shell_model *m, const struct xz_shell_deck *d, struct xz_canvas c, struct xz_shell_rect r, int amp, int64_t first_px, struct sh_wave_axis axis) {
 const struct xz_shell_wave *w=&d->stems;
 struct span_canvas s;
 if(axis.pps<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 struct stem_ink k;stem_inks(m,d,0,&k);
 int16_t scale[256];scale_table(scale,amp);
 int cy=r.y+r.h/2,prev[3]={-1,-1,-1};
 int64_t source_start=sh_axis_px(axis,0),start=first_px+s.x0-r.x;
 if(start<source_start)start=source_start;
 struct lane_sampler sampler=lane_sampler(w,axis,start);
 for(int x=s.x0;x<s.x1;x++){
  int64_t p=first_px+(x-r.x);
  if(p<source_start)continue;
  uint64_t from,to;lane_range(&sampler,&from,&to);
  if(from>=w->count)break;
  const struct xz_shell_column *q=stem_pick(w,(uint32_t)from,(uint32_t)to,1);
  if(!q->low&&!q->mid&&!q->high){prev[0]=prev[1]=prev[2]=-1;continue;}
  int core,inner,total;stem_heights(q,scale,&core,&inner,&total);
  stem_column_centered(&s,x,cy,core,inner,total,&k,prev);
  /* The centre row takes the innermost stem that sounds. */
  int centre=q->high?2:q->mid?1:0;
  vspan(&s,x,cy,cy+1,k.outline[centre]&&total<=1?k.stroke[centre]:k.fill[centre]);
 }
}
/* The overview rises from the bottom: three fill spans and a one-pixel outline on each layer's top. */
static void stem_overview(const struct xz_shell_model *m, const struct xz_shell_deck *d, struct xz_canvas c, struct xz_shell_rect r, int played_x) {
 const struct xz_shell_wave *w=&d->stems;
 struct span_canvas s;
 if(r.w<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 struct stem_ink k[2];stem_inks(m,d,0,&k[0]);stem_inks(m,d,1,&k[1]);
 int16_t scale[256];scale_table(scale,r.h);
 uint64_t cpp=((uint64_t)w->count<<16)/(uint64_t)r.w;
 if(!cpp)cpp=1;
 int base=r.y+r.h,prev[3]={-1,-1,-1};
 for(int x=s.x0;x<s.x1;x++){
  uint64_t from=((uint64_t)(x-r.x)*cpp)>>16,to=((uint64_t)(x-r.x+1)*cpp)>>16;
  if(from>=w->count)break;
  const struct xz_shell_column *q=stem_pick(w,(uint32_t)from,(uint32_t)to,0);
  int core,inner,total;stem_heights(q,scale,&core,&inner,&total);
  const struct stem_ink *ink=&k[x<played_x];
  const int bound[4]={total,inner,core,0};
  for(int layer=0;layer<3;layer++){
   int outer=bound[layer],in=bound[layer+1];
   if(outer<=in){prev[layer]=-1;continue;}
   vspan(&s,x,base-outer,base-in,ink->fill[layer]);
   int was=prev[layer];prev[layer]=outer;
   if(!ink->outline[layer])continue;
   int lo=outer,hi=outer;
   if(was>=0){lo=sh_min(lo,was);hi=sh_max(hi,was);}
   lo=sh_max(lo,in+1);
   if(hi>=lo)vspan(&s,x,base-hi,base-lo+1,ink->stroke[layer]);
  }
 }
}

/* ---- a deck's waveforms in the chosen colour ---- */
int sh_stem_wave_ready(const struct xz_shell_deck *d) {
 return sh_has(d,XZ_SHELL_F_STEM_WAVE)&&d->stems.columns&&d->stems.count&&d->stems.span_ms;
}
void sh_deck_lane_axis(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int64_t first_px, struct sh_wave_axis axis) {
 enum xz_shell_wave_color mode=sh_deck_wave_color(u,d);
 if(mode==XZ_SHELL_WAVE_STEMS){
  if(sh_stem_wave_ready(d)){stem_lane(m,d,c,r,amp,first_px,axis);return;}
  mode=XZ_SHELL_WAVE_3BAND;
 }
 if(sh_has(d,XZ_SHELL_F_WAVE))wave_lane_axis(c,r,amp,&d->wave,mode,first_px,axis);
}
void sh_deck_overview(struct xz_canvas c, struct xz_shell_rect r, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int played_x) {
 enum xz_shell_wave_color mode=sh_deck_wave_color(u,d);
 if(mode==XZ_SHELL_WAVE_STEMS){
  if(sh_stem_wave_ready(d)){stem_overview(m,d,c,r,played_x);return;}
  mode=XZ_SHELL_WAVE_3BAND;
 }
 if(sh_has(d,XZ_SHELL_F_OVERVIEW))sh_wave_overview(c,r,&d->overview,mode,played_x);
}

void sh_deck_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int64_t first_px, int pps) {
 sh_deck_lane_axis(c,r,amp,u,m,d,first_px,(struct sh_wave_axis){.pps=pps,.rate=10000});
}
int32_t xz_shell_elapsed_now(const struct xz_shell_model *m, int deck) {
 if(!m||deck<0||deck>=sh_deck_count(m))return -1;
 const struct xz_shell_deck *d=&m->deck[deck];
 if(!sh_has(d,XZ_SHELL_F_ELAPSED))return -1;
 int64_t e=d->elapsed_ms;
 int timeline=sh_has(d,XZ_SHELL_F_MOTION);
 int moving=timeline?d->moving:sh_has(d,XZ_SHELL_F_PLAYING)&&d->playing;
 int rate_known=sh_has(d,XZ_SHELL_F_PLAYBACK_RATE);
 if(moving&&d->elapsed_at_ms&&(!timeline||rate_known)){
  uint32_t dt=m->clock_ms-d->elapsed_at_ms;
  if(dt<=0x7fffffffu){
   uint32_t limit=(timeline||rate_known)?XZ_SHELL_TIMELINE_EXTRAPOLATE_MS:XZ_SHELL_EXTRAPOLATE_MAX_MS;
   if(dt>limit)dt=limit;
   int64_t rate=rate_known?d->playback_rate_ppm:1000000+(int64_t)(sh_has(d,XZ_SHELL_F_TEMPO)?d->tempo_x100:0)*100;
   if(!rate_known&&rate<0)rate=0;
   int64_t advanced=e+(int64_t)dt*rate/1000000;
   const struct xz_shell_loop *l=&d->loop;
   if(sh_has(d,XZ_SHELL_F_LOOP)&&l->active&&l->out_ms>l->in_ms&&e>=l->in_ms&&e<l->out_ms&&
      (advanced>=l->out_ms||advanced<l->in_ms)){
    int64_t span=(int64_t)l->out_ms-l->in_ms,offset=(advanced-l->in_ms)%span;
    advanced=l->in_ms+(offset<0?offset+span:offset);
   }
   e=advanced;
  }
 }
 if(e<0)e=0;
 if(sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0&&e>d->total_ms)e=d->total_ms;
 return (int32_t)e;
}

/* ---- four-deck status strip ----
   Under the non-play screens of the four-deck view each deck gets a quarter: the player badge, REMAIN
   or TIME with the time, a mini overview under them, and the BPM box (orange digits and MASTER for
   the master deck). Numerals use the text face; a quarter has no room for the numeral faces. */
enum { Q_BADGE_W=48, Q_TEXT_X=58, Q_BPM_W=52 };
struct xz_shell_rect sh_strip_cell(const struct xz_shell_model *m, struct xz_shell_rect band, int deck) {
 int count=m?sh_deck_count(m):2,w=band.w/count;
 return (struct xz_shell_rect){band.x+sh_strip_column(count,deck)*w,band.y,w,band.h};
}
static void quarter(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect q, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int seam=sh_strip_column(4,deck)?1:0;
 int x=q.x+seam,w=q.w-seam-1,y=q.y+1,h=q.h-1;
 char text[24];
 xz_rect(c,x,y,w,h,SH_CHROME);
 int bx=x+4,by=y+3,bh=h-6;
 int control=sh_control_active(m,deck);
 uint32_t accent=sh_deck_accent(u,d,SH_BLUE),tile=control?accent:SH_RAIL,badge_ink=sh_ink_on(tile);
 xz_rect(c,bx,by,Q_BADGE_W,bh,tile);
 /* The deck's number in its badge (deck_colour.h), as on the four-deck play strips; under it, the side's
    fill, arrow and USB stick stripe say who drives the deck and where its track came from. */
 xz_rect(c,bx,by,Q_BADGE_W,22,xz_deck_box_fill(deck,1,tile));xz_rect(c,bx,by+22,Q_BADGE_W,1,SH_BLACK);
 if(!control&&accent!=SH_BLUE)xz_rect(c,bx,by+23,3,bh-23,accent);
 snprintf(text,sizeof text,"%d",deck+1);
 sh_num(c,bx+(Q_BADGE_W-sh_num_width(SH_NUM_MID,text))/2,by+1,text,SH_NUM_MID,xz_deck_box_ink(deck,1,tile));
 if(control){
  sh_control_arrow(c,bx+(Q_BADGE_W-27)/2,by+bh-20,deck&1);
  sh_text_center(c,bx+Q_BADGE_W/2,by+bh-10,"CONTROL",SH_SMALL,badge_ink);
 }
 if(u->deck==deck)xz_border(c,bx,by,Q_BADGE_W,bh,SH_WHITE);
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 if(loaded_known&&!loaded){sh_text_center(c,(x+Q_TEXT_X+x+w-4)/2,y+(h-SH_CAP_TEXT)/2-3,"Not Loaded.",SH_TEXT,SH_RED);return;}
 /* REMAIN / TIME and the time on one line */
 int mode_known=1;enum xz_shell_time_mode mode=XZ_SHELL_TIME_REMAIN;
 if(sh_has(d,XZ_SHELL_F_TIME_MODE))mode=d->time_mode;
 else if(sh_setting(m,XZ_SHELL_SET_TIME_MODE))mode=m->settings.time_mode;
 else mode_known=0;
 const char *caption=mode==XZ_SHELL_TIME_ELAPSED?"TIME":"REMAIN";
 int tx=x+Q_TEXT_X;
 sh_text(c,tx,y+9,caption,SH_SMALL,mode_known?SH_WHITE:SH_DIM);
 int32_t e=loaded?xz_shell_elapsed_now(m,deck):-1,shown=-1;
 int timed=e>=0&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0;
 if(mode==XZ_SHELL_TIME_ELAPSED&&e>=0)shown=e;
 else if(mode==XZ_SHELL_TIME_REMAIN&&timed)shown=sh_max(0,d->total_ms-e);
 if(shown>=0){
  int32_t s=shown/1000,min=s/60,sec=s%60;
  if(min>99){min=99;sec=59;}
  snprintf(text,sizeof text,"%02d:%02d",(int)min,(int)sec);
 }
 else snprintf(text,sizeof text,"--:--");
 sh_text(c,tx+sh_text_width(c,"REMAIN",SH_SMALL)+3,y+4,text,SH_TEXT,shown>=0?SH_WHITE:SH_DIM);
 /* BPM box at the right edge, the mini overview under the time */
 int px=x+w-4-Q_BPM_W,master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 uint32_t ink=master?SH_RED:SH_WHITE;
 xz_border(c,px,by,Q_BPM_W,bh,master?SH_RED:SH_WHITE);
 sh_text(c,px+4,by+3,master?"MASTER":"BPM",SH_SMALL,ink);
 if(loaded&&sh_has(d,XZ_SHELL_F_BPM)){
  int32_t b=sh_max(0,sh_min(d->bpm_x100,99999));
  snprintf(text,sizeof text,"%d.%d",(int)(b/100),(int)((b/10)%10));
  sh_text_right(c,px+Q_BPM_W-4,by+bh-18,text,SH_TEXT,ink);
 }else sh_text_right(c,px+Q_BPM_W-4,by+bh-18,"---.-",SH_TEXT,SH_DIM);
 struct xz_shell_rect ov={tx,y+24,px-6-tx,h-28};
 int ph=timed?ov.x+(int)((int64_t)sh_min(e,d->total_ms)*(ov.w-1)/d->total_ms):-1;
 xz_rect(c,ov.x,ov.y,ov.w,ov.h,SH_BLACK);
 if(loaded)sh_deck_overview(c,(struct xz_shell_rect){ov.x,ov.y,ov.w,ov.h-1},u,m,d,ph);
 xz_rect(c,ov.x,ov.y+ov.h-1,ov.w,1,SH_RAIL);
 if(ph>=0)xz_rect(c,ph,ov.y,1,ov.h,SH_PLAYHEAD);
}
int sh_deck_quarters(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect band) {
 if(sh_deck_count(m)!=4)return 0;
 xz_rect(c,band.x,band.y,band.w,band.h,SH_BLACK);
 for(int deck=0;deck<4;deck++)quarter(u,m,c,sh_strip_cell(m,band,deck),deck);
 return 1;
}

/* Key notation. Alphanumeric numbers step a fifth (7 semitones) per number;
   8A is A minor, 8B is C major. 7 is its own inverse modulo 12. */
/* Pitch class 0..11 and minor flag, or 0 when the text is neither notation. */
static int parse_key(const char *s, int *pc, int *minor) {
 if(!s)return 0;
 if(s[0]>='1'&&s[0]<='9'){
  int n=s[0]-'0',i=1;
  if(s[1]>='0'&&s[1]<='9'){n=n*10+s[1]-'0';i=2;}
  char l=s[i];
  if(n<1||n>12||(l!='A'&&l!='B'&&l!='a'&&l!='b')||s[i+1])return 0;
  *minor=l=='A'||l=='a';
  *pc=((n-1-(*minor?4:7)+24)%12)*7%12;
  return 1;
 }
 static const int letters[7]={9,11,0,2,4,5,7};
 if(s[0]<'A'||s[0]>'G')return 0;
 int p=letters[s[0]-'A'],i=1;
 if(s[i]=='#'){p++;i++;}
 else if(s[i]=='b'){p--;i++;}
 else if(!strncmp(s+i,"\xe2\x99\xaf",3)){p++;i+=3;}
 else if(!strncmp(s+i,"\xe2\x99\xad",3)){p--;i+=3;}
 *minor=0;
 if(!strcmp(s+i,"m")||!strcmp(s+i,"min"))*minor=1;
 else if(s[i]&&strcmp(s+i,"maj"))return 0;
 *pc=(p+12)%12;
 return 1;
}
const char *sh_key_text(const char *key, enum xz_shell_key_display mode, char *buffer, size_t cap) {
 int pc,minor;
 if(!key||mode==XZ_SHELL_KEY_AS_DECK||!parse_key(key,&pc,&minor)||cap<4)return key;
 char formatted[8];xz_key_notation_format(pc,minor,mode==XZ_SHELL_KEY_ALPHANUMERIC,formatted);
 if(strlen(formatted)+1>cap)return key;
 memcpy(buffer,formatted,strlen(formatted)+1);return buffer;
}

/* ---- key traffic light ----
   On the key wheel (1A..12B) a key sits one number from its neighbours and
   shares its number with its relative. */
enum xz_shell_key_match xz_shell_key_match(const char *key, const char *master_key) {
 int pc,minor,mpc,mminor;
 if(!parse_key(key,&pc,&minor)||!parse_key(master_key,&mpc,&mminor))return XZ_SHELL_KEY_MATCH_UNKNOWN;
 int n=(pc*7+(minor?4:7))%12,mn=(mpc*7+(mminor?4:7))%12;
 if(n==mn)return XZ_SHELL_KEY_MATCH_GREEN;
 if(minor==mminor&&((n+1)%12==mn||(mn+1)%12==n))return XZ_SHELL_KEY_MATCH_AMBER;
 return XZ_SHELL_KEY_MATCH_NONE;
}
const char *xz_shell_master_key(const struct xz_shell_model *m) {
 if(!m)return NULL;
 for(int d=0;d<sh_deck_count(m);d++){
  const struct xz_shell_deck *k=&m->deck[d];
  if(sh_has(k,XZ_SHELL_F_MASTER)&&k->master&&sh_has(k,XZ_SHELL_F_KEY)&&k->key&&(!sh_has(k,XZ_SHELL_F_LOADED)||k->loaded))return k->key;
 }
 return NULL;
}
uint32_t sh_key_ink(const struct xz_shell_model *m, const char *key, int matches_master, uint32_t plain) {
 if(m&&m->mods_settings_known&&!m->browse_traffic)return plain;
 const char *master=xz_shell_master_key(m);
 if(!master)return matches_master?SH_GREEN:plain;
 switch(xz_shell_key_match(key,master)){
 case XZ_SHELL_KEY_MATCH_GREEN:return SH_KEY_GREEN;
 case XZ_SHELL_KEY_MATCH_AMBER:return SH_KEY_AMBER;
 default:return plain;
 }
}
