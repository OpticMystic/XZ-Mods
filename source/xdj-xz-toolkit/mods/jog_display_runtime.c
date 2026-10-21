/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <unistd.h>
#include <time.h>
#include "jog_display_runtime.h"
#include "runtime.h"
#include "clock_compat.h"
#include <stdatomic.h>
#include <string.h>

#define XZ_JD_RUNTIME_FRESH_MS 250u

static uint8_t jd_storage[XZ_EXTRA_DECKS][XZ_JD_STORAGE_BYTES];
static struct xz_jd jd_state[XZ_EXTRA_DECKS];
static _Atomic int jd_installed;

struct jd_pub_slot {
 _Atomic uint32_t seq;
 struct xz_extra_deck deck[2];
};
static struct jd_pub_slot jd_pub[XZ_EXTRA_DECKS];

static void (*orig_jog_assemble)(void *);
static uint64_t (*test_now_ms)(void);
static uint32_t (*test_control_revision)(void);

static uint64_t default_now_ms(void){
 if(test_now_ms)return test_now_ms();
 struct timespec t;
#ifdef __linux__
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
#else
 timespec_get(&t,TIME_UTC);
#endif
 return (uint64_t)t.tv_sec*1000u+(uint64_t)(t.tv_nsec/1000000L);
}

static uint32_t current_control_revision(void){
 if(test_control_revision)return test_control_revision();
 return xz_extra_decks_control_revision();
}

void xz_jog_display_runtime_publish_deck(int deck_index,const struct xz_extra_deck *deck,uint32_t now_ms){
 (void)now_ms;
 if(deck_index<0||deck_index>=XZ_EXTRA_DECKS||!deck)return;
 struct jd_pub_slot *s=&jd_pub[deck_index];
 uint32_t seq=atomic_load_explicit(&s->seq,memory_order_relaxed);
 uint32_t next=seq+1u;
 s->deck[next&1u]=*deck;
 atomic_store_explicit(&s->seq,next,memory_order_release);
}

void xz_jog_display_runtime_publish(const struct xz_extra_decks *decks,uint32_t now_ms){
 if(!decks)return;
 for(int i=0;i<XZ_EXTRA_DECKS;i++)xz_jog_display_runtime_publish_deck(i,&decks->deck[i],now_ms);
}

static int read_published_deck(int deck_index,struct xz_extra_deck *out){
 if(deck_index<0||deck_index>=XZ_EXTRA_DECKS||!out)return 0;
 struct jd_pub_slot *s=&jd_pub[deck_index];
 for(unsigned tries=0;tries<4;tries++){
  uint32_t s1=atomic_load_explicit(&s->seq,memory_order_acquire);
  if(!s1)return 0;
  *out=s->deck[s1&1u];
  uint32_t s2=atomic_load_explicit(&s->seq,memory_order_acquire);
  if(s1==s2||s2-s1<=1u)return 1;
 }
 return 0;
}

int xz_jog_display_snapshot_from_deck(int deck_index,const struct xz_extra_deck *d,uint64_t now_ms,struct xz_jd_snapshot *out){
 if(!d||!out||deck_index<0||deck_index>=XZ_EXTRA_DECKS)return 0;
 memset(out,0,sizeof *out);
 if(!d->loaded||d->total_ms<=0||(uint32_t)d->total_ms>=128u*60000u)return 0;
 uint32_t length_ms=(uint32_t)d->total_ms;
 uint32_t pos_ms=d->elapsed_ms>0?(uint32_t)d->elapsed_ms:0u;
 uint64_t cap_ms=now_ms;
 if((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE))&&(d->timeline.flags&XZ_EXTRA_TIMELINE_ANCHOR)&&d->timeline.source_rate){
  uint32_t now32=(uint32_t)now_ms;
  uint32_t age32=now32-d->timeline.at_ms;
  if((int32_t)age32<0||age32>XZ_JD_RUNTIME_FRESH_MS){
   if(!d->playing)age32=0;
   else return 0;
  }
  double base_ms=(double)d->timeline.source_frame*1000./(double)d->timeline.source_rate;
  if(d->timeline.moving&&(d->timeline.flags&XZ_EXTRA_TIMELINE_RATE)){
   base_ms+=(double)age32*((double)d->timeline.rate_ppm/1000000.);
  }
  if(d->loop.active&&d->loop.out_ms>d->loop.in_ms&&base_ms>=(double)d->loop.out_ms){
   double span=(double)(d->loop.out_ms-d->loop.in_ms);
   double rel=base_ms-(double)d->loop.in_ms;
   while(rel>=span)rel-=span;
   base_ms=(double)d->loop.in_ms+rel;
  }
  if(base_ms<0.)base_ms=0.;
  if(base_ms>(double)length_ms)base_ms=(double)length_ms;
  pos_ms=(uint32_t)(base_ms+.5);
  cap_ms=d->playing&&age32<=XZ_JD_RUNTIME_FRESH_MS?now_ms-age32:now_ms;
 }
 if(pos_ms>length_ms)pos_ms=length_ms;
 uint8_t range=(uint8_t)d->tempo_range;
 if(range!=0&&range!=6&&range!=10&&range!=16&&range!=100)range=10;
 int32_t tempo=d->tempo_x100;
 if(tempo<-32768)tempo=-32768;
 if(tempo>32767)tempo=32767;
 uint64_t gen=((uint64_t)(d->timeline.route_epoch?d->timeline.route_epoch:1u)<<32)|
              (uint64_t)(d->timeline.load_revision?d->timeline.load_revision:1u);
 *out=(struct xz_jd_snapshot){
  .generation=gen,
  .captured_ms=cap_ms,
  .position_ms=pos_ms,
  .length_ms=length_ms,
  .logical_deck=(uint8_t)(deck_index+3),
  .paused=(uint8_t)!d->playing,
  .tempo_rate=(int16_t)tempo,
  .tempo_range=range,
  .vinyl_mode=(uint8_t)!!d->vinyl,
  .master_tempo=(uint8_t)!!d->master_tempo,
  .tempo_known=1
 };
 return 1;
}

