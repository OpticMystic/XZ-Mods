#ifdef NDEBUG
#error DECK 3/4 detail panel acceptance requires active assertions
#endif
/* SPDX-License-Identifier: MIT */
/* The classic DECK 3/4 detail panel (extra_detail.h) and the four-deck frame composition the runtime draws:
   geometry and hit testing the touch thread shares with the renderer, the needle rule, notices, bounded
   panel-only writes, every state drawn apart, and an open panel leaving every lane right of it untouched. */
#include "extra_detail.h"
#include "extra_lane.h"
#include "ui_skin.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K(f) XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f)
#define STRIDE 540
#define PIXELS (XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT)
static uint16_t canvas[STRIDE*XZ_WAVE_HEIGHT],before[STRIDE*XZ_WAVE_HEIGHT];
static uint16_t closed[PIXELS],open[PIXELS],manual[PIXELS];
static struct xz_extra_column columns[45000];
static struct xz_extra_beat grid[700];
static struct xz_extra_lane_marks marks[2];

static void pattern(void){for(size_t i=0;i<sizeof canvas/sizeof *canvas;i++)canvas[i]=(uint16_t)(0x2222+i%11);}
static struct xz_extra_deck deck(int playing){
 struct xz_extra_deck d;memset(&d,0,sizeof d);
 d.known=((1u<<XZ_EXTRA_FIELD_COUNT)-1u)&~K(TIMELINE);d.loaded=1;
 snprintf(d.title,sizeof d.title,"Night Drive (Extended Mix)");snprintf(d.artist,sizeof d.artist,"Aurora Lane");
 snprintf(d.key,sizeof d.key,"8A");d.source=1;
 d.elapsed_ms=61500;d.total_ms=300000;d.elapsed_at_ms=1000;d.playing=playing;
 d.bpm_x100=13120;d.track_bpm_x100=12800;d.tempo_x100=250;d.tempo_range=10;
 d.master_tempo=d.vinyl=d.quantize=d.remain=d.auto_cue=1;d.active_pad=-1;
 d.cue_ms=60000;d.hot_cue[0]=(struct xz_extra_cue){1,62000,0x30d050,0};d.hot_cue[5]=(struct xz_extra_cue){1,70000,0xffa020,74000};
 for(unsigned i=0;i<45000;i++)columns[i]=(struct xz_extra_column){(uint8_t)(40+i%90),(uint8_t)(30+i%40),(uint8_t)(10+i%30)};
 for(unsigned i=0;i<700;i++)grid[i]=(struct xz_extra_beat){(int32_t)(i*469),(uint8_t)(i%4+1),0};
 d.grid=grid;d.grid_count=700;d.wave=(struct xz_extra_wave){columns,45000,300000,200};
 return d;
}
static uint32_t panel_hash(const struct xz_extra_decks *s,int index,const struct xz_extra_detail_view *v){
 pattern();assert(xz_extra_detail_draw(canvas,STRIDE,sizeof canvas/sizeof *canvas,s,index,v));
 uint32_t h=2166136261u;
 for(size_t i=0;i<sizeof canvas/sizeof *canvas;i++){
  size_t x=i%STRIDE;
  if(x>=XZ_EXTRA_DETAIL_W)assert(canvas[i]==(uint16_t)(0x2222+i%11));
  else h=(h^canvas[i])*16777619u;
 }
 return h;
}
static int boxes_overlap(struct xz_extra_detail_box a,struct xz_extra_detail_box b){
 return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;
}


