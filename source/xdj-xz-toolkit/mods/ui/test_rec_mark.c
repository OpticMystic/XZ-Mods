/* SPDX-License-Identifier: MIT */
/* The REC mark (rec_mark.h): its clock and levels, the runtime tracker over the recorder seam (session time
   through file splits, failed reads, a recording met mid-file), what fits where, and its paint in all 25
   themes: inside its own box, a clock that never changes the pill's size within a form, a blink that
   changes only the dot, labels that read on both fills, CLASSIC's band paint inside rows 0..17 and left of
   DECKS with an exact restore, and the MODS panel header. */
#ifdef NDEBUG
#error REC mark acceptance requires assertions
#endif
#include "rec_mark.h"
#include "ui.h"
#include "ui_skin.h"
#include "deck_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { STRIDE=832, SENTINEL=0x1234 };
static uint16_t frame[STRIDE*480],other[STRIDE*480];
static void clear(uint16_t *f){for(size_t i=0;i<STRIDE*480;i++)f[i]=SENTINEL;}
static const uint32_t HOUR=3600000u;

static struct xz_rec_view view_of(int recording,uint32_t elapsed,int time_known){
 struct xz_rec_reading r={1,(uint8_t)recording,(uint8_t)time_known,elapsed,1000,1};
 return xz_rec_view_at(&r,1000);
}
static struct xz_recorder_state seam(int known,int recording,uint32_t elapsed,uint32_t generation){
 struct xz_recorder_state s={known,recording,elapsed,generation,{0}};return s;
}

static void clock_text(void){
 static const struct { uint32_t ms; const char *text; } cases[]={
  {0,"00:00"},{3000,"00:03"},{3999,"00:03"},{59*60000u+59000u,"59:59"},{HOUR-1,"59:59"},{HOUR,"1:00:00"},
  {(72*60u+40u)*1000u,"1:12:40"},{10*HOUR,"10:00:00"},{100*HOUR,"99:59:59"},{UINT32_MAX,"99:59:59"}};
 for(size_t i=0;i<sizeof cases/sizeof *cases;i++){
  char out[12];xz_rec_clock(out,cases[i].ms);
  if(strcmp(out,cases[i].text)){fprintf(stderr,"FAIL clock %u: %s, not %s\n",cases[i].ms,out,cases[i].text);assert(0);}
 }
 puts("PASS clock: MM:SS under an hour, H:MM:SS after, held at 99:59:59");
}

static void levels(void){
 struct xz_rec_reading zero={0};
 assert(xz_rec_view_at(&zero,5000).level==XZ_REC_NONE);                       /* unknown: no claim */
 assert(xz_rec_view_at(NULL,5000).level==XZ_REC_NONE);
 struct xz_rec_reading idle={1,0,0,0,1000,3};assert(xz_rec_view_at(&idle,5000).level==XZ_REC_NONE);
 struct xz_rec_view v=view_of(1,3000,1);assert(v.level==XZ_REC_ON&&v.dot_lit&&v.time_known&&v.elapsed_ms==3000);
 v=view_of(1,HOUR-1,1);assert(v.level==XZ_REC_ON);
 v=view_of(1,HOUR,1);assert(v.level==XZ_REC_LONG&&v.dot_lit);
 /* The long dot: lit for the first half of each second, dark for the second. */
 for(uint32_t ms=0;ms<3000;ms+=100){v=view_of(1,HOUR+ms,1);assert(v.dot_lit==(ms%1000<XZ_REC_BLINK_MS));}
 /* The calm dot never blinks. */
 for(uint32_t ms=0;ms<3000;ms+=100)assert(view_of(1,60000+ms,1).dot_lit);
 /* The clock runs on from a reading for XZ_REC_EXTRAPOLATE_MS, then holds; a reading from the future does not
    run backwards. */
 struct xz_rec_reading r={1,1,1,10000,1000,1};
 assert(xz_rec_view_at(&r,1000+1234).elapsed_ms==11234);
 assert(xz_rec_view_at(&r,1000+60000).elapsed_ms==10000+XZ_REC_EXTRAPOLATE_MS);
 assert(xz_rec_view_at(&r,900).elapsed_ms==10000);
 /* The clock domain wraps. */
 struct xz_rec_reading w={1,1,1,5000,UINT32_MAX-100,1};assert(xz_rec_view_at(&w,400).elapsed_ms==5501);
 /* No clock: the level still escalates on what is known. */
 v=view_of(1,2*HOUR,0);assert(v.level==XZ_REC_LONG&&!v.time_known);
 puts("PASS levels: nothing unless known and recording, calm under an hour, long after with a half-second dot");
}

