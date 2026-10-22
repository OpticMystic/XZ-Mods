/* SPDX-License-Identifier: MIT */
#include "../jog_display.h"
#include "jog_display_fixtures.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void packet(const uint8_t *frame,uint8_t *out){for(unsigned i=0;i<64;i++)out[i]=frame[12+(i^3u)];}
static void seed(struct xz_jd *s){
 uint8_t p[64];memcpy(p,native_info,64);p[0]=(uint8_t)(s->physical_side<<4);xz_jd_capture(s,p);
 for(unsigned i=1;i<=5;i++){xz_jd_clear_packet(s->physical_side,i,p);xz_jd_capture(s,p);}
}
static void schedule(unsigned side,unsigned gap,int changing){
 struct xz_jd s;void *storage=malloc(XZ_JD_STORAGE_BYTES);assert(storage);
 assert(!xz_jd_init(&s,side,storage,XZ_JD_STORAGE_BYTES,250));xz_jd_native_epoch(&s,1);seed(&s);
 struct xz_jd_wave wave;uint8_t rgb[1800];memset(rgb,0x55,sizeof rgb);xz_jd_wave_encode_rgb(rgb,0,&wave);
 struct xz_jd_snapshot live={.generation=1,.length_ms=600000,.logical_deck=(uint8_t)(side+2),
  .tempo_known=1,.tempo_range=10,.vinyl_mode=1,.status_known=1};
 struct xz_jd_transaction tx;unsigned count=0,infos=0,since_info=0,steps=0;uint64_t last_info=0;
 while(count<73&&steps<200){
  uint64_t now=1000+(uint64_t)steps*gap;live.captured_ms=now;live.position_ms=(uint32_t)(now-1000);
  if(changing)live.tempo_rate=(int16_t)steps;
  int result=xz_jd_prepare(&s,side==1?native_outer:native_right,side+2,&live,now,&wave,1,&tx);
  assert(result==XZ_JD_OK);xz_jd_commit(&s,&tx);
  uint8_t p[64];packet(tx.frame,p);
  if(p[1]==0x21){
   if(infos)assert(now-last_info<=(20+gap>2*gap?20+gap:2*gap));
   last_info=now;infos++;since_info=0;
   unsigned milliseconds=p[0xd]|((unsigned)p[0xe]<<8);
   assert((unsigned)p[0xb]*60000u+(unsigned)p[0xc]*1000u+milliseconds==live.position_ms);
  }else if(p[1]==0x2c&&p[4]==73){
   uint8_t expected[64];assert(xz_jd_wave_packet(&wave,side,count,expected));
   assert(!memcmp(p,expected,64));count++;assert(infos&&++since_info<=7);
  }
  steps++;
 }
 assert(count==73&&steps<=151); /* five clears plus at most 146 transfer opportunities */
 /* Preparing a packet without commit consumes no cursor, sequence or deadline. */
 uint16_t next=s.wave_next,sequence=s.wire_sequence;uint64_t last=s.last_info_ms;
 live.captured_ms=1000+(uint64_t)steps*gap;
 assert(xz_jd_prepare(&s,side==1?native_outer:native_right,side+2,&live,live.captured_ms,&wave,2,&tx)==XZ_JD_OK);
 assert(s.wave_next==next&&s.wire_sequence==sequence&&s.last_info_ms==last);
 uint8_t unsupported[148];memcpy(unsupported,native_outer,148);unsupported[2]|=0x40;
 assert(xz_jd_prepare(&s,unsupported,side+2,&live,live.captured_ms,&wave,2,&tx)==XZ_JD_UNSUPPORTED);
 assert(s.wave_next==next&&s.wire_sequence==sequence);
 /* Loss of an adopted waveform clears it before another software batch. */
 assert(xz_jd_prepare(&s,side==1?native_outer:native_right,side+2,&live,live.captured_ms,NULL,0,&tx)==XZ_JD_OK);
 uint8_t p[64];packet(tx.frame,p);assert(p[1]==0x2d);xz_jd_commit(&s,&tx);
 free(storage);
}
int main(void){
 _Static_assert(sizeof(struct xz_jd_transaction)<1024,"fixed transaction bound");
 for(unsigned side=1;side<=2;side++)for(unsigned gap=1;gap<=100;gap*=10)for(int changing=0;changing<=1;changing++)schedule(side,gap,changing);
 printf("PASS transactional wave schedule: both sides, 1/10/100ms service, changing INFO, exact73packets, bounded gaps, rejected proposals; transaction bytes %zu\n",sizeof(struct xz_jd_transaction));
 return 0;
}