static void sync_contract(void){
 static const struct {const char *label,*unmatched,*line1,*line2;} rows[]={
  {"SYNC","SYNC","SYNC ON",""},
  {"SYNC","SYNC","MASTER CLOCK UNAVAILABLE","LAST TEMPO RETAINED"},
  {"SYNC 1","SYNC 1","SYNC ON","TEMPO MATCHED"},
  {"BPM 1","SYNC 1","BPM SYNC","NO BEAT GRID"},
  {"SYNC 1","SYNC 1","SYNC ON","JOG CONTROL"},
  {"SYNC 1","SYNC 1","SYNC ON","FOLLOWING DECK 1"},
  {"BPM 1","SYNC 1","BPM SYNC","LOOP ACTIVE"},
  {"LIMIT","LIMIT","SYNC TEMPO LIMIT","LAST TEMPO RETAINED"},
  {"SYNC 1","SYNC 1","SYNC ON","FOLLOWING DECK 1"},
  {"SYNC 1","SYNC 1","SYNC ON","FOLLOWING DECK 1"},
  {"BPM 1","SYNC 1","BPM SYNC","BEAT POSITION UNAVAILABLE"},
  {"MASTER","MASTER","MASTER",""},
  {"EXT MASTER","EXT MASTER","EXTERNAL MASTER","SYNC SOURCE UNAVAILABLE"},
  {"NO BPM","NO BPM","TRACK BPM UNAVAILABLE","SYNC ON"},
  {"BPM 1","SYNC 1","BPM SYNC",""}
 };
 _Static_assert(sizeof rows/sizeof *rows==XZ_EXTRA_SYNC_TEMPO_FOLLOW+1,"all reasons covered");
 struct xz_extra_decks s={.route_up=1};struct xz_extra_detail_view view={1000,0};
 for(int r=0;r<=XZ_EXTRA_SYNC_TEMPO_FOLLOW;r++)for(int q=0;q<2;q++)for(int lock=0;lock<2;lock++)for(int f=0;f<2;f++){
  s.deck[0]=deck(1);struct xz_extra_deck *d=&s.deck[0];d->quantize=q;
  d->sync=(struct xz_extra_sync){.requested=1,.locked=lock,.master_deck=0,.reason=r,
   .tempo_matched=f,.source=XZ_EXTRA_SYNC_SOURCE_LIVE};
  struct xz_extra_sync_presentation p=xz_extra_sync_present(d);
  const char *label=f?rows[r].label:rows[r].unmatched;
  const char *a=rows[r].line1,*b=rows[r].line2;
  if(!f&&!strcmp(a,"BPM SYNC"))a="SYNC ON";
  if(r==XZ_EXTRA_SYNC_WAIT_ANCHOR){if(!q&&f)label="BPM 1";if(!f)b="";}
  if(r==XZ_EXTRA_SYNC_TEMPO_FOLLOW&&!q)b="QUANTIZE OFF";
  assert(!strcmp(p.label,label)&&!strcmp(p.line1,a)&&!strcmp(p.line2,b)&&p.led_on);
  assert(!strstr(p.label,"WAIT")&&!strstr(p.line1,"WAIT")&&!strstr(p.line2,"WAIT"));
  assert(p.status!=XZ_SHELL_SYNC_WAITING);
  assert((p.klass==XZ_EXTRA_SYNC_CLASS_LOCKED)==(r==XZ_EXTRA_SYNC_LOCKED&&lock&&f));
  char chip[32];assert(!strcmp(xz_extra_sync_chip(d,chip,sizeof chip),p.label));
  /* Full text fits the two existing rows, including the chip and its six-pixel gap. */
  struct xz_canvas c={canvas,STRIDE,XZ_EXTRA_DETAIL_W,XZ_EXTRA_DETAIL_H,0};
  assert(6+xz_text_width(c,p.label,1,30)+8+6+xz_text_width(c,p.line1,1,48)<=XZ_EXTRA_DETAIL_W-6);
  assert(6+xz_text_width(c,p.line2,1,48)<=XZ_EXTRA_DETAIL_W-6);
  (void)panel_hash(&s,0,&view);
  d->loaded=0;assert(!xz_extra_sync_present(d).led_on);d->loaded=1;
  d->sync.requested=0;p=xz_extra_sync_present(d);assert(!p.led_on&&!p.label[0]&&!strcmp(p.line1,"SYNC OFF"));
 }
 struct xz_extra_deck d=deck(0);d.sync=(struct xz_extra_sync){.requested=1,.master_deck=-1,.reason=XZ_EXTRA_SYNC_WAIT_MASTER};
 static const char *const sources[]={"NO MASTER","MASTER CLOCK UNAVAILABLE","MASTER CLOCK UNAVAILABLE","EMERGENCY LOOP"};
 for(int source=0;source<=XZ_EXTRA_SYNC_SOURCE_EMERGENCY;source++){
  d.sync.source=source;struct xz_extra_sync_presentation p=xz_extra_sync_present(&d);
  assert(!strcmp(p.label,"SYNC")&&!strcmp(p.line1,sources[source]));
  assert(!strcmp(p.line2,source?"LAST TEMPO RETAINED":"TEMPO UNCHANGED"));
 }
 d.sync.source=0;d.sync.master_deck=2;
 assert(!strcmp(xz_extra_sync_present(&d).line1,"MASTER CLOCK UNAVAILABLE"));
 d.sync.reason=XZ_EXTRA_SYNC_ACQUIRING;assert(!strcmp(xz_extra_sync_present(&d).line2,"FOLLOWING DECK 3"));
 d.sync.reason=XZ_EXTRA_SYNC_NO_GRID;d.sync.quantum_unknown=1;d.sync.tempo_matched=1;
 assert(!strcmp(xz_extra_sync_present(&d).line2,"QUANTIZE VALUE UNAVAILABLE"));
 d.sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;assert(!strcmp(xz_extra_sync_present(&d).line2,"PAUSED / TEMPO MATCHED"));
 d.sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;assert(!strcmp(xz_extra_sync_present(&d).line2,"DECK PAUSED"));
 d.sync.master_paused=1;assert(!strcmp(xz_extra_sync_present(&d).line2,"MASTER PAUSED"));
 d.sync.reason=999;d.sync.locked=1;
 assert(!strcmp(xz_extra_sync_present(&d).label,"SYNC")&&xz_extra_sync_present(&d).klass==XZ_EXTRA_SYNC_CLASS_ENABLED);
 d.master=1;d.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;d.sync.requested=0;
 assert(!strcmp(xz_extra_sync_present(&d).label,"3/4 MASTER")&&!xz_extra_sync_present(&d).led_on);
 puts("PASS all SYNC reasons x QUANTIZE x lock x tempo proof: exact copy, bounded render, no WAIT, steady intent LED");
}