static void tracker(void){
 /* Seen from the start: the session clock runs on through a TRACK MARK split (new generation, elapsed back
    to 0) and the stock 3-hour split. */
 struct xz_rec_tracker t={0};struct xz_recorder_state s=seam(1,0,0,0);
 struct xz_rec_reading r=xz_rec_observe(&t,&s,1000);assert(r.known&&!r.recording);
 s=seam(1,1,40,1);r=xz_rec_observe(&t,&s,1250);assert(r.recording&&r.elapsed_known&&r.elapsed_ms==40);
 s=seam(1,1,600000,1);r=xz_rec_observe(&t,&s,1250+600000-40);assert(r.elapsed_ms==600000);
 s=seam(1,1,200,2);r=xz_rec_observe(&t,&s,1250+605000);assert(r.elapsed_ms==605040&&r.generation==2);
 s=seam(1,1,10800000u-1000u,3);r=xz_rec_observe(&t,&s,1210+3*HOUR+605000);assert(r.elapsed_ms==3*HOUR+605000);
 assert(xz_rec_view_at(&r,1210+3*HOUR+605000).level==XZ_REC_LONG);
 /* Stop, then a new recording starts from zero. */
 s=seam(1,0,0,3);r=xz_rec_observe(&t,&s,20000000);assert(!r.recording);
 s=seam(1,1,0,4);r=xz_rec_observe(&t,&s,20000250);assert(r.recording&&r.elapsed_known&&r.elapsed_ms==0);
 s=seam(1,1,1000,4);r=xz_rec_observe(&t,&s,20001250);assert(r.elapsed_ms==1000);
 /* One failed read (the seam's snapshots disagreed) holds the last reading; a longer loss is unknown, and the
    next recording is met mid-file. */
 s=seam(0,0,0,0);struct xz_rec_reading held=xz_rec_observe(&t,&s,20002000);assert(held.recording&&held.read_at_ms==20001250);
 assert(xz_rec_view_at(&held,20002000).elapsed_ms==1750);
 r=xz_rec_observe(&t,&s,20001250+XZ_REC_HOLD_MS);assert(!r.known&&xz_rec_view_at(&r,20005000).level==XZ_REC_NONE);
 /* Met mid-file: the current file's time is all that is known (a lower bound). */
 s=seam(1,1,90000,9);r=xz_rec_observe(&t,&s,30000000);assert(r.recording&&r.elapsed_known&&r.elapsed_ms==90000);
 /* Met with no elapsed and the start unseen: no clock until the seam gives one. */
 struct xz_rec_tracker u={0};s=seam(1,1,0,5);r=xz_rec_observe(&u,&s,1000);assert(r.recording&&!r.elapsed_known);
 assert(!xz_rec_view_at(&r,1000).time_known&&xz_rec_view_at(&r,1000).level==XZ_REC_ON);
 s=seam(1,1,5000,5);r=xz_rec_observe(&u,&s,2000);assert(r.elapsed_known&&r.elapsed_ms==5000);
 /* A build that cannot tell from the first read: nothing, ever. */
 struct xz_rec_tracker n={0};s=seam(0,0,0,0);
 for(uint32_t now=0;now<10000;now+=250){r=xz_rec_observe(&n,&s,now);assert(!r.known);}
 assert(!xz_rec_observe(NULL,&s,0).known&&!xz_rec_observe(&n,NULL,0).known);
 puts("PASS tracker: a session clock through splits, failed reads held 3 s, mid-file and clockless meets told apart");
}

/* Every changed pixel lies inside [x0,x1) x [y0,y1). */
static void inside(const uint16_t *f,int x0,int y0,int x1,int y1,const char *what,int theme){
 for(int y=0;y<480;y++)for(int x=0;x<STRIDE;x++){
  if(f[y*STRIDE+x]==SENTINEL)continue;
  if(x<x0||x>=x1||y<y0||y>=y1){fprintf(stderr,"FAIL %s theme %d paints (%d,%d) outside %d..%d x %d..%d\n",what,theme,x,y,x0,x1-1,y0,y1-1);assert(0);}
 }
}
static int changed(const uint16_t *a,const uint16_t *b){int n=0;for(size_t i=0;i<STRIDE*480;i++)n+=a[i]!=b[i];return n;}
static int luma(uint16_t v){return (((v>>11)&31)*255/31*299+((v>>5)&63)*255/63*587+(v&31)*255/31*114)/1000;}

