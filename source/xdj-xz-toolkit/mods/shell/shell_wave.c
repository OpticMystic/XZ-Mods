/* Waveforms, playhead extrapolation and key text, shared by every screen so a
   deck reads the same on the play screen, the status strips and the lists.

   Waveforms write RGB565 spans straight into the canvas: one palette lookup per
   colour mode, no division per pixel. Lanes sample the track by absolute track
   pixel (track_px = ms x pps / 1000), so an audio column lands on the same
   pixel phase every frame while the lane scrolls and nothing shimmers. */
#include "shell_internal.h"
#include <string.h>

#define C565(c) ((uint16_t)((((c)>>8)&0xf800)|(((c)>>5)&0x7e0)|(((c)>>3)&31)))
static inline uint16_t c565(uint32_t c) { return C565(c); }

/* Front to back: high over mid over low, as the flagship's 3BAND draws. The played part of an overview greys out. */
static const uint16_t band_ink[2][3]={{C565(SH_WAVE_LOW),C565(SH_WAVE_MID),C565(SH_WAVE_HIGH)},{C565(0x343434),C565(0x5c5c5c),C565(0x8c8c8c)}};
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

/* Mirrored about cy, size px each way at full scale. */
static void column_centered(const struct span_canvas *s, int x, int cy, const int16_t *scale, const int band[3], enum xz_shell_wave_color mode) {
 if(mode==XZ_SHELL_WAVE_3BAND){
  int h0=scale[band[0]],h1=scale[band[1]],h2=scale[band[2]];
  int outer=sh_max(h0,sh_max(h1,h2));
  const uint16_t *ink=band_ink[0];
  /* Paint back to front as nested spans: each pixel written once. */
  int lo=h2,mid=sh_max(h1,h2);
  if(!band[0]&&!band[1]&&!band[2])return;
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
  int h0=scale[band[0]],h1=scale[band[1]],h2=scale[band[2]];
  int outer=sh_max(h0,sh_max(h1,h2)),mid=sh_max(h1,h2);
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
static uint64_t lane_cpp(const struct xz_shell_wave *w, int pps) {
 uint64_t cpp=((uint64_t)w->count*1000u<<16)/((uint64_t)w->span_ms*(uint64_t)pps);
 return cpp?cpp:1;
}
static void lane_range(uint64_t cpp, int64_t p, uint64_t *from, uint64_t *to) {
 uint64_t centre=(uint64_t)p*cpp,half=cpp/2;
 *from=(centre>half?centre-half:0)>>16;*to=(centre+half)>>16;
}
void sh_wave_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int64_t first_px, int pps) {
 struct span_canvas s;
 if(!w->columns||!w->count||!w->span_ms||pps<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 int16_t scale[256];scale_table(scale,amp);
 int cy=r.y+r.h/2;
 uint64_t cpp=lane_cpp(w,pps);
 for(int x=s.x0;x<s.x1;x++){
  int64_t p=first_px+(x-r.x);
  if(p<0)continue;
  uint64_t from,to;lane_range(cpp,p,&from,&to);
  if(from>=w->count)break;
  int band[3];band_max(w,(uint32_t)from,(uint32_t)to,band);
  column_centered(&s,x,cy,scale,band,mode);
 }
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
   tall as its loudest stem and each layer as thick as that stem's share. A layer darkens a little
   toward its outer edge in four steps (to 84 %), so neighbours of similar brightness still part;
   a muted stem turns a dim grey. */
enum { STEM_STEPS=4 };
struct stem_ink { uint16_t ink[3][STEM_STEPS]; };
static const uint32_t stem_default_rgb[3]={0x007de1,0xff4619,0x28e028};   /* the FLAGSHIP theme stem colours */
static void stem_inks(const struct xz_shell_model *m, const struct xz_shell_deck *d, int played, struct stem_ink *k) {
 static const uint32_t played_rgb[3]={0x343434,0x5c5c5c,0x8c8c8c};
 for(int stem=0;stem<3;stem++){
  uint32_t base=played?played_rgb[stem]:m->stem_rgb_known?m->stem_rgb[stem]&0xffffff:stem_default_rgb[stem];
  if(!played&&sh_has(d,XZ_SHELL_F_STEM_MUTE)&&((d->stem_muted>>stem)&1)){
   unsigned grey=(((base>>16)&255)+((base>>8)&255)+(base&255))/3;
   base=xz_blend(0,grey*0x010101u,110);
  }
  for(int step=0;step<STEM_STEPS;step++)k->ink[stem][step]=c565(xz_blend(base,0,(unsigned)(step*14)));
 }
}
/* 65536/sum for the sum of three stems, 1..765. */
static uint32_t stem_reciprocal(int sum) {
 static uint32_t table[766];
 if(!table[1])for(int i=1;i<766;i++)table[i]=65536u/(uint32_t)i;
 return table[sum<1?1:sum>765?765:sum];
}
/* The loudest column of [from,to); a long range is sampled at 16 evenly spaced columns. */
static const struct xz_shell_column *stem_pick(const struct xz_shell_wave *w, uint32_t from, uint32_t to) {
 if(to<=from)to=from+1;
 if(to>w->count)to=w->count;
 uint32_t step=to-from>16?(to-from)/16:1;
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
/* One mirrored column in a single pass from the outer edge in: twelve bands (three layers, four
   shades each, darker outward), each pixel written once, no clipping inside the loop. */
static void stem_column_centered(const struct span_canvas *s, int x, int cy, int core, int inner, int total, const struct stem_ink *k) {
 int edge=sh_min(cy-s->y0,s->y1-1-cy);
 if(total>edge)total=edge;
 if(inner>total)inner=total;
 if(core>inner)core=inner;
 /* from[i]: the lowest offset of band i, outermost band first. */
 int from[3*STEM_STEPS];const uint16_t *ink[3*STEM_STEPS];int n=0;
 const int bounds[4]={total,inner,core,0};
 for(int layer=0;layer<3;layer++){
  int b=bounds[layer],a=bounds[layer+1];
  for(int step=STEM_STEPS-1;step>=0;step--){from[n]=a+((b-a)*step>>2)+1;ink[n]=&k->ink[layer][step];n++;}
 }
 uint16_t *top=s->p+(size_t)(cy-total)*s->stride+(size_t)x,*bottom=s->p+(size_t)(cy+total)*s->stride+(size_t)x;
 int band=0;
 for(int d=total;d>=1;d--){
  while(band<n-1&&d<from[band])band++;
  *top=*bottom=*ink[band];
  top+=s->stride;bottom-=s->stride;
 }
}
static void stem_lane(const struct xz_shell_model *m, const struct xz_shell_deck *d, struct xz_canvas c, struct xz_shell_rect r, int amp, int64_t first_px, int pps) {
 const struct xz_shell_wave *w=&d->stems;
 struct span_canvas s;
 if(pps<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 struct stem_ink k;stem_inks(m,d,0,&k);
 int16_t scale[256];scale_table(scale,amp);
 int cy=r.y+r.h/2;
 uint64_t cpp=lane_cpp(w,pps);
 for(int x=s.x0;x<s.x1;x++){
  int64_t p=first_px+(x-r.x);
  if(p<0)continue;
  uint64_t from,to;lane_range(cpp,p,&from,&to);
  if(from>=w->count)break;
  const struct xz_shell_column *q=stem_pick(w,(uint32_t)from,(uint32_t)to);
  if(!q->low&&!q->mid&&!q->high)continue;
  int core,inner,total;stem_heights(q,scale,&core,&inner,&total);
  stem_column_centered(&s,x,cy,core,inner,total,&k);
  /* The centre row takes the innermost stem that sounds. */
  vspan(&s,x,cy,cy+1,k.ink[q->high?2:q->mid?1:0][0]);
 }
}
static void stem_overview(const struct xz_shell_model *m, const struct xz_shell_deck *d, struct xz_canvas c, struct xz_shell_rect r, int played_x) {
 const struct xz_shell_wave *w=&d->stems;
 struct span_canvas s;
 if(r.w<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 struct stem_ink k[2];stem_inks(m,d,0,&k[0]);stem_inks(m,d,1,&k[1]);
 int16_t scale[256];scale_table(scale,r.h);
 uint64_t cpp=((uint64_t)w->count<<16)/(uint64_t)r.w;
 if(!cpp)cpp=1;
 int base=r.y+r.h;
 for(int x=s.x0;x<s.x1;x++){
  uint64_t from=((uint64_t)(x-r.x)*cpp)>>16,to=((uint64_t)(x-r.x+1)*cpp)>>16;
  if(from>=w->count)break;
  const struct xz_shell_column *q=stem_pick(w,(uint32_t)from,(uint32_t)to);
  int core,inner,total;stem_heights(q,scale,&core,&inner,&total);
  const struct stem_ink *ink=&k[x<played_x];
  if(total>inner)vspan(&s,x,base-total,base-inner,ink->ink[0][0]);
  if(inner>core)vspan(&s,x,base-inner,base-core,ink->ink[1][0]);
  if(core>0)vspan(&s,x,base-core,base,ink->ink[2][0]);
 }
}

/* ---- a deck's waveforms in the chosen colour ---- */
int sh_stem_wave_ready(const struct xz_shell_deck *d) {
 return sh_has(d,XZ_SHELL_F_STEM_WAVE)&&d->stems.columns&&d->stems.count&&d->stems.span_ms;
}
void sh_deck_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int64_t first_px, int pps) {
 enum xz_shell_wave_color mode=sh_deck_wave_color(u,d);
 if(mode==XZ_SHELL_WAVE_STEMS){
  if(sh_stem_wave_ready(d)){stem_lane(m,d,c,r,amp,first_px,pps);return;}
  mode=XZ_SHELL_WAVE_3BAND;
 }
 if(sh_has(d,XZ_SHELL_F_WAVE))sh_wave_lane(c,r,amp,&d->wave,mode,first_px,pps);
}
void sh_deck_overview(struct xz_canvas c, struct xz_shell_rect r, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int played_x) {
 enum xz_shell_wave_color mode=sh_deck_wave_color(u,d);
 if(mode==XZ_SHELL_WAVE_STEMS){
  if(sh_stem_wave_ready(d)){stem_overview(m,d,c,r,played_x);return;}
  mode=XZ_SHELL_WAVE_3BAND;
 }
 if(sh_has(d,XZ_SHELL_F_OVERVIEW))sh_wave_overview(c,r,&d->overview,mode,played_x);
}

int32_t xz_shell_elapsed_now(const struct xz_shell_model *m, int deck) {
 if(!m||deck<0||deck>=XZ_SHELL_DECKS)return -1;
 const struct xz_shell_deck *d=&m->deck[deck];
 if(!sh_has(d,XZ_SHELL_F_ELAPSED))return -1;
 int64_t e=d->elapsed_ms;
 if(sh_has(d,XZ_SHELL_F_PLAYING)&&d->playing&&d->elapsed_at_ms){
  uint32_t dt=m->clock_ms-d->elapsed_at_ms;
  if(dt<=0x7fffffffu){
   if(dt>XZ_SHELL_EXTRAPOLATE_MAX_MS)dt=XZ_SHELL_EXTRAPOLATE_MAX_MS;
   int64_t rate=10000+(sh_has(d,XZ_SHELL_F_TEMPO)?d->tempo_x100:0);
   if(rate<0)rate=0;
   int64_t advanced=e+(int64_t)dt*rate/10000;
   /* An active loop wraps instead of running past its out point. */
   const struct xz_shell_loop *l=&d->loop;
   if(sh_has(d,XZ_SHELL_F_LOOP)&&l->active&&l->out_ms>l->in_ms&&e>=l->in_ms&&e<l->out_ms&&advanced>=l->out_ms)
    advanced=l->in_ms+(advanced-l->in_ms)%(l->out_ms-l->in_ms);
   e=advanced;
  }
 }
 if(e<0)e=0;
 if(sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0&&e>d->total_ms)e=d->total_ms;
 return (int32_t)e;
}

/* Key notation. Alphanumeric numbers step a fifth (7 semitones) per number;
   8A is A minor, 8B is C major. 7 is its own inverse modulo 12. */
static const char *const minor_names[12]={"Cm","Dbm","Dm","Ebm","Em","Fm","F#m","Gm","Abm","Am","Bbm","Bm"};
static const char *const major_names[12]={"C","Db","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
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
 if(mode==XZ_SHELL_KEY_CLASSIC)return minor?minor_names[pc]:major_names[pc];
 int n=(pc*7+(minor?4:7))%12+1;
 buffer[0]=(char)(n>=10?'1':'0'+n);
 size_t i=1;
 if(n>=10)buffer[i++]=(char)('0'+n-10);
 buffer[i++]=minor?'A':'B';buffer[i]=0;
 return buffer;
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
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  const struct xz_shell_deck *k=&m->deck[d];
  if(sh_has(k,XZ_SHELL_F_MASTER)&&k->master&&sh_has(k,XZ_SHELL_F_KEY)&&k->key&&(!sh_has(k,XZ_SHELL_F_LOADED)||k->loaded))return k->key;
 }
 return NULL;
}
uint32_t sh_key_ink(const struct xz_shell_model *m, const char *key, int matches_master, uint32_t plain) {
 const char *master=xz_shell_master_key(m);
 if(!master)return matches_master?SH_GREEN:plain;
 switch(xz_shell_key_match(key,master)){
 case XZ_SHELL_KEY_MATCH_GREEN:return SH_KEY_GREEN;
 case XZ_SHELL_KEY_MATCH_AMBER:return SH_KEY_AMBER;
 default:return plain;
 }
}
