#ifdef NDEBUG
#error Extra-deck lane acceptance requires active assertions
#endif
#include "extra_lane.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 540
#define ALL ((1u<<(XZ_EXTRA_F_VINYL+1))-1) /* legacy fully populated fixture */
static uint16_t pixels[STRIDE*268+8],copy[STRIDE*268+8];
static struct xz_extra_column columns[3000];
static struct xz_extra_beat grid[64];
static struct xz_extra_lane_marks marks;

static void fill(void){
 for(size_t i=0;i<sizeof(pixels)/sizeof(*pixels);i++)pixels[i]=(uint16_t)(0x1111+i%7);
}
static int guards_intact(unsigned lane_y){
 for(size_t i=0;i<sizeof(pixels)/sizeof(*pixels);i++){
  size_t y=i/STRIDE,x=i%STRIDE;
  int inside=i<(size_t)STRIDE*268&&y>=lane_y&&y<lane_y+64&&x<536;
  if(!inside&&pixels[i]!=(uint16_t)(0x1111+i%7))return 0;
 }
 return 1;
}
static struct xz_extra_deck deck(void){
 struct xz_extra_deck d;memset(&d,0,sizeof(d));
 d.known=ALL;d.loaded=1;snprintf(d.title,sizeof(d.title),"Fixture");
 d.elapsed_ms=10000;d.total_ms=20000;d.elapsed_at_ms=500;d.playing=1;
 d.bpm_x100=12000;d.track_bpm_x100=12000;d.tempo_x100=0;d.cue_ms=9000;
 d.hot_cue[0]=(struct xz_extra_cue){1,11250,0x30d050,0};
 for(unsigned i=0;i<3000;i++)columns[i]=(struct xz_extra_column){(uint8_t)(60+i%50),40,(uint8_t)(20+i%9)};
 for(unsigned i=0;i<64;i++)grid[i]=(struct xz_extra_beat){(int32_t)(i*500),(uint8_t)(i%4+1),0};
 d.grid=grid;d.grid_count=64;
 d.wave=(struct xz_extra_wave){columns,3000,20000,150};
 return d;
}
static const struct xz_extra_lane_view view={500,XZ_EXTRA_LANE_COLUMN_US,1,0};
static uint16_t at(unsigned lane_y,unsigned r,unsigned x){return pixels[(lane_y+r)*STRIDE+x];}
/* Lane rows 4..9 are beat-tick rows; the playhead covers 268 and 269. */
static int tick_at(unsigned lane_y,unsigned x){return at(lane_y,6,x)==0xce59||at(lane_y,6,x)==0xf800;}

static void sync_label_parity(void){
 for(unsigned number=3;number<=4;number++){
  struct xz_extra_deck d=deck();d.known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_SYNC);
  d.sync.requested=1;d.sync.tempo_matched=1;d.sync.master_deck=1;d.sync.reason=XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,number,&d,&view,&marks));memcpy(copy,pixels,sizeof pixels);
  d.sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,number,&d,&view,&marks));assert(!memcmp(copy,pixels,sizeof pixels));
  d.sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,number,&d,&view,&marks));assert(memcmp(copy,pixels,sizeof pixels));
 }
 for(unsigned number=3;number<=4;number++){
  struct xz_extra_deck d=deck();d.known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_SYNC);
  d.sync.requested=1;d.sync.tempo_matched=1;d.sync.master_deck=1;d.sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,number,&d,&view,&marks));memcpy(copy,pixels,sizeof pixels);
  d.sync.reason=XZ_EXTRA_SYNC_OUT_OF_RANGE;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,number,&d,&view,&marks));assert(guards_intact(0));
  unsigned changed=0;
  for(unsigned r=0;r<64;r++)for(unsigned x=0;x<536;x++)if(at(0,r,x)!=copy[r*STRIDE+x]){
   assert(x<XZ_EXTRA_LANE_READOUT_W&&r<16);changed++;
  }
  assert(changed>0);
 }
 puts("PASS Classic tempo-only, enabled request and rate-limit readouts are distinct on both extra decks");
}
#define K(f) XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f)
#define PLATE XZ_EXTRA_LANE_READOUT_W
/* Lane pixels that differ from copy. Every difference must lie left of xmax in rows r0..r1: a readout change
   never reaches the waveform, the marks or the playhead. */