static void fitting(void){
 struct xz_canvas c={frame,STRIDE,800,480,XZ_THEME_FLAGSHIP};
 struct xz_rec_view v=view_of(1,(72*60u+40u)*1000u,1),calm=view_of(1,3000,1),blind=view_of(1,3000,0);
 assert(xz_rec_parts(c,120,20,&v)==(XZ_REC_PART_WORD|XZ_REC_PART_TIME));
 assert(xz_rec_parts(c,68,20,&v)==XZ_REC_PART_TIME);            /* MODERN's header slot */
 assert(xz_rec_parts(c,46,18,&v)==XZ_REC_PART_WORD);            /* CLASSIC beside VJ.TOOLS */
 assert(xz_rec_parts(c,20,20,&v)==0);                           /* a lit BEAT FX strip: the dot */
 assert(xz_rec_parts(c,120,20,&blind)==XZ_REC_PART_WORD);       /* no clock without a time */
 assert(xz_rec_box(c,19,20,&v)==0);                             /* not even the dot: nothing */
 struct xz_rec_view none={0};assert(xz_rec_box(c,120,20,&none)==0);
 /* Every theme: the full pill fits MODERN's top bar room and CLASSIC's band, the time-only pill the header slot. */
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  c.theme=theme;
  assert(xz_rec_box(c,120,20,&v)<=110&&xz_rec_box(c,120,18,&v)<=120);
  assert(xz_rec_box(c,68,20,&calm)<=68&&(xz_rec_parts(c,68,20,&calm)&XZ_REC_PART_TIME));
 }
 puts("PASS fitting: dot REC time, then dot time, then dot REC, then the dot; never a clock it does not have");
}

static void paint(void){
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  struct xz_canvas c={frame,STRIDE,800,480,theme};
  for(int level=0;level<2;level++){
   uint32_t base=level?HOUR+(72*60u+40u-3600u)*1000u:3000;
   struct xz_rec_view v=view_of(1,base,1);
   int w=xz_rec_box(c,120,20,&v);
   clear(frame);int left=xz_rec_draw(c,500,3,20,120,&v);
   assert(left==500-w);inside(frame,500-w,3,500,23,"pill",theme);
   /* The clock never resizes the pill within a form: every second of the first hour, every minute after. */
   for(uint32_t s=0;s<(level?6*3600u:3600u);s+=level?60u:1u){
    struct xz_rec_view t=view_of(1,(level?HOUR:0)+s*1000u,1);
    if(xz_rec_box(c,120,20,&t)!=w){fprintf(stderr,"FAIL theme %d: the pill is %d wide at %us, %d at the start\n",theme,xz_rec_box(c,120,20,&t),s,w);assert(0);}
   }
   /* The ink reads on the fill: the label's white against the pill's middle row beside it. */
   uint16_t fill=frame[13*STRIDE+(500-6)],ink=0xffff;int d=luma(ink)-luma(fill);
   if(d<0)d=-d;
   if(d<96){fprintf(stderr,"FAIL theme %d level %d: label brightness difference %d\n",theme,level,d);assert(0);}
  }
  /* The long state's blink changes only the dot: at most one (2r+1)^2 box, a few dozen pixels. */
  struct xz_rec_view lit=view_of(1,HOUR+100,1),dark=view_of(1,HOUR+600,1);
  struct xz_canvas o={other,STRIDE,800,480,theme};
  clear(frame);xz_rec_draw(c,500,3,20,120,&lit);clear(other);xz_rec_draw(o,500,3,20,120,&dark);
  int n=changed(frame,other);
  if(n==0||n>81){fprintf(stderr,"FAIL theme %d: the blink changes %d pixels\n",theme,n);assert(0);}
  /* A second's tick in the calm state changes only the clock's last digits. */
  struct xz_rec_view a=view_of(1,12000,1),b=view_of(1,13000,1);
  clear(frame);xz_rec_draw(c,500,3,20,120,&a);clear(other);xz_rec_draw(o,500,3,20,120,&b);
  n=changed(frame,other);
  if(n==0||n>20*12){fprintf(stderr,"FAIL theme %d: a tick changes %d pixels\n",theme,n);assert(0);}
  /* Nothing drawn while idle, unknown or out of room. */
  struct xz_rec_view none={0};clear(frame);assert(xz_rec_draw(c,500,3,20,120,&none)==500);
  struct xz_rec_view on=view_of(1,3000,1);assert(xz_rec_draw(c,500,3,20,10,&on)==500);
  for(size_t i=0;i<STRIDE*480;i++)assert(frame[i]==SENTINEL);
 }
 puts("PASS paint: inside its box in all 25 themes, a fixed size per form, a dot-only blink, a digit-only tick, labels that read");
}