int main(void){
 sync_contract();
 /* Geometry: inside the left half, clear of the playhead and of the header strip's rows, keys apart. */
 assert(XZ_EXTRA_DETAIL_W<XZ_EXTRA_LANE_PLAYHEAD_X&&XZ_EXTRA_DETAIL_H==XZ_WAVE_HEIGHT);
 struct xz_extra_detail_box panel=xz_extra_detail_box(XZ_EXTRA_DETAIL_PANEL);
 assert(!panel.x&&!panel.y&&panel.w==XZ_EXTRA_DETAIL_W&&panel.h==XZ_EXTRA_DETAIL_H);
 for(int hit=XZ_EXTRA_DETAIL_CLOSE;hit<XZ_EXTRA_DETAIL_HITS;hit++){
  struct xz_extra_detail_box b=xz_extra_detail_box(hit);
  assert(b.w>=40&&b.h>=22&&b.x>=0&&b.y>=6&&b.x+b.w<=XZ_EXTRA_DETAIL_W&&b.y+b.h<=XZ_EXTRA_DETAIL_H);
  assert(xz_extra_detail_hit(b.x+b.w/2,b.y+b.h/2)==hit&&xz_extra_detail_hit(b.x,b.y)==hit&&xz_extra_detail_hit(b.x+b.w-1,b.y+b.h-1)==hit);
  for(int other=hit+1;other<XZ_EXTRA_DETAIL_HITS;other++)assert(!boxes_overlap(b,xz_extra_detail_box(other)));
 }
 assert(xz_extra_detail_hit(XZ_EXTRA_DETAIL_W,100)==XZ_EXTRA_DETAIL_OUTSIDE&&xz_extra_detail_hit(-1,100)==XZ_EXTRA_DETAIL_OUTSIDE);
 assert(xz_extra_detail_hit(100,XZ_EXTRA_DETAIL_H)==XZ_EXTRA_DETAIL_OUTSIDE&&xz_extra_detail_hit(100,-1)==XZ_EXTRA_DETAIL_OUTSIDE);
 assert(xz_extra_detail_hit(100,150)==XZ_EXTRA_DETAIL_PANEL&&xz_extra_detail_hit(0,0)==XZ_EXTRA_DETAIL_PANEL);
 assert(xz_extra_detail_box(XZ_EXTRA_DETAIL_HITS).w==0);
 /* The strip: ends map to the track's ends, clamped outside, monotonic, and its own inverse within a column. */
 struct xz_extra_detail_box strip=xz_extra_detail_box(XZ_EXTRA_DETAIL_OVERVIEW);
 assert(xz_extra_detail_seek_ms(strip.x,300000)==0&&xz_extra_detail_seek_ms(strip.x+strip.w-1,300000)==300000);
 assert(xz_extra_detail_seek_ms(-50,300000)==0&&xz_extra_detail_seek_ms(9999,300000)==300000&&xz_extra_detail_seek_ms(100,0)==0);
 for(int x=strip.x,last=-1;x<strip.x+strip.w;x++){
  int32_t ms=xz_extra_detail_seek_ms(x,300000);assert(ms>last);last=ms;
  int back=xz_extra_detail_column(ms,300000);assert(back==x||back==x-1);
 }
 assert(xz_extra_detail_column(-5,300000)==strip.x&&xz_extra_detail_column(400000,300000)==strip.x+strip.w-1);
 assert(xz_extra_detail_column(10,0)==strip.x);
 /* The needle rule: loaded, length known, paused and not held; unknown play state stays locked. */
 struct xz_extra_deck d=deck(0);
 assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NONE);
 assert(xz_extra_detail_seek_refusal(NULL)==XZ_EXTRA_DETAIL_NOTICE_NO_TRACK);
 d.playing=1;assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING);
 d.playing=0;d.held_playing=1;assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_HELD);
 d.platter_contact=1;assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NONE);d.platter_contact=0;
 d.held_playing=0;d.known&=~K(PLAYING);assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING);
 d.known|=K(PLAYING);d.known&=~K(TOTAL);assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NO_LENGTH);
 d.known|=K(TOTAL);d.total_ms=0;assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NO_LENGTH);
 d.total_ms=300000;d.loaded=0;assert(xz_extra_detail_seek_refusal(&d)==XZ_EXTRA_DETAIL_NOTICE_NO_TRACK);
 /* Notices: one short line each, naming the deck where it is the deck's. */
 char text[96];
 assert(!xz_extra_detail_notice_text(XZ_EXTRA_DETAIL_NOTICE_NONE,3,text,sizeof text)[0]);
 assert(!xz_extra_detail_notice_text(XZ_EXTRA_DETAIL_NOTICES,3,text,sizeof text)[0]);
 for(int n=XZ_EXTRA_DETAIL_NOTICE_NONE+1;n<XZ_EXTRA_DETAIL_NOTICES;n++){
  size_t length=strlen(xz_extra_detail_notice_text(n,4,text,sizeof text));
  assert(length>10&&length<=42);
  if(n<=XZ_EXTRA_DETAIL_NOTICE_REFUSED)assert(strstr(text,"DECK 4"));
 }

 /* Drawing: guards write nothing; a draw writes only the panel's columns and repeats exactly. */
 struct xz_extra_decks s;memset(&s,0,sizeof s);s.route_up=1;s.deck[0]=deck(0);s.deck[1]=deck(1);
 struct xz_extra_detail_view view={1000,XZ_EXTRA_DETAIL_NOTICE_NONE};
 pattern();memcpy(before,canvas,sizeof canvas);
 assert(!xz_extra_detail_draw(NULL,STRIDE,sizeof canvas/sizeof *canvas,&s,0,&view));
 assert(!xz_extra_detail_draw(canvas,STRIDE,sizeof canvas/sizeof *canvas,NULL,0,&view));
 assert(!xz_extra_detail_draw(canvas,STRIDE,sizeof canvas/sizeof *canvas,&s,0,NULL));
 assert(!xz_extra_detail_draw(canvas,STRIDE,sizeof canvas/sizeof *canvas,&s,2,&view));
 assert(!xz_extra_detail_draw(canvas,STRIDE,sizeof canvas/sizeof *canvas,&s,-1,&view));
 assert(!xz_extra_detail_draw(canvas,530,sizeof canvas/sizeof *canvas,&s,0,&view));
 assert(!xz_extra_detail_draw(canvas,STRIDE,sizeof canvas/sizeof *canvas-1,&s,0,&view));
 assert(!memcmp(before,canvas,sizeof canvas));
 uint32_t base=panel_hash(&s,0,&view);assert(base==panel_hash(&s,0,&view));
 /* Every state the panel reports draws apart from the base, inside the panel. */
 uint32_t seen[64];unsigned count=0;
 for(int change=0;change<41;change++){
  struct xz_extra_decks m=s;struct xz_extra_detail_view mv=view;struct xz_extra_deck *x=&m.deck[0];
  switch(change){
  case 0:x->remain=0;break;
  case 1:x->sync=(struct xz_extra_sync){.requested=1,.locked=1,.master_deck=0,.reason=XZ_EXTRA_SYNC_LOCKED,.phase_error_us=0,.correction_ppm=0,.master_scope=XZ_EXTRA_MASTER_SCOPE_NATIVE,.tempo_matched=1};break;
  case 2:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW,.phase_error_us=0,.correction_ppm=0,.master_scope=XZ_EXTRA_MASTER_SCOPE_NATIVE,.tempo_matched=1};break;
  case 3:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE,.phase_error_us=0,.correction_ppm=0,.master_scope=XZ_EXTRA_MASTER_SCOPE_NATIVE,.tempo_matched=1};break;
  case 4:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 5:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=-1,.reason=XZ_EXTRA_SYNC_WAIT_MASTER,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 6:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_DISCONTINUITY,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 7:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_MANUAL,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 8:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_NO_GRID,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 9:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_NO_SOURCE_BPM,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 10:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_OUT_OF_RANGE,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 11:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=0,.reason=XZ_EXTRA_SYNC_LOOP_UNALIGNED,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 12:x->sync=(struct xz_extra_sync){.requested=1,.locked=0,.master_deck=-1,.reason=XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED,.phase_error_us=0,.correction_ppm=0,.master_scope=0,.tempo_matched=1};break;
  case 13:x->master=1;x->master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;break;
  case 14:x->pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;break;
  case 15:x->pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;x->active_pad=2;break;
  case 16:x->pad_mode=XZ_EXTRA_PAD_SLIP_LOOP;break;
  case 17:x->pad_mode=XZ_EXTRA_PAD_BEAT_JUMP;break;
  case 18:x->pad_mode=XZ_EXTRA_PAD_NONE;break;
  case 19:x->quantize=0;break;
  case 20:x->auto_cue=0;break;
  case 21:x->vinyl=0;break;
  case 22:x->reverse=1;break;
  case 23:x->slip=1;break;
  case 24:x->slip=x->slip_active=1;break;
  case 25:x->tempo_reset=1;x->tempo_x100=0;break;
  case 26:x->tempo_range=100;break;
  case 27:x->master_tempo=0;break;
  case 28:x->loop.active=x->loop.stored=1;x->loop.in_ms=61000;x->loop.out_ms=62875;break;
  case 29:x->loop.active=x->loop.stored=1;x->loop.in_ms=61000;x->loop.out_ms=62875;x->loop.edit=1;break;
  case 30:x->loop.stored=1;x->loop.in_ms=61000;x->loop.out_ms=62875;break;
  case 31:x->playing=1;break;
  case 32:x->held_playing=1;break;
  case 33:mv.notice=XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING;break;
  case 34:mv.notice=XZ_EXTRA_DETAIL_NOTICE_SELECT_FAILED;break;
  case 35:m.notice_seq=1;m.notice_at_ms=900;snprintf(m.notice,sizeof m.notice,"LOOP EXIT");break;
  case 36:m.left_on_3=1;break;
  case 37:x->loaded=0;break;
  case 38:snprintf(x->key,sizeof x->key,"11B");x->source=2;break;
  case 40:x->pad_mode=XZ_EXTRA_PAD_CALL;break;
  default:x->elapsed_ms=120000;break;
  }
  uint32_t h=panel_hash(&m,0,&mv);
  if(h==base){fprintf(stderr,"panel state %d draws like the base\n",change);assert(0);}
  for(unsigned i=0;i<count;i++)if(seen[i]==h&&!(change==6&&i==1)){fprintf(stderr,"panel state %d draws like state %u\n",change,i);assert(0);}
  seen[count++]=h;
 }
 /* A notice for the panel wins over the players' notice; the players' notice expires on its own. */
 struct xz_extra_decks noted=s;noted.notice_seq=1;noted.notice_at_ms=900;snprintf(noted.notice,sizeof noted.notice,"LOOP EXIT");
 struct xz_extra_detail_view refused={1000,XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING};
 assert(panel_hash(&noted,0,&refused)==panel_hash(&s,0,&refused));
 struct xz_extra_detail_view later={900+3600,XZ_EXTRA_DETAIL_NOTICE_NONE};
 assert(panel_hash(&noted,0,&later)==panel_hash(&s,0,&later));
 /* DECK 4 draws its own deck and side, not DECK 3's. */
 assert(panel_hash(&s,1,&view)!=base);
 /* A long title, an unknown title and a 2-hour wave stay inside the panel. */
 struct xz_extra_decks edge=s;memset(edge.deck[0].title,'W',sizeof edge.deck[0].title-1);edge.deck[0].title[sizeof edge.deck[0].title-1]=0;
 (void)panel_hash(&edge,0,&view);
 edge.deck[0].known&=~K(TITLE);(void)panel_hash(&edge,0,&view);
 struct xz_extra_column *huge=calloc(1080000,sizeof *huge);assert(huge);
 for(unsigned i=0;i<1080000;i+=97)huge[i]=(struct xz_extra_column){200,100,50};
 edge=s;edge.deck[0].wave=(struct xz_extra_wave){huge,1080000,7200000,300};edge.deck[0].total_ms=7200000;
 (void)panel_hash(&edge,0,&view);free(huge);

 /* The frame: closed, exactly the lanes and tags; open, the panel and lane numbers only, every lane right of it
    (playheads, waveforms, marks) as closed. */
 for(int order=XZ_FOUR_STACKED;order<=XZ_FOUR_MIXER;order++){
  struct xz_four_layout g;assert(xz_four_layout_get(order,&g));
  struct xz_four_frame frame={&s,order,1000,XZ_EXTRA_LANE_COLUMN_US,1,-1,0,0};
  for(size_t i=0;i<PIXELS;i++)closed[i]=manual[i]=(uint16_t)(0x1357+i%5);
  assert(xz_four_frame_draw(closed,PIXELS,&frame,marks));
  struct xz_extra_lane_view lv={1000,XZ_EXTRA_LANE_COLUMN_US,1,0};
  for(unsigned i=0;i<2;i++){lv.lit=i?s.right_on_4:s.left_on_3;assert(xz_extra_lane_draw_wave(manual,XZ_WAVE_WIDTH,PIXELS,g.lane_y[2+i],3+i,&s.deck[i],&lv,NULL));}
  assert(xz_four_badge_draw(manual,XZ_WAVE_WIDTH,PIXELS,g.lane_y[0],1,1)&&xz_four_badge_draw(manual,XZ_WAVE_WIDTH,PIXELS,g.lane_y[1],2,1));
  assert(!memcmp(closed,manual,sizeof closed));
  for(int index=0;index<2;index++){
   for(size_t i=0;i<PIXELS;i++)open[i]=(uint16_t)(0x1357+i%5);
   frame.detail=index;assert(xz_four_frame_draw(open,PIXELS,&frame,marks));
   unsigned tabs=0;
   for(size_t i=0;i<PIXELS;i++){
    unsigned x=(unsigned)(i%XZ_WAVE_WIDTH),y=(unsigned)(i/XZ_WAVE_WIDTH);
    if(x<XZ_EXTRA_DETAIL_W||open[i]==closed[i])continue;
    int deck=xz_four_layout_lane_at(&g,(int)y);
    assert(deck>=0&&x>=XZ_WAVE_WIDTH-15&&x<XZ_WAVE_WIDTH-3&&y>=g.lane_y[deck]+2&&y<g.lane_y[deck]+(g.lane_y[deck]?15u:21u));
    tabs++;
   }
   assert(tabs>4*40);
  }
  frame.detail=-1;
 }
 /* After a theme pass the number boxes go back in their own colours, and nothing else does: closed (every
    lane's tag box), with the panel open (its header box and the lane numbers), and with 3/4 not connected. */
 for(int order=XZ_FOUR_STACKED;order<=XZ_FOUR_MIXER;order++)for(int state=0;state<4;state++){
  struct xz_four_layout g;assert(xz_four_layout_get(order,&g));
  int detail=state==1?0:state==2?1:-1,down=state==3;
  struct xz_four_frame frame={&s,order,1000,XZ_EXTRA_LANE_COLUMN_US,1,detail,0,down};
  for(size_t i=0;i<PIXELS;i++)closed[i]=(uint16_t)(0x1357+i%5);
  assert(xz_four_frame_draw(closed,PIXELS,&frame,marks));
  for(size_t i=0;i<PIXELS;i++)open[i]=(uint16_t)~closed[i];
  xz_four_identity_draw(open,PIXELS,&frame);
  unsigned kept=0;
  for(size_t i=0;i<PIXELS;i++){
   int x=(int)(i%XZ_WAVE_WIDTH),y=(int)(i/XZ_WAVE_WIDTH),box=0;
   if(detail>=0){
    box=x>=6&&x<28&&y>=7&&y<29;
    for(int d=0;d<4;d++){int ny=(int)g.lane_y[d]+(g.lane_y[d]?2:8)+1;box|=x>=XZ_WAVE_WIDTH-14&&x<XZ_WAVE_WIDTH-4&&y>=ny&&y<ny+11;}
   }else for(int d=0;d<(down?2:4);d++)box|=x>=3&&x<3+XZ_FOUR_NUMBER_BOX&&y>=(int)g.lane_y[d]+2&&y<(int)g.lane_y[d]+2+XZ_FOUR_NUMBER_BOX;
   if(box){assert(open[i]==closed[i]);kept++;}else assert(open[i]==(uint16_t)~closed[i]);
  }
  assert(kept==(detail>=0?22u*22u+4u*10u*11u:(down?2u:4u)*XZ_FOUR_NUMBER_BOX*XZ_FOUR_NUMBER_BOX));
 }
 struct xz_four_frame bad={&s,7,1000,0,1,-1,0,0};
 assert(!xz_four_frame_draw(closed,PIXELS,&bad,marks));bad.order=XZ_FOUR_STACKED;
 assert(!xz_four_frame_draw(closed,PIXELS-1,&bad,marks)&&!xz_four_frame_draw(NULL,PIXELS,&bad,marks));
 bad.decks=NULL;assert(!xz_four_frame_draw(closed,PIXELS,&bad,marks));
 puts("PASS DECK 3/4 detail panel: shared touch geometry, strip seek inverse, NEEDLE LOCK rule, notices, panel-only writes, "
      "41 bounded states with shared enabled SYNC styling, notice priority, frame = lanes + tags when closed, lanes right of an open panel untouched in both orders, number boxes restored after a theme pass and nothing else");
 return 0;
}