static unsigned changed_in(unsigned lane_y,unsigned xmax,unsigned r0,unsigned r1){
 unsigned n=0;
 for(unsigned r=0;r<64;r++)for(unsigned x=0;x<536;x++)if(at(lane_y,r,x)!=copy[(lane_y+r)*STRIDE+x]){
  if(x>=xmax||r<r0||r>r1){fprintf(stderr,"unexpected change at x=%u row=%u\n",x,r);assert(0);}
  n++;
 }
 return n;
}
static void draw_lane(const struct xz_extra_deck *d){
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,d,&view,&marks));assert(guards_intact(0));
}
static void keep(const struct xz_extra_deck *d){draw_lane(d);memcpy(copy,pixels,sizeof pixels);}
static struct xz_extra_deck published(void){
 struct xz_extra_deck d=deck();
 d.known=(1u<<XZ_EXTRA_FIELD_COUNT)-1u;d.known&=~K(TIMELINE);
 d.remain=d.quantize=d.vinyl=d.master_tempo=d.auto_cue=1;d.tempo_range=10;d.active_pad=-1;
 snprintf(d.key,sizeof d.key,"8A");d.source=1;snprintf(d.artist,sizeof d.artist,"Artist");
 return d;
}
static void sync_as(struct xz_extra_deck *d,int reason,int locked,int master_deck){
 d->sync=(struct xz_extra_sync){.requested=1,.locked=locked,.master_deck=master_deck,.reason=reason,.tempo_matched=1};
}
static uint32_t lane_hash(void){
 uint32_t h=2166136261u;
 for(unsigned r=0;r<64;r++)for(unsigned x=0;x<536;x++)h=(h^at(0,r,x))*16777619u;
 return h;
}
/* The plate shows every published mode, in the deck's TIME MODE, and nothing it does not know. */
static void plate_modes(void){
 char text[32];
 struct xz_extra_deck d=published();d.master=0;
 /* TIME MODE: remaining with a minus or elapsed, and the REMAIN / TIME label, on lines 2 and 3 only. */
 keep(&d);d.remain=0;draw_lane(&d);assert(changed_in(0,PLATE,15,43)>100);
 int32_t pos;assert(xz_extra_lane_position_ms(&d,view.now_ms,&pos)&&pos==10000);
 /* Unknown TIME MODE: the remaining clock with no label and no A.CUE lamp, whatever the hidden values say. */
 d.known&=~K(TIME_MODE);d.remain=0;d.auto_cue=0;keep(&d);d.remain=1;d.auto_cue=1;draw_lane(&d);assert(!changed_in(0,0,0,0));
 d=published();d.master=0;keep(&d);d.auto_cue=0;draw_lane(&d);assert(changed_in(0,PLATE,30,44)>0);

 /* SYNC classes: every reason the players raise, and the MASTER bit first. */
 static const struct { int reason,locked; enum xz_extra_sync_class klass; } table[]={
  {XZ_EXTRA_SYNC_OFF,0,XZ_EXTRA_SYNC_CLASS_ENABLED},{XZ_EXTRA_SYNC_WAIT_MASTER,0,XZ_EXTRA_SYNC_CLASS_ENABLED},
  {XZ_EXTRA_SYNC_WAIT_ANCHOR,0,XZ_EXTRA_SYNC_CLASS_ENABLED},{XZ_EXTRA_SYNC_NO_GRID,0,XZ_EXTRA_SYNC_CLASS_TEMPO},
  {XZ_EXTRA_SYNC_MANUAL,0,XZ_EXTRA_SYNC_CLASS_ENABLED},{XZ_EXTRA_SYNC_DISCONTINUITY,0,XZ_EXTRA_SYNC_CLASS_ENABLED},
  {XZ_EXTRA_SYNC_LOOP_UNALIGNED,0,XZ_EXTRA_SYNC_CLASS_TEMPO},{XZ_EXTRA_SYNC_OUT_OF_RANGE,0,XZ_EXTRA_SYNC_CLASS_BLOCKED},
  {XZ_EXTRA_SYNC_ACQUIRING,0,XZ_EXTRA_SYNC_CLASS_ENABLED},{XZ_EXTRA_SYNC_LOCKED,1,XZ_EXTRA_SYNC_CLASS_LOCKED},
  {XZ_EXTRA_SYNC_LOCKED,0,XZ_EXTRA_SYNC_CLASS_ENABLED},
  {XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE,0,XZ_EXTRA_SYNC_CLASS_TEMPO},{XZ_EXTRA_SYNC_MASTER_CLOCK,0,XZ_EXTRA_SYNC_CLASS_MASTER},
  {XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED,0,XZ_EXTRA_SYNC_CLASS_BLOCKED},{XZ_EXTRA_SYNC_NO_SOURCE_BPM,0,XZ_EXTRA_SYNC_CLASS_BLOCKED},
  {XZ_EXTRA_SYNC_TEMPO_FOLLOW,0,XZ_EXTRA_SYNC_CLASS_TEMPO}};
 assert(xz_extra_sync_class(NULL)==XZ_EXTRA_SYNC_CLASS_NONE&&!xz_extra_sync_chip(NULL,text,sizeof text)[0]);
 for(size_t i=0;i<sizeof table/sizeof *table;i++){
  sync_as(&d,table[i].reason,table[i].locked,0);assert(xz_extra_sync_class(&d)==table[i].klass);
 }
 d.known&=~K(SYNC);assert(xz_extra_sync_class(&d)==XZ_EXTRA_SYNC_CLASS_NONE);d.known|=K(SYNC);
 d.sync.requested=0;assert(xz_extra_sync_class(&d)==XZ_EXTRA_SYNC_CLASS_NONE);
 d.master=1;d.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;assert(xz_extra_sync_class(&d)==XZ_EXTRA_SYNC_CLASS_MASTER);
 assert(!strcmp(xz_extra_sync_chip(&d,text,sizeof text),"3/4 MASTER"));
 d.master_scope=0;assert(!strcmp(xz_extra_sync_chip(&d,text,sizeof text),"MASTER"));
 d.known&=~K(MASTER);assert(xz_extra_sync_class(&d)==XZ_EXTRA_SYNC_CLASS_NONE);d.known|=K(MASTER);d.master=0;
 static const struct { int reason,locked,master_deck; const char *chip; } chips[]={
  {XZ_EXTRA_SYNC_LOCKED,1,0,"SYNC 1"},{XZ_EXTRA_SYNC_LOCKED,1,3,"SYNC 4"},{XZ_EXTRA_SYNC_LOCKED,1,-1,"SYNC"},
  {XZ_EXTRA_SYNC_TEMPO_FOLLOW,0,1,"BPM 2"},{XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE,0,0,"BPM 1"},
  {XZ_EXTRA_SYNC_WAIT_ANCHOR,0,0,"SYNC 1"},{XZ_EXTRA_SYNC_WAIT_MASTER,0,-1,"SYNC"},
  {XZ_EXTRA_SYNC_NO_GRID,0,2,"BPM 3"},{XZ_EXTRA_SYNC_NO_SOURCE_BPM,0,2,"NO BPM"},
  {XZ_EXTRA_SYNC_OUT_OF_RANGE,0,0,"LIMIT"},{XZ_EXTRA_SYNC_LOOP_UNALIGNED,0,0,"BPM 1"},
  {XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED,0,-1,"EXT MASTER"}};
 for(size_t i=0;i<sizeof chips/sizeof *chips;i++){
  sync_as(&d,chips[i].reason,chips[i].locked,chips[i].master_deck);
  if(strcmp(xz_extra_sync_chip(&d,text,sizeof text),chips[i].chip)){fprintf(stderr,"chip %u: %s\n",(unsigned)i,text);assert(0);}
 }
 sync_as(&d,XZ_EXTRA_SYNC_MASTER_CLOCK,0,2);d.sync.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
 assert(!strcmp(xz_extra_sync_chip(&d,text,sizeof text),"3/4 MASTER"));
 /* Phase acquisition and lock share enabled styling. Established BPM-only cases share BPM styling. */
 d.sync.requested=0;keep(&d);
 uint32_t hash[6];
 sync_as(&d,XZ_EXTRA_SYNC_LOCKED,1,0);draw_lane(&d);assert(changed_in(0,PLATE,0,15));hash[0]=lane_hash();
 sync_as(&d,XZ_EXTRA_SYNC_TEMPO_FOLLOW,0,0);draw_lane(&d);assert(changed_in(0,PLATE,0,15));hash[1]=lane_hash();
 sync_as(&d,XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE,0,0);draw_lane(&d);hash[2]=lane_hash();
 sync_as(&d,XZ_EXTRA_SYNC_WAIT_ANCHOR,0,0);draw_lane(&d);assert(changed_in(0,PLATE,0,15));hash[3]=lane_hash();
 sync_as(&d,XZ_EXTRA_SYNC_ACQUIRING,0,0);draw_lane(&d);hash[4]=lane_hash();
 sync_as(&d,XZ_EXTRA_SYNC_NO_GRID,0,0);draw_lane(&d);assert(changed_in(0,PLATE,0,15));hash[5]=lane_hash();
 assert(hash[1]==hash[2]&&hash[3]==hash[4]);
 assert(hash[0]!=hash[1]&&hash[0]==hash[3]&&hash[5]==hash[1]);
 d.sync.requested=0;d.master=1;d.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;draw_lane(&d);assert(changed_in(0,PLATE,0,15));
 uint32_t software=lane_hash();d.master_scope=0;draw_lane(&d);assert(lane_hash()!=software);d.master=0;

 /* Pads, as the players apply them. */
 static const char *const loops[8]={"1/4","1/2","1","2","4","8","16","32"};
 static const char *const slips[8]={"1/16","1/8","1/4","1/2","1","2","1/3","3/4"};
 static const char *const jumps[8]={"-1","+1","-2","+2","-4","+4","-8","+8"};
 d=published();
 d.pad_mode=XZ_EXTRA_PAD_HOT_CUE;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"HOT CUE"));
 for(int i=0;i<8;i++){char want[2]={(char)('A'+i),0};assert(!strcmp(xz_extra_pad_label(&d,i,text,sizeof text),want));}
 d.pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"BEAT LOOP"));
 for(int i=0;i<8;i++)assert(!strcmp(xz_extra_pad_label(&d,i,text,sizeof text),loops[i]));
 d.active_pad=1;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"BEAT LOOP 1/2"));
 d.pad_mode=XZ_EXTRA_PAD_SLIP_LOOP;d.active_pad=6;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"SLIP LOOP 1/3"));
 for(int i=0;i<8;i++)assert(!strcmp(xz_extra_pad_label(&d,i,text,sizeof text),slips[i]));
 d.active_pad=8;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"SLIP LOOP"));
 d.pad_mode=XZ_EXTRA_PAD_BEAT_JUMP;d.active_pad=-1;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"BEAT JUMP"));
 for(int i=0;i<8;i++)assert(!strcmp(xz_extra_pad_label(&d,i,text,sizeof text),jumps[i]));
 d.pad_mode=XZ_EXTRA_PAD_CALL;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"HOT CUE CALL"));
 struct xz_extra_cue colorless={1,1000,0,0};assert(xz_extra_cue_color(&colorless)==0x00ff00);
 colorless.end_ms=2000;assert(xz_extra_cue_color(&colorless)==0xff8000);
 colorless.rgb=0xabcdef;assert(xz_extra_cue_color(&colorless)==0xabcdef);
 for(int i=0;i<8;i++){char want[2]={(char)('A'+i),0};assert(!strcmp(xz_extra_pad_label(&d,i,text,sizeof text),want));}
 d.pad_mode=XZ_EXTRA_PAD_NONE;assert(!strcmp(xz_extra_pad_text(&d,text,sizeof text),"PADS OFF"));
 for(int i=0;i<8;i++)assert(!xz_extra_pad_label(&d,i,text,sizeof text)[0]);
 assert(!xz_extra_pad_label(&d,8,text,sizeof text)[0]&&!xz_extra_pad_label(&d,-1,text,sizeof text)[0]);
 d.loaded=0;d.pad_mode=XZ_EXTRA_PAD_HOT_CUE;assert(!xz_extra_pad_text(&d,text,sizeof text)[0]&&!xz_extra_pad_label(&d,0,text,sizeof text)[0]);

 /* Each mode, the key, the source and the artist change the plate and never the lane right of it. */
 struct xz_extra_deck base=published();base.master=0;
 for(int change=0;change<17;change++){
  d=base;keep(&d);
  switch(change){
  case 0:d.reverse=1;break;
  case 1:d.slip=1;break;
  case 2:d.slip=d.slip_active=1;break;
  case 3:d.quantize=0;break;
  case 4:d.vinyl=0;break;
  case 5:d.master_tempo=0;break;
  case 6:d.tempo_reset=1;d.tempo_x100=0;break;
  case 7:d.tempo_range=100;break;
  case 8:d.tempo_range=16;break;
  case 9:d.pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;break;
  case 10:d.pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;d.active_pad=4;break;
  case 11:d.pad_mode=XZ_EXTRA_PAD_NONE;break;
  case 12:snprintf(d.key,sizeof d.key,"11B");break;
  case 13:d.source=2;break;
  case 14:snprintf(d.artist,sizeof d.artist,"Someone Else");break;
  case 15:d.pad_mode=XZ_EXTRA_PAD_BEAT_JUMP;break;
  default:d.tempo_x100=-333;break;
  }
  draw_lane(&d);
  if(!changed_in(0,PLATE,0,63)){fprintf(stderr,"mode change %d drew nothing\n",change);assert(0);}
 }
 /* Never a guess: with their bits clear, hidden key, source, artist, range, QUANTIZE, VINYL and MT draw nothing. */
 d=base;d.known&=~(K(KEY)|K(SOURCE)|K(ARTIST)|K(TEMPO_RANGE)|K(QUANTIZE)|K(VINYL)|K(MASTER_TEMPO));keep(&d);
 snprintf(d.key,sizeof d.key,"3B");d.source=2;snprintf(d.artist,sizeof d.artist,"Hidden");d.tempo_range=100;
 d.quantize=!d.quantize;d.vinyl=!d.vinyl;d.master_tempo=!d.master_tempo;draw_lane(&d);assert(!changed_in(0,0,0,0));

 /* LOOP IN / OUT adjust: the edge the jog moves is drawn wide in yellow; a stored loop keeps dim ends. */
 unsigned in_x=268+(unsigned)(200000/XZ_EXTRA_LANE_COLUMN_US),out_x=268+(unsigned)(800000/XZ_EXTRA_LANE_COLUMN_US)-1;
 d=deck();d.loop.active=d.loop.stored=1;d.loop.in_ms=10200;d.loop.out_ms=10800;
 draw_lane(&d);assert(at(0,20,in_x)==0xfd20);
 d.loop.edit=1;draw_lane(&d);for(unsigned r=0;r<64;r+=7)assert(at(0,r,in_x)==0xffe0);assert(at(0,20,out_x)!=0xffe0);
 d.loop.edit=2;draw_lane(&d);assert(at(0,20,out_x)==0xffe0&&at(0,20,in_x)!=0xffe0);
 d.loop.active=0;d.loop.edit=0;draw_lane(&d);assert(at(0,20,in_x)==0x8c40&&at(0,20,out_x)==0x8c40&&at(0,20,in_x+30)!=0xb300);
 d.loop.stored=0;draw_lane(&d);assert(at(0,20,in_x)!=0x8c40);
 d.loop.stored=1;d.known&=~K(LOOP);draw_lane(&d);assert(at(0,20,in_x)!=0x8c40);

 /* Native DECK 1 / 2 tags: only the tag's pixels, lit and unlit apart, guarded. */
 fill();memcpy(copy,pixels,sizeof pixels);
 assert(xz_four_badge_draw(pixels,STRIDE,STRIDE*268,68,1,1));
 unsigned tag=0;
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)if(pixels[i]!=copy[i]){
  size_t x=i%STRIDE,y=i/STRIDE;assert(x>=3&&x<60&&y>=68+2&&y<68+15);tag++;
 }
 assert(tag>100);
 /* Both native decks wear the grey badge (deck_colour.h); the DECK chip beside it is what lights. */
 uint16_t lit_pixel=at(68,3,19),badge=at(68,3,4);
 fill();assert(xz_four_badge_draw(pixels,STRIDE,STRIDE*268,68,2,0));assert(at(68,3,19)!=lit_pixel&&at(68,3,4)==badge);
 fill();memcpy(copy,pixels,sizeof pixels);
 assert(!xz_four_badge_draw(pixels,STRIDE,STRIDE*268,68,3,1)&&!xz_four_badge_draw(pixels,STRIDE,STRIDE*268,205,1,1)&&
        !xz_four_badge_draw(pixels,STRIDE,STRIDE*100,68,1,1)&&!xz_four_badge_draw(NULL,STRIDE,STRIDE*268,0,1,1)&&
        !xz_four_badge_draw(pixels,530,STRIDE*268,0,1,1));
 assert(!memcmp(copy,pixels,sizeof pixels));
 puts("PASS Classic plate: TIME MODE clock and label, every SYNC reason class and chip, 3/4 vs native MASTER scope, "
      "pad tables per mode, REV/SLIP/Q/VINYL/MT/RESET/range/key/source/artist inside the plate only, hidden fields blank, "
      "LOOP IN/OUT adjust and stored loop marks, native deck tags");
}