static void band(void){
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  for(int vj=0;vj<2;vj++)for(int level=0;level<2;level++){
   struct xz_rec_band_pixels saved={0};
   struct xz_rec_view v=view_of(1,level?HOUR+5000:3000,1);
   clear(frame);int w=xz_rec_band_draw(frame,STRIDE,theme,vj,&v,&saved);
   assert(w>0&&saved.valid);
   /* Rows 0..17 only, never into VJ.TOOLS (x <= 111) while it shows, never into DECKS (x >= 162). */
   inside(frame,vj?XZ_REC_BAND_VJ_LEFT:XZ_REC_BAND_RIGHT-XZ_REC_BAND_W,0,XZ_REC_BAND_RIGHT,XZ_TOP_BAR_H,"band",theme);
   assert(xz_deck_view_classic.button.x>=XZ_REC_BAND_RIGHT+2&&XZ_REC_BAND_VJ_LEFT>XZ_TOP_VJ_X+XZ_TOP_VJ_W);
   /* With VJ.TOOLS hidden the full pill shows; beside it, the dot and REC. */
   struct xz_canvas c={frame,STRIDE,800,XZ_TOP_BAR_H,theme};
   assert(xz_rec_parts(c,vj?XZ_REC_BAND_RIGHT-XZ_REC_BAND_VJ_LEFT:XZ_REC_BAND_W,XZ_TOP_BAR_H,&v)==(vj?XZ_REC_PART_WORD:(XZ_REC_PART_WORD|XZ_REC_PART_TIME)));
   /* The recording stops: the next frame's restore gives back the stock pixels exactly. */
   xz_rec_band_restore(&saved,frame,STRIDE);
   for(size_t i=0;i<STRIDE*480;i++)assert(frame[i]==SENTINEL);
   assert(!saved.valid);
   /* Stock repainted the band since: its fresh pixels win, the restore writes nothing. */
   clear(frame);xz_rec_band_draw(frame,STRIDE,theme,vj,&v,&saved);
   for(int y=0;y<XZ_TOP_BAR_H;y++)for(int x=0;x<800;x++)frame[y*STRIDE+x]=0x0841;
   memcpy(other,frame,sizeof frame);xz_rec_band_restore(&saved,frame,STRIDE);assert(!changed(frame,other));
   /* Idle or unknown: no paint and nothing saved. */
   struct xz_rec_view none={0};clear(frame);assert(!xz_rec_band_draw(frame,STRIDE,theme,vj,&none,&saved)&&!saved.valid);
  }
 }
 puts("PASS band: rows 0..17 between VJ.TOOLS and DECKS in all 25 themes, an exact restore, fresh stock pixels win");
}

/* The MODS panel: the recorder changes only the pill's box in the header, in every theme. */
static void panel(void){
 struct xz_ui ui;struct xz_ui_model m={0};
 xz_ui_init(&ui);m.jump=xz_jump_defaults();m.clock_ms=5000;m.settings_status="SETTINGS SAVED TO USB 1";
 for(int d=0;d<4;d++){m.deck[d].levels[0]=m.deck[d].levels[1]=m.deck[d].levels[2]=1;m.deck[d].groove_active=m.deck[d].sample_active=-1;}
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  m.theme=theme;ui.page=XZ_UI_HOME;
  m.recorder=(struct xz_rec_reading){0};
  assert(xz_ui_render(&ui,&m,frame,STRIDE*480,STRIDE));
  m.recorder=(struct xz_rec_reading){1,0,0,0,5000,1};
  assert(xz_ui_render(&ui,&m,other,STRIDE*480,STRIDE));assert(!changed(frame,other)); /* idle: the same frame */
  m.recorder=(struct xz_rec_reading){1,1,1,(72*60u+40u)*1000u,5000,1};
  assert(xz_ui_render(&ui,&m,other,STRIDE*480,STRIDE));
  int x0=800,x1=-1,y0=480,y1=-1;
  for(int y=0;y<480;y++)for(int x=0;x<800;x++)if(frame[y*STRIDE+x]!=other[y*STRIDE+x]){if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;}
  assert(x1>=0);
  if(x1>=XZ_REC_PANEL_RIGHT||x0<XZ_REC_PANEL_RIGHT-XZ_REC_PANEL_ROOM||y0<XZ_REC_PANEL_Y||y1>=XZ_REC_PANEL_Y+XZ_REC_PANEL_H||y1>=XZ_UI_HEAD){
   fprintf(stderr,"FAIL panel theme %d changes %d..%d x %d..%d\n",theme,x0,x1,y0,y1);assert(0);
  }
 }
 puts("PASS panel: the header pill is the only change in all 25 themes; idle leaves the frame untouched");
}

int main(void){
 clock_text();levels();tracker();fitting();paint();band();panel();
 return 0;
}
