/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Jog display runtime tests require assertions
#endif
#include "../jog_display_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int xz_hook_arm(uint32_t address,const unsigned char expected[8],void *replacement,void **original){
 (void)address;(void)expected;(void)replacement;(void)original;return -1;
}
uint32_t xz_extra_decks_control_revision(void){return 0;}

static uint64_t sim_now_ms;
static uint32_t sim_ctrl;
static uint16_t sim_seq[2];
static uint8_t sim_next_pkt[2][64];
static void (*assemble_hook)(void *);

static void swap64(uint8_t *out,const uint8_t *in){
 for(unsigned i=0;i<64;i++)out[i]=in[i^3u];
}
static void set16(uint8_t *p,unsigned n){
 p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);
}
static unsigned get16(const uint8_t *p){
 return p[0]|((unsigned)p[1]<<8);
}
static uint32_t get32(const uint8_t *p){
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static void fake_orig_assemble(void *self){
 uint32_t ch=0;memcpy(&ch,(const uint8_t *)self+0x64,4);
 unsigned idx=ch-1u;
 uint8_t *f=(uint8_t *)self+0x104;
 memset(f,0,XZ_JD_FRAME);
 f[0]=0xaa;f[3]=0xff;f[4]=0xff;f[5]=0xff;f[0x4e]=0x40;
 f[0x53]=(uint8_t)(ch<<4);
 set16(f+8,++sim_seq[idx]);
 swap64(f+12,sim_next_pkt[idx]);
 uint32_t crc=xz_jd_crc(f);
 memcpy(f+144,&crc,4);
}
static uint64_t fake_now_ms(void){return sim_now_ms;}
static uint32_t fake_ctrl(void){return sim_ctrl;}

static struct xz_extra_deck make_extra_deck(uint32_t at_ms,uint64_t source_frame,int32_t rate_ppm,int playing){
 struct xz_extra_deck d;memset(&d,0,sizeof d);
 d.loaded=1;d.playing=playing;d.total_ms=180000;d.elapsed_ms=30000;d.elapsed_at_ms=at_ms;
 d.tempo_x100=-250;d.tempo_range=10;d.vinyl=1;d.master_tempo=1;
 d.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL);
 d.timeline.source_frame=source_frame;
 d.timeline.source_rate=44100;
 d.timeline.at_ms=at_ms;
 d.timeline.route_epoch=3;
 d.timeline.load_revision=7;
 d.timeline.transport_generation=2;
 d.timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE|XZ_EXTRA_TIMELINE_SINK_FRAME|XZ_EXTRA_TIMELINE_MOTION;
 d.timeline.rate_ppm=rate_ppm;
 d.timeline.moving=playing;
 return d;
}

int main(void){
 sim_now_ms=10000;sim_ctrl=8u|4u; /* AVAILABLE | ROUTE, native sides */
 sim_seq[0]=sim_seq[1]=100;
 struct xz_jog_display_test_ops ops={fake_orig_assemble,fake_now_ms,fake_ctrl};
 xz_jog_display_runtime_test_install(&ops,&assemble_hook);
 assert(assemble_hook);

 unsigned char jog_left[0x200];memset(jog_left,0,sizeof jog_left);
 uint32_t ch1=1;memcpy(jog_left+0x64,&ch1,4);
 uint8_t *frame=jog_left+0x104;

 /* Feed a native wave packet (1/1) and native info packet on Deck 1. */
 memset(sim_next_pkt[0],0,64);
 sim_next_pkt[0][0]=0x10;sim_next_pkt[0][1]=0x2c;sim_next_pkt[0][2]=1;sim_next_pkt[0][4]=1;sim_next_pkt[0][6]=0x55;
 assemble_hook(jog_left);
 assert(xz_jog_display_runtime_state(0)->phase==XZ_JD_NATIVE);

 sim_next_pkt[0][0]=0x10;sim_next_pkt[0][1]=0x21;sim_next_pkt[0][2]=0x08;sim_next_pkt[0][11]=1;
 assemble_hook(jog_left);

 /* Publish Deck 3 state and switch Left side to Deck 3 (ctrl |= 1u). */
 struct xz_extra_deck d3=make_extra_deck(10000,44100*30,1000000,1);
 xz_jog_display_runtime_publish_deck(0,&d3,10000);
 sim_ctrl|=1u;

 /* Next 5 ticks emit EXTRA_CLEAR packets (families 1..5) for logical deck 3 (0x30). */
 static const uint8_t exp_clear_cmd[5]={0x2d,0x30,0x3f,0x2c,0x33};
 for(unsigned i=0;i<5;i++){
  uint8_t pkt[64];
  assemble_hook(jog_left);
  swap64(pkt,frame+12);
  assert(pkt[0]==0x30&&pkt[1]==exp_clear_cmd[i]);
  assert((frame[0x53]>>4)==3&&frame[0]==0&&get32(frame+144)==xz_jd_crc(frame));
 }
 /* Tick 6 emits live Deck 3 INFO packet with extrapolated audio timeline position. */
 sim_now_ms=10050; /* 50 ms after anchor at 1.0x -> 30050 ms (0m 30s 50ms) */
 assemble_hook(jog_left);
 uint8_t pkt[64];swap64(pkt,frame+12);
 assert(xz_jog_display_runtime_state(0)->phase==XZ_JD_EXTRA);
 assert(pkt[0]==0x30&&pkt[1]==0x21&&pkt[2]==0x08);
 assert(pkt[11]==0&&pkt[12]==30&&get16(pkt+13)==50);
 assert((int16_t)get16(pkt+0x17)==-250);
 assert(get32(frame+144)==xz_jd_crc(frame));

 /* Stale timeline (> 250 ms without update while playing) clears and marks unavailable. */
 sim_now_ms=10400;
 for(unsigned i=0;i<5;i++)assemble_hook(jog_left);
 assemble_hook(jog_left);
 swap64(pkt,frame+12);
 assert(pkt[0]==0x30&&pkt[1]==0x21&&pkt[0x13]==0&&pkt[0x14]==0);

 /* Switch Left side back to Native Deck 1 (ctrl &= ~1u): 5 native clear packets + replay -> NATIVE. */
 sim_ctrl&=~1u;
 for(unsigned i=0;i<5;i++){
  assemble_hook(jog_left);
  swap64(pkt,frame+12);
  assert(pkt[0]==0x10&&pkt[1]==exp_clear_cmd[i]);
  assert((frame[0x53]>>4)==1&&get32(frame+144)==xz_jd_crc(frame));
 }
 for(unsigned i=0;i<12&&xz_jog_display_runtime_state(0)->phase!=XZ_JD_NATIVE;i++){
  assemble_hook(jog_left);
  assert(get32(frame+144)==xz_jd_crc(frame));
 }
 assert(xz_jog_display_runtime_state(0)->phase==XZ_JD_NATIVE);
 puts("PASS runtime Jog LCD bridge: lock-free Deck 3/4 timeline projection, EXTRA_CLEAR/EXTRA, stale guard, and NATIVE_CLEAR/REPLAY restoration");
 return 0;
}