static void prime_side_baseline(unsigned side_idx){
 unsigned side=side_idx+1u;
 struct xz_jd *s=&jd_state[side_idx];
 xz_jd_init(s,side,jd_storage[side_idx],XZ_JD_STORAGE_BYTES,XZ_JD_RUNTIME_FRESH_MS);
 xz_jd_native_epoch(s,1);
 static const uint8_t neutral_info[64]={
  0x10,0x21,0x0a,0x00,0x20,0x41,0x01,0x00,0x00,0x5e,0x80,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0x00,0x00,0x00,0x00,0x99,0x95,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x99,
  0x59,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0d,0x00,0x00
 };
 uint8_t pkt[64];
 memcpy(pkt,neutral_info,64);
 pkt[0]=(uint8_t)(side<<4);
 (void)xz_jd_capture(s,pkt);
 for(unsigned fam=1;fam<=5;fam++){
  xz_jd_clear_packet(side,fam,pkt);
  (void)xz_jd_capture(s,pkt);
 }
}

static void hook_jog_assemble(void *self){
 if(orig_jog_assemble)orig_jog_assemble(self);
 if(!atomic_load(&jd_installed)||!self)return;
 uint32_t channel=0;
 memcpy(&channel,(const uint8_t *)self+0x64,4);
 if(channel!=1u&&channel!=2u)return;
 unsigned side_idx=channel-1u;
 uint8_t *frame=(uint8_t *)self+0x104;
 uint64_t now_ms=default_now_ms();
 uint32_t ctrl=current_control_revision();
 int route_up=!!(ctrl&4u);
 int on_extra=route_up&&!!(ctrl&(side_idx==0?1u:2u));
 struct xz_extra_deck d;
 struct xz_jd_snapshot snap;
 int have_snap=0;
 if(on_extra&&read_published_deck((int)side_idx,&d)){
  have_snap=xz_jog_display_snapshot_from_deck((int)side_idx,&d,now_ms,&snap);
 }
 if(!on_extra){
  (void)xz_jd_select(&jd_state[side_idx],channel,NULL,now_ms);
 }else if(have_snap){
  (void)xz_jd_select(&jd_state[side_idx],channel+2u,&snap,now_ms);
 }
 uint8_t out[XZ_JD_FRAME];
 (void)xz_jd_step(&jd_state[side_idx],frame,on_extra&&have_snap?&snap:NULL,now_ms,out);
 memcpy(frame,out,XZ_JD_FRAME);
}

int xz_jog_display_runtime_start(void){
 if(atomic_load(&jd_installed))return 0;
 for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
  atomic_init(&jd_pub[i].seq,0);
  memset(jd_pub[i].deck,0,sizeof jd_pub[i].deck);
  prime_side_baseline(i);
 }
 static const unsigned char g_assemble[8]={0xf0,0x4f,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
 if(xz_hook_arm(0x2591e0,g_assemble,(void *)(uintptr_t)hook_jog_assemble,(void **)&orig_jog_assemble))return -1;
 atomic_store(&jd_installed,1);
 return 0;
}

void xz_jog_display_runtime_stop(void){
 atomic_store(&jd_installed,0);
}

void xz_jog_display_runtime_test_install(const struct xz_jog_display_test_ops *originals,void (**assemble_hook_out)(void *)){
 atomic_store(&jd_installed,0);
 for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
  atomic_init(&jd_pub[i].seq,0);
  memset(jd_pub[i].deck,0,sizeof jd_pub[i].deck);
  prime_side_baseline(i);
 }
 orig_jog_assemble=originals?originals->assemble:NULL;
 test_now_ms=originals?originals->now_ms:NULL;
 test_control_revision=originals?originals->control_revision:NULL;
 atomic_store(&jd_installed,1);
 if(assemble_hook_out)*assemble_hook_out=hook_jog_assemble;
}

const struct xz_jd *xz_jog_display_runtime_state(int side_index){
 return side_index>=0&&side_index<XZ_EXTRA_DECKS?&jd_state[side_index]:NULL;
}
