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
 for(unsigned i=0;i<64;i++)grid[i]=(struct xz_extra_beat){(int32_t)(i*500),(uint8_t)(i%4+1)};
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
  d.sync.requested=1;d.sync.master_deck=1;d.sync.reason=XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,number,&d,&view,&marks));memcpy(copy,pixels,sizeof pixels);
  d.sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,number,&d,&view,&marks));assert(!memcmp(copy,pixels,sizeof pixels));
  d.sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;
  fill();assert(xz_extra_lane_draw(pixels,STRIDE,STRIDE*268,68,number,&d,&view,&marks));assert(memcmp(copy,pixels,sizeof pixels));
 }
 puts("PASS Classic tempo following is BPM; unavailable authority remains WAIT on both extra decks");
}
int main(void){
 sync_label_parity();
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
 puts("PASS Classic SYNC/BPM/WAIT status and3/4master bounds; changingstatus cannotmove waveforms");

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
 puts("PASS extra-deck lane: position extrapolation, guards and lane-only writes, playhead, zoom-scaled ticks, loop tint, NULL/unknown wave, unloaded, blank readouts, lit tag, theme protect/restore");
}
