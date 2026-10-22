/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *observed_frame_copy(void *,const void *,size_t);
#define memcpy observed_frame_copy
#include "../jog_display_runtime.c"
#undef memcpy
#include "jog_display_fixtures.h"
static uint64_t clock_ms=10000,media[2]={10,20};
static uint32_t content[2]={10,20},revision,reads,copies[2];
static struct xz_ds_selection selection={.epoch={1,1},.deck={3,4},.route_up=1,.route_epoch=3};
static struct xz_extra_decks values;
static struct xz_extra_column *columns[2];
static uint8_t objects[2][0x200],original_frame[2][148];
static void (*hook)(void *);
static unsigned flip_at,flip_kind,copy_clock_step,stop_on_copy,in_hook;
static unsigned now_calls,now_flip_at,now_flip_kind,after_fence_flip;
static void *observed_frame_copy(void *dst,const void *src,size_t size){
 if(after_fence_flip&&in_hook&&dst==objects[0]+0x104&&size==148){
  after_fence_flip=0;content[0]++; /* Simulate a concurrent owner event after validation. */
 }
 return memcpy(dst,src,size);
}
static uint64_t now(void){
 if(++now_calls==now_flip_at){
  if(now_flip_kind==1)content[0]++;
  if(now_flip_kind==2)selection.epoch[0]+=2;
  if(now_flip_kind==3)media[0]++;
  if(now_flip_kind==4)selection.route_epoch++;
  if(now_flip_kind==5)xz_jog_display_runtime_stop();
  if(now_flip_kind==6)clock_ms+=251;
 }
 return clock_ms;
}
static uint32_t control(void){return 12u|(selection.deck[0]==3?1u:0u)|(selection.deck[1]==4?2u:0u);}
uint32_t xz_extra_decks_control_revision(void){return control();}
uint32_t xz_extra_decks_epoch(void){return selection.route_epoch;}
uint32_t xz_extra_decks_jog_content_epoch(int side){return (unsigned)side<2?content[side]:0;}
uint64_t xz_extra_source_generation(int source){return source>=1&&source<=2?media[source-1]:0;}
int xz_runtime_deck_selection_snapshot(struct xz_ds_selection *out){
 if(++reads==flip_at){
  if(flip_kind==1)selection.epoch[0]+=2;
  if(flip_kind==2)content[0]++;
  if(flip_kind==3)media[0]++;
  if(flip_kind==4)clock_ms+=251;
  if(flip_kind==5)return 0;
 }
 *out=selection;return 1;
}
int xz_hook_arm(uint32_t at,const unsigned char g[8],void *f,void **old){(void)at;(void)g;(void)f;(void)old;return -1;}
static void original(void *self){
 unsigned side;memcpy(&side,(uint8_t *)self+0x64,4);side--;
 uint8_t *frame=(uint8_t *)self+0x104;memcpy(frame,side?native_right:native_outer,148);
 uint8_t p[64];memcpy(p,native_info,64);p[0]=(uint8_t)((side+1)<<4);
 for(unsigned i=0;i<64;i++)frame[12+(i^3u)]=p[i];
 uint32_t crc=xz_jd_crc(frame);memcpy(frame+144,&crc,4);memcpy(original_frame[side],frame,148);
}
static const struct xz_jog_display_test_ops ops={original,now,control};
enum xz_extra_wave_copy_result xz_extra_decks_copy_wave(int side,const struct xz_extra_wave_key *expected,uint32_t first,struct xz_extra_wave_chunk *out){
 assert(!in_hook&&(unsigned)side<2);copies[side]++;clock_ms+=copy_clock_step;
 if(stop_on_copy){stop_on_copy=0;xz_jog_display_runtime_test_install(&ops,&hook);}
 struct xz_extra_wave_key key=sample_wave_key(&values.deck[side]);
 if(expected&&!same_wave_key(expected,&key))return XZ_EXTRA_WAVE_COPY_CHANGED;
 if(!key.column_count)return expected?XZ_EXTRA_WAVE_COPY_CHANGED:XZ_EXTRA_WAVE_COPY_ABSENT;
 assert(first<=key.column_count);out->key=key;out->first=first;out->count=key.column_count-first;
 if(out->count>512)out->count=512;
 if(out->count)memcpy(out->columns,columns[side]+first,out->count*sizeof *columns[side]);
 return XZ_EXTRA_WAVE_COPY_OK;
}
static uint8_t *frame(unsigned side){return objects[side]+0x104;}
static void packet(unsigned side,uint8_t out[64]){for(unsigned i=0;i<64;i++)out[i]=frame(side)[12+(i^3u)];}
static void tick(unsigned side){in_hook=1;hook(objects[side]);in_hook=0;}
static void publish(void){values.snapshot_revision=++revision;xz_jog_display_runtime_publish(&values,(uint32_t)clock_ms);}
static void reset(unsigned count){
 clock_ms=10000;reads=flip_at=flip_kind=copy_clock_step=stop_on_copy=0;memset(copies,0,sizeof copies);
 now_calls=now_flip_at=now_flip_kind=after_fence_flip=0;
 selection.route_up=1;selection.route_epoch=3;selection.deck[0]=3;selection.deck[1]=4;
 memset(&values,0,sizeof values);
 for(unsigned side=0;side<2;side++){
  selection.epoch[side]++;content[side]++;media[side]++;free(columns[side]);columns[side]=count?calloc(count,sizeof **columns):NULL;
  for(unsigned i=0;i<count;i++)columns[side][i]=(struct xz_extra_column){i*13u+side,i*17u,i*23u};
  struct xz_extra_deck *d=&values.deck[side];d->loaded=1;d->source=(int)side+1;d->total_ms=180000;d->elapsed_ms=30000;
  d->tempo_range=10;d->vinyl=1;d->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
  d->jog_content_epoch=content[side];strcpy(d->key,"Fm");d->bpm_x100=d->track_bpm_x100=17400;
  d->timeline=(struct xz_extra_timeline){.source_frame=30000,.source_rate=1000,.at_ms=(uint32_t)clock_ms,
   .media_generation=media[side],.route_epoch=3,.load_revision=7,.transport_generation=2,
   .flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE};
  d->wave=(struct xz_extra_wave){columns[side],count,180000,765};if(count)d->known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
  unsigned channel=side+1;memcpy(objects[side]+0x64,&channel,4);
 }
 values.master_actual.deck=XZ_MASTER_UNKNOWN;
 xz_jog_display_runtime_test_install(&ops,&hook);publish();tick(0);tick(1);
}
static void complete(void){
 for(unsigned i=0;i<40;i++){
  xz_jog_display_runtime_service_assets();
  if(jd_wave_job[0].done&&jd_wave_job[1].done)return;
 }
 assert(!"asset failed to finish");
}
static void waves_and_contention(void){
 reset(1025);assert(!atomic_flag_test_and_set(&jd_wave_slot[0].busy));
 for(unsigned i=0;i<12;i++){xz_jog_display_runtime_service_assets();clock_ms++;publish();tick(0);tick(1);}
 assert(jd_wave_job[0].ready&&!jd_wave_job[0].done&&jd_wave_job[1].done);
 uint8_t p[64];packet(0,p);assert(p[1]==0x21&&p[0x3c]==18);
 atomic_flag_clear(&jd_wave_slot[0].busy);complete();
 /* Poison and free all analysis storage. The callback must use owned bytes. */
 for(unsigned side=0;side<2;side++){memset(columns[side],0xcd,1025*sizeof **columns);free(columns[side]);columns[side]=NULL;}
 unsigned seen[2]={jd_state[0].wave_next,jd_state[1].wave_next};
 for(unsigned n=0;n<180;n++)for(unsigned side=0;side<2;side++){
  tick(side);packet(side,p);
  if(p[1]==0x2c&&p[4]==73){assert(p[2]==++seen[side]);uint8_t expected[64];
   xz_jd_wave_packet(&jd_wave_slot[side].value.wave,side+1,seen[side]-1,expected);assert(!memcmp(p,expected,64));}
 }
 assert(seen[0]==73&&seen[1]==73&&jd_state[1].wave_next==73);
 values.deck[0].wave.span_ms=90000;publish();for(unsigned n=0;n<7;n++)tick(0);
 assert(!jd_side[0].have_wave);packet(0,p);assert(p[1]==0x21);
 puts("PASS owned assets survive poisoned source, both sides progress and live INFO survives asset contention");
}
static void fences(void){
 for(unsigned kind=1;kind<=5;kind++){
  reset(1025);complete();for(unsigned n=0;n<10;n++)tick(0);
  uint16_t next=jd_state[0].wave_next,seq=jd_state[0].wire_sequence;
  reads=0;flip_at=2;flip_kind=kind;tick(0);flip_at=0;
  assert(!memcmp(frame(0),original_frame[0],148));assert(jd_state[0].wave_next==next&&jd_state[0].wire_sequence==seq);
 }
 reset(1025);stop_on_copy=1;xz_jog_display_runtime_service_assets();
 assert(!jd_wave_slot[0].valid||jd_wave_slot[0].value.tag.session!=atomic_load(&jd_session));
 reset(100000);copy_clock_step=1;memset(copies,0,sizeof copies);xz_jog_display_runtime_service_assets();
 assert(copies[0]==1&&copies[1]==1);copy_clock_step=0;
 memset(copies,0,sizeof copies);xz_jog_display_runtime_service_assets();assert(copies[0]<=64&&copies[1]<=64);
 puts("PASS final side/load/media/time/busy fences reject without cursor progress; stale producer and connector budget bounded");
}
static void replacement(void){
 unsigned boundaries[]={1,37,73};
 for(unsigned case_no=0;case_no<3;case_no++){
  reset(1025);complete();uint8_t p[64];unsigned reached=0;
  for(unsigned n=0;n<170&&!reached;n++){tick(0);packet(0,p);reached=p[1]==0x2c&&p[4]==73&&p[2]==boundaries[case_no];}
  assert(reached);
  struct xz_extra_decks delayed=values;
  content[0]++;values.deck[0].jog_content_epoch=content[0];values.deck[0].timeline.load_revision++;
  values.deck[0].known&=~(XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM));
  /* Retained nonzero values deliberately lack known bits. */
  assert(!atomic_flag_test_and_set(&jd_wave_slot[0].busy));
  delayed.snapshot_revision=++revision;xz_jog_display_runtime_publish(&delayed,(uint32_t)clock_ms);
  for(unsigned n=0;n<7;n++){tick(0);packet(0,p);assert(!(p[1]==0x2c&&p[4]==73));}
  assert(p[1]==0x21&&!p[0x3c]&&!(p[9]&0x20));
  memset(columns[0],0xff,1025*sizeof **columns);publish();
  for(unsigned n=0;n<7;n++){xz_jog_display_runtime_service_assets();tick(0);}
  packet(0,p);assert(p[1]==0x21&&!p[0x3c]&&(p[9]&0x30)==0x10);
  assert(jd_wave_job[0].ready&&!jd_wave_job[0].done);
  atomic_flag_clear(&jd_wave_slot[0].busy);complete();
  unsigned seen=0;
  for(unsigned n=0;n<170&&seen<73;n++){
   tick(0);packet(0,p);if(p[1]==0x2c&&p[4]==73){
    assert(p[2]==++seen);uint8_t expected[64];xz_jd_wave_packet(&jd_wave_slot[0].value.wave,1,seen-1,expected);
    assert(!memcmp(expected,p,64));
   }
  }
  assert(seen==73);
  /* The completed software waveform must never contaminate native retention. */
  selection.deck[0]=1;selection.epoch[0]++;
  for(unsigned n=0;n<30;n++)tick(0);
  assert(jd_state[0].phase==XZ_JD_NATIVE);
  for(unsigned i=0;i<144;i++)if(i!=8&&i!=9)assert(frame(0)[i]==original_frame[0][i]);
 }
 puts("PASS replacement at wave1/37/73, blocked mailbox, delayed old snapshot, missing key/BPM, fresh batch and exact native restoration");
}
static void clock_and_commit_boundary(void){
 for(unsigned kind=1;kind<=6;kind++){
  reset(1025);complete();for(unsigned n=0;n<10;n++)tick(0);
  uint16_t next=jd_state[0].wave_next,seq=jd_state[0].wire_sequence;
  now_calls=0;now_flip_at=2;now_flip_kind=kind;tick(0);now_flip_at=0;
  assert(now_calls==2);assert(!memcmp(frame(0),original_frame[0],148));
  assert(jd_state[0].wave_next==next&&jd_state[0].wire_sequence==seq);
 }
 reset(1025);complete();for(unsigned n=0;n<10;n++)tick(0);
 uint32_t accepted_content=content[0];uint16_t accepted_seq=jd_state[0].wire_sequence;
 after_fence_flip=1;tick(0);
 assert(content[0]==accepted_content+1&&!after_fence_flip);
 assert(memcmp(frame(0),original_frame[0],148)&&jd_state[0].wire_sequence==(uint16_t)(accepted_seq+1));
 /* Already validated output can race a later event. The next callback must
    cancel it; no claim of atomic physical-send cancellation is made. */
 tick(0);uint8_t p[64];packet(0,p);assert(p[1]==0x2d&&!jd_side[0].have_wave);
 puts("PASS final clock changes reject all six contexts; post-validation event permits accepted packet then cancels next callback");
}
static void authority(void){
 reset(0);
 for(unsigned side=0;side<2;side++){
  struct xz_extra_deck *d=&values.deck[side];d->master=1; /* deliberately insufficient */
  struct xz_master_observation good={.deck=(int)side+2,.revision=3,.observed_ms=(uint32_t)clock_ms,
   .route_epoch=3,.load_revision=7,.transport_generation=2};
  for(unsigned variant=0;variant<9;variant++){
   values.master_actual=good;values.master_intent=(struct xz_master_intent){.deck=(int)side+2,.epoch=8,.explicit_choice=1,.operator_epoch=9};
   if(variant==1)values.master_actual.deck=XZ_MASTER_UNKNOWN;
   if(variant==2)values.master_actual.revision=0;
   if(variant==3)values.master_actual.route_epoch++;
   if(variant==4)values.master_actual.load_revision++;
   if(variant==5)values.master_actual.transport_generation++;
   if(variant==6)values.master_actual.observed_ms=(uint32_t)clock_ms-101;
   if(variant==7)values.master_actual.observed_ms=(uint32_t)clock_ms+1;
   if(variant==8)values.master_actual.observed_ms=(uint32_t)clock_ms-100;
   publish();for(unsigned n=0;n<8;n++)tick(side);
   uint8_t p[64];packet(side,p);int yes=variant==0||variant==8;
   assert(p[1]==0x21&&!!(p[5]&0x80)==yes&&!!(frame(side)[0x56]&0x80)==yes);
  }
  clock_ms=(UINT64_C(1)<<32)+10;good.observed_ms=UINT32_MAX-50;
  values.master_actual=good;publish();tick(side);
  uint8_t wrapped[64];packet(side,wrapped);assert(wrapped[5]&0x80);
  clock_ms=10000;
  xz_jog_display_runtime_publish_deck((int)side,d,(uint32_t)clock_ms);tick(side);
  uint8_t p[64];packet(side,p);assert(!(p[5]&0x80));
  selection.deck[side]=(int)side+1;selection.epoch[side]++;
  for(unsigned n=0;n<20;n++)tick(side);
  assert(jd_state[side].phase==XZ_JD_NATIVE);
  for(unsigned i=0;i<144;i++)if(i!=8&&i!=9)assert(frame(side)[i]==original_frame[side][i]);
 }
 puts("PASS confirmed MASTER tuple/100ms expiry; UNKNOWN, pending, future and direct-deck-only do not assert; native restoration exact except owned sequence/CRC");
}
static double bench_elapsed(struct timespec begin,struct timespec end){return (end.tv_sec-begin.tv_sec)*1000000.+(end.tv_nsec-begin.tv_nsec)/1000.;}
static void benchmark(void){
 reset(1025);complete();for(unsigned n=0;n<160;n++){tick(0);tick(1);}
 struct timespec begin,end;const unsigned count=2000;double measured[3];
 for(unsigned mode=0;mode<3;mode++){
  clock_gettime(CLOCK_MONOTONIC,&begin);
  for(unsigned n=0;n<count;n++){
   if(!mode)original(objects[n&1u]);
   else {if(mode==2)jd_side[n&1u].have_wave=0;tick(n&1u);}
  }
  clock_gettime(CLOCK_MONOTONIC,&end);measured[mode]=bench_elapsed(begin,end)/count;
 }
 printf("SOFTWARE COST ONLY us/call: synthetic-original %.3f, whole-hook %.3f, forced4234B-adoption %.3f; assets+jobs+chunk %zuB, side-owned waves %zuB, transaction %zuB\n",
  measured[0],measured[1],measured[2],sizeof jd_wave_slot+sizeof jd_wave_job+sizeof jd_wave_chunk,
  2*sizeof(struct xz_jd_wave),sizeof(struct xz_jd_transaction));
}
int main(void){setvbuf(stdout,NULL,_IONBF,0);waves_and_contention();fences();replacement();clock_and_commit_boundary();authority();benchmark();free(columns[0]);free(columns[1]);return 0;}
