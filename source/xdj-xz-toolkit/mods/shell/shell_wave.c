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

void sh_wave_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int64_t first_px, int pps) {
 struct span_canvas s;
 if(!w->columns||!w->count||!w->span_ms||pps<=0||!clip(c,r.x,r.y,r.w,r.h,&s))return;
 int16_t scale[256];scale_table(scale,amp);
 int cy=r.y+r.h/2;
 /* Columns per track pixel in 16.16. */
 uint64_t cpp=((uint64_t)w->count*1000u<<16)/((uint64_t)w->span_ms*(uint64_t)pps);
 if(!cpp)cpp=1;
 for(int x=s.x0;x<s.x1;x++){
  int64_t p=first_px+(x-r.x);
  if(p<0)continue;
  uint64_t from=((uint64_t)p*cpp)>>16,to=((uint64_t)(p+1)*cpp)>>16;
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