/* 2 DECKS: a side on DECK 3 or 4 fills that side's half with a whole native lane, without a tag. */
static void two_deck_halves(void){
 static uint16_t s[536*268],before[536*268],drawn[536*268];static uint8_t keep[XZ_FOUR_KEEP_BYTES];
 static struct xz_extra_lane_marks halves[2];
 struct xz_extra_deck d=deck();
 for(unsigned i=0;i<3000;i++)columns[i]=(struct xz_extra_column){(uint8_t)(60+i%50),40,(uint8_t)(20+i%9)};
 /* The full lane: guards, native rows one to one (ticks in rows 4..13 and 118..127, the cue triangle in the bottom
    six rows, the playhead through all 132), and no tag in the top-left corner. */
 for(unsigned y=0;y<=136;y+=136){
  for(unsigned i=0;i<536*268;i++)s[i]=(uint16_t)(0x1111+i%7);
  assert(xz_extra_lane_draw_full(s,536,536*268,y,3,&d,&view,&halves[0]));
  for(unsigned i=0;i<536*268;i++)if(i/536<y||i/536>=y+132)assert(s[i]==(uint16_t)(0x1111+i%7));
  for(unsigned r=0;r<132;r++)assert(s[(y+r)*536+268]==0xf800&&s[(y+r)*536+269]==0xf800);
  unsigned spacing=(unsigned)(500000/XZ_EXTRA_LANE_COLUMN_US),tx=268+spacing;
  uint16_t t=s[(y+6)*536+tx]==0xce59||s[(y+6)*536+tx]==0xf800?s[(y+6)*536+tx]:s[(y+6)*536+tx+1];
  assert(t==0xce59||t==0xf800);
  unsigned x=s[(y+6)*536+tx]==t?tx:tx+1;
  for(unsigned r=4;r<=13;r++)assert(s[(y+r)*536+x]==t);
  for(unsigned r=118;r<=127;r++)assert(s[(y+r)*536+x]==t);
  assert(s[(y+65)*536+400]==0xffff&&s[(y+20)*536+x]!=t);
  unsigned cue=268-(unsigned)((1000000+XZ_EXTRA_LANE_COLUMN_US-1)/XZ_EXTRA_LANE_COLUMN_US);
  assert(s[(y+131)*536+cue]==0xfc60&&s[(y+126)*536+cue]==0xfc60);
  for(unsigned r=0;r<16;r++)for(unsigned cx=0;cx<60;cx++)assert(s[(y+r)*536+cx]!=0xebf2&&s[(y+r)*536+cx]!=0x9e5f&&s[(y+r)*536+cx]!=0x1926);
  assert(!xz_extra_lane_draw_full(s,536,536*268,137,3,&d,&view,NULL));
  assert(!xz_extra_lane_draw_full(s,536,536*267,136,3,&d,&view,NULL));
 }
 /* Unloaded: NO TRACK centred in the whole lane. */
 struct xz_extra_deck empty=deck();empty.loaded=0;
 memset(s,0,sizeof s);assert(xz_extra_lane_draw_full(s,536,536*268,0,4,&empty,&view,NULL));
 unsigned ink=0,left=536,right=0;
 for(unsigned r=0;r<132;r++)for(unsigned x=0;x<536;x++)if(s[r*536+x]){ink++;if(x<left)left=x;if(x>right)right=x;}
 assert(ink>50&&left>200&&right<336&&left+right>500&&left+right<572);
 /* The frame: each pair writes exactly its halves; the right half's separator rows go black. */
 struct xz_extra_decks x;memset(&x,0,sizeof x);x.route_up=1;x.deck[0]=d;x.deck[1]=d;
 for(unsigned pair=0;pair<4;pair++){
  for(unsigned i=0;i<536*268;i++)s[i]=before[i]=(uint16_t)(0x2222+i%5);
  struct xz_two_frame f={&x,pair,500,XZ_EXTRA_LANE_COLUMN_US,1};
  assert(xz_two_frame_draw(s,536*268,&f,halves)==(pair!=0));
  for(unsigned i=0;i<536*268;i++){
   unsigned row=i/536;int side=row>=132;
   if(!(pair&(1u<<side)))assert(s[i]==before[i]);
   else if(side&&row<136)assert(s[i]==0);
  }
  for(unsigned side=0;side<2;side++)if(!(pair&(1u<<side)))for(unsigned c=0;c<536;c++)assert(!halves[side].col[c].valid);
 }
 struct xz_two_frame bad={&x,4,500,XZ_EXTRA_LANE_COLUMN_US,1};
 assert(!xz_two_frame_draw(s,536*268,&bad,halves));bad.pair=1;bad.decks=NULL;assert(!xz_two_frame_draw(s,536*268,&bad,halves));
 bad.decks=&x;assert(!xz_two_frame_draw(s,536*268-1,&bad,halves));
 /* Theme pass over both halves: layer pixels exact, everything else themed. */
 struct xz_two_frame both={&x,3,500,XZ_EXTRA_LANE_COLUMN_US,1};
 memset(s,0,sizeof s);assert(xz_two_frame_draw(s,536*268,&both,halves));memcpy(drawn,s,sizeof drawn);
 xz_two_protect(3,halves,s,keep);for(unsigned i=0;i<536*268;i++)s[i]=(uint16_t)~s[i];xz_two_restore(3,halves,s,keep);
 unsigned kept[2]={0,0},themed=0;
 for(unsigned i=0;i<536*268;i++){
  if(keep[i>>3]&(1u<<(i&7))){assert(s[i]==drawn[i]);kept[i/536>=132]++;}
  else{assert(s[i]==(uint16_t)~drawn[i]);themed++;}
 }
 assert(kept[0]>20000&&kept[1]>20000&&themed>10000);
 assert(!(keep[(65*536+268)>>3]&(1u<<((65*536+268)&7))));
 /* A side not in pair keeps nothing, though its marks are still there. */
 xz_two_protect(1,halves,drawn,keep);
 for(unsigned i=132*536;i<536*268;i++)assert(!(keep[i>>3]&(1u<<(i&7))));
 xz_two_protect(3,NULL,drawn,keep);for(unsigned i=0;i<XZ_FOUR_KEEP_BYTES;i++)assert(!keep[i]);
 xz_two_restore(3,halves,NULL,keep);xz_two_protect(3,halves,drawn,NULL);
 puts("PASS 2 DECKS halves: whole native lane rows one to one, no tag, NO TRACK centred, exact half writes per pair, black separator, theme keep per side");
}
int main(void){
 sync_label_parity();
 two_deck_halves();
 struct xz_extra_deck d=deck();int32_t pos;
 assert(xz_extra_lane_position_ms(&d,500,&pos)&&pos==10000);
 assert(xz_extra_lane_position_ms(&d,900,&pos)&&pos==10400);
 d.tempo_x100=1000;assert(xz_extra_lane_position_ms(&d,1500,&pos)&&pos==11100);
 assert(xz_extra_lane_position_ms(&d,60000,&pos)&&pos==11100);
 assert(xz_extra_lane_position_ms(&d,100,&pos)&&pos==10000);
 d.elapsed_ms=19900;assert(xz_extra_lane_position_ms(&d,1500,&pos)&&pos==20000);
 d.playing=0;assert(xz_extra_lane_position_ms(&d,1500,&pos)&&pos==19900);
 d.known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED);assert(!xz_extra_lane_position_ms(&d,1500,&pos));
 assert(!xz_extra_lane_position_ms(NULL,0,&pos));
 d=deck();d.elapsed_ms=10900;d.loop.active=1;d.loop.in_ms=10000;d.loop.out_ms=11000;
 assert(xz_extra_lane_position_ms(&d,900,&pos)&&pos==10300);

 d=deck();fill();
 assert(!xz_extra_lane_draw(NULL,STRIDE,STRIDE*268,0,3,&d,&view,&marks));
 assert(!xz_extra_lane_draw(pixels,530,STRIDE*268,0,3,&d,&view,&marks));
 assert(!xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,205,3,&d,&view,&marks));
 assert(!xz_extra_lane_draw(pixels,STRIDE,STRIDE*100,68,3,&d,&view,&marks));
 assert(!xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,2,&d,&view,&marks));
 assert(!xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,5,&d,&view,&marks));
 assert(!xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,NULL,&marks));
 assert(!xz_extra_lane_draw(pixels,SIZE_MAX,SIZE_MAX,0,3,&d,&view,&marks));
 assert(guards_intact(999));

 for(unsigned lane_y=0;lane_y<=204;lane_y+=68){
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,lane_y,4,&d,&view,&marks));
  assert(guards_intact(lane_y));
 }
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*(68+64),68,3,&d,&view,&marks));assert(guards_intact(68));

 /* Full data: red playhead while playing, layers around the centre, ticks every 500 ms. */
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,3,&d,&view,&marks));
 for(unsigned r=0;r<64;r++)assert(at(68,r,268)==0xf800&&at(68,r,269)==0xf800);
 unsigned valid=0;for(unsigned x=0;x<536;x++)valid+=marks.col[x].valid;
 assert(valid==536&&marks.look.kind==XZ_LAYERED_THREE_BAND&&!marks.look.bands);
 assert(at(68,32,400)==0xffff);
 unsigned spacing=(unsigned)(500000/XZ_EXTRA_LANE_COLUMN_US);
 assert(tick_at(68,268+spacing)||tick_at(68,268+spacing+1));
 assert(tick_at(68,268+2*spacing)||tick_at(68,268+2*spacing+1));
 assert(at(68,6,268+spacing/2)==0);
 struct xz_extra_lane_view wide=view;wide.column_us=2*XZ_EXTRA_LANE_COLUMN_US;
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,3,&d,&wide,&marks));
 assert(tick_at(68,268+spacing/2)||tick_at(68,268+spacing/2+1));

 /* Paused: white playhead. Loop: tinted body background inside the loop. */
 d.playing=0;d.loop.active=1;d.loop.in_ms=10200;d.loop.out_ms=10800;
 for(unsigned i=0;i<3000;i++)columns[i]=(struct xz_extra_column){4,2,1};
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));
 assert(at(0,20,268)==0xffff);
 unsigned in_loop=268+(unsigned)(300000/XZ_EXTRA_LANE_COLUMN_US),before=200;
 assert(at(0,20,in_loop)==0xb300&&at(0,20,before)!=0xb300);
 d.known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_LOOP);
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));assert(at(0,20,in_loop)!=0xb300);

 /* NULL wave and unknown wave draw no layers; NULL deck and unloaded draw identically. */
 d=deck();d.wave.columns=NULL;
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));
 for(unsigned x=0;x<536;x++)assert(!marks.col[x].valid);
 d=deck();d.known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));
 for(unsigned x=0;x<536;x++)assert(!marks.col[x].valid);
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,136,4,NULL,&view,NULL));memcpy(copy,pixels,sizeof(pixels));
 d=deck();d.loaded=0;
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,136,4,&d,&view,&marks));
 assert(!memcmp(copy,pixels,sizeof(pixels)));
 unsigned lit=0;for(unsigned r=0;r<64;r++)for(unsigned x=XZ_EXTRA_LANE_READOUT_W;x<536;x++)lit+=at(136,r,x)!=0;
 assert(lit>100);
 for(unsigned r=0;r<64;r++)assert(at(136,r,268)!=0xffff&&at(136,r,268)!=0xf800);

 /* Unknown readouts never show the hidden values: any value with a clear bit draws the same. */
 d=deck();d.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED);
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));memcpy(copy,pixels,sizeof(pixels));
 d.total_ms=99999;d.bpm_x100=17777;d.tempo_x100=-555;d.master=1;snprintf(d.title,sizeof(d.title),"Hidden");
 d.cue_ms=10100;d.hot_cue[1]=(struct xz_extra_cue){1,10200,0xff0000,0};d.loop.active=1;d.loop.in_ms=9000;d.loop.out_ms=11000;
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));
 assert(!memcmp(copy,pixels,sizeof(pixels)));
 /* The lit tag changes only the readout. */
 d=deck();fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));memcpy(copy,pixels,sizeof(pixels));
 struct xz_extra_lane_view on=view;on.lit=1;
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&on,&marks));
 unsigned changed=0;
 for(unsigned r=0;r<64;r++)for(unsigned x=0;x<536;x++)if(at(0,r,x)!=copy[r*STRIDE+x]){assert(x<XZ_EXTRA_LANE_READOUT_W&&r<16);changed++;}
 assert(changed>50);

 /* Sync status changes the readout only; it must not align or move the waveform. */
 d=deck();d.master=0;d.known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_SYNC);
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));memcpy(copy,pixels,sizeof pixels);
 uint32_t status_hash[3]={0};
 for(int state=0;state<3;state++){
  d.sync=(struct xz_extra_sync){.requested=1,.locked=state==0,.master_deck=state==1?0:3,
   .reason=state==0?XZ_EXTRA_SYNC_LOCKED:state==1?XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE:XZ_EXTRA_SYNC_WAIT_MASTER};
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));assert(guards_intact(0));
  unsigned differences=0;uint32_t hash=2166136261u;
  for(unsigned r=0;r<64;r++)for(unsigned x=0;x<536;x++){
   uint16_t pixel=at(0,r,x);hash=(hash^pixel)*16777619u;
   if(pixel!=copy[r*STRIDE+x]){assert(x<XZ_EXTRA_LANE_READOUT_W&&r<16);differences++;}
  }
  assert(differences>0);status_hash[state]=hash;
 }
 assert(status_hash[0]!=status_hash[1]&&status_hash[1]!=status_hash[2]&&status_hash[0]!=status_hash[2]);
 d.master=1;d.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
 fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,0,3,&d,&view,&marks));assert(guards_intact(0));
 puts("PASS Classic SYNC/BPM/armed status and 3/4 master bounds; status cannot move waveforms");

 /* Theme pass: layer pixels come back exactly; background, marks and readout stay themed. */
 static uint16_t surface[536*268];static uint8_t keep[XZ_FOUR_KEEP_BYTES];
 static struct xz_extra_lane_marks lanes[2];
 struct xz_four_layout g;assert(xz_four_layout_get(XZ_FOUR_INTERLEAVED,&g));
 memset(surface,0,sizeof(surface));d=deck();
 for(unsigned i=0;i<3000;i++)columns[i]=(struct xz_extra_column){(uint8_t)(60+i%50),40,(uint8_t)(20+i%9)};
 assert(xz_extra_lane_draw(surface,536,536*268,g.lane_y[2],3,&d,&view,&lanes[0]));
 assert(xz_extra_lane_draw(surface,536,536*268,g.lane_y[3],4,&d,&view,&lanes[1]));
 struct xz_four_marks m={0};m.look[2]=lanes[0].look;m.look[3]=lanes[1].look;m.col[2]=lanes[0].col;m.col[3]=lanes[1].col;
 static uint16_t drawn[536*268];memcpy(drawn,surface,sizeof(drawn));
 xz_four_protect(&g,&m,surface,keep);
 for(unsigned i=0;i<536*268;i++)surface[i]=(uint16_t)~surface[i];
 xz_four_restore(&g,&m,surface,keep);
 unsigned kept=0,themed=0;
 for(unsigned i=0;i<536*268;i++){
  if(keep[i>>3]&(1u<<(i&7))){assert(surface[i]==drawn[i]);kept++;}
  else{assert(surface[i]==(uint16_t)~drawn[i]);themed++;}
 }
 assert(kept>10000&&themed>kept);
 unsigned y=g.lane_y[2]+32;assert(!(keep[(y*536+268)>>3]&(1u<<((y*536+268)&7))));
 assert(!(keep[(y*536+10)>>3]&(1u<<((y*536+10)&7))));
 xz_four_protect(&g,NULL,surface,keep);for(unsigned i=0;i<XZ_FOUR_KEEP_BYTES;i++)assert(!keep[i]);
 xz_four_restore(&g,&m,NULL,keep);xz_four_protect(&g,&m,surface,NULL);
 /* Notices survive a route loss, expire, and only touch their bounded banner. */
 struct xz_extra_decks notice={0};
 snprintf(notice.notice,sizeof notice.notice,"DECK 3/4 ROUTE LOST");
 notice.notice_at_ms=100;notice.notice_seq=1;
 fill();memcpy(copy,pixels,sizeof pixels);
 assert(xz_extra_notice_active(&notice,100));
 assert(xz_extra_notice_draw(pixels,STRIDE,STRIDE*268,&notice,100));
 unsigned changes=0;
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)if(pixels[i]!=copy[i]){
  size_t x=i%STRIDE,y=i/STRIDE;
  assert(x>=6&&x<530&&y>=244&&y<266);changes++;
 }
 assert(changes>500);
 assert(!xz_extra_notice_active(&notice,3600));
 fill();memcpy(copy,pixels,sizeof pixels);
 assert(!xz_extra_notice_draw(pixels,STRIDE,STRIDE*268,&notice,3600));
 assert(!memcmp(copy,pixels,sizeof pixels));
 assert(!xz_extra_notice_draw(pixels,STRIDE,STRIDE*268-1,&notice,100));
 notice.notice_at_ms=UINT32_MAX-30;
 assert(xz_extra_notice_active(&notice,20));
 memset(notice.notice,'X',sizeof notice.notice);
 assert(xz_extra_notice_draw(pixels,STRIDE,STRIDE*268,&notice,20));
 plate_modes();
 puts("PASS extra-deck lane: position extrapolation, guards and lane-only writes, playhead, zoom-scaled ticks, loop tint, NULL/unknown wave, unloaded, blank readouts, lit tag, theme protect/restore");
}
