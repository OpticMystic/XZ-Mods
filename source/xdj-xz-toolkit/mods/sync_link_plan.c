/* SPDX-License-Identifier: MIT */
#include "sync_link_plan.h"
#include <limits.h>
#include <math.h>
#include <string.h>
static int known_id(const unsigned char id[8]){unsigned n=0;for(unsigned i=0;i<8;i++)n|=id[i];return n!=0;}
static int input_master(int32_t id){return id<0?0:id<4?id+1:id+13;}
static int32_t lease_master(const struct xz_sync_link_lease *s){return (int32_t)s->claim.source_id-1;}
static void be32(unsigned char *p,uint32_t x){p[0]=(unsigned char)(x>>24);p[1]=(unsigned char)(x>>16);p[2]=(unsigned char)(x>>8);p[3]=(unsigned char)x;}
int xz_sync_link_clock_update(struct xz_sync_link_clock *clock,const struct xz_sync_link_clock_pair *pair){
 if(!clock)return XZ_SYNC_LINK_CLOCK_UNCERTAIN;
 if(clock->stepped)return XZ_SYNC_LINK_CLOCK_STEP;
 if(!pair||pair->mono_us>INT64_MAX||pair->realtime_us>INT64_MAX||pair->uncertainty_us>XZ_SYNC_LINK_MAX_UNCERTAINTY_US){clock->valid=0;return XZ_SYNC_LINK_CLOCK_UNCERTAIN;}
 int64_t offset=(int64_t)pair->realtime_us-(int64_t)pair->mono_us;
 if(clock->valid){
  if(pair->mono_us<clock->pair.mono_us){clock->valid=0;clock->stepped=1;return XZ_SYNC_LINK_CLOCK_STEP;}
  uint64_t change=offset>=clock->offset_us?(uint64_t)offset-(uint64_t)clock->offset_us:(uint64_t)clock->offset_us-(uint64_t)offset;if(change>2000){clock->valid=0;clock->stepped=1;return XZ_SYNC_LINK_CLOCK_STEP;}
 }
 clock->valid=1;clock->offset_us=offset;clock->pair=*pair;return XZ_SYNC_LINK_OK;
}
int xz_sync_link_lease_claim(struct xz_sync_link_lease *s,const struct xz_sync_link_claim *c){
 if(!s||!c||c->source_id<3||c->source_id>4||!c->owner||!c->route_epoch||!c->source_revision||!c->playback_epoch||
    !c->music_id_known||!known_id(c->music_id)||c->previous_master_id< -1||c->previous_master_id>5)return XZ_SYNC_LINK_BAD_INPUT;
 if(c->foreign_present||c->previous_master_id==(int32_t)c->source_id-1)return XZ_SYNC_LINK_SLOT_BUSY;
 if(s->active){
  return s->claim.owner==c->owner&&s->claim.source_id==c->source_id&&s->claim.route_epoch==c->route_epoch&&
   s->claim.source_revision==c->source_revision&&s->claim.playback_epoch==c->playback_epoch&&
   s->claim.external_writer_epoch==c->external_writer_epoch&&s->claim.master_choice_epoch==c->master_choice_epoch&&!memcmp(s->claim.music_id,c->music_id,8)?XZ_SYNC_LINK_OK:XZ_SYNC_LINK_SLOT_BUSY;
 }
 memset(s,0,sizeof *s);s->claim=*c;s->active=1;return XZ_SYNC_LINK_OK;
}
void xz_sync_link_lease_release(struct xz_sync_link_lease *s,const struct xz_sync_link_observation *o,struct xz_sync_link_cleanup *clean){
 if(clean)memset(clean,0,sizeof *clean);
 if(!s||!s->active)return;
 if(clean){
  clean->inhibit_phase=1;
  if(o){
   clean->clear_owned_payload=o->payload_still_ours&&!o->foreign_present&&o->external_writer_epoch==s->claim.external_writer_epoch;
   clean->restore_master=o->current_master_id==lease_master(s)&&o->master_choice_epoch==s->claim.master_choice_epoch&&
    !o->foreign_present&&o->external_writer_epoch==s->claim.external_writer_epoch;
   /* IDs 2/3 are players 3/4, which only a lease publishes, and one lease is
      released here. Returning the engine to the other one leaves it following a
      player nobody publishes; return to no master instead. */
   int32_t previous=s->claim.previous_master_id;
   clean->restore_input_id=previous==2||previous==3?0:input_master(previous);
  }
 }
 s->active=0;s->master_committed=0;
}
int xz_sync_link_lease_observe(struct xz_sync_link_lease *s,const struct xz_sync_link_observation *o,struct xz_sync_link_cleanup *clean){
 if(clean)memset(clean,0,sizeof *clean);
 if(!s||!o||!s->active)return XZ_SYNC_LINK_LEASE_LOST;
 if(o->foreign_present||o->external_writer_epoch!=s->claim.external_writer_epoch||o->master_choice_epoch!=s->claim.master_choice_epoch||
    (s->master_committed&&o->current_master_id!=lease_master(s))||
    (!s->master_committed&&o->current_master_id!=lease_master(s)&&o->current_master_id!=s->claim.previous_master_id)){
  xz_sync_link_lease_release(s,o,clean);return XZ_SYNC_LINK_LEASE_LOST;
 }
 if(o->current_master_id==lease_master(s))s->master_committed=1;
 return XZ_SYNC_LINK_OK;
}
int xz_sync_link_build(struct xz_sync_link_lease *s,const struct xz_sync_link_clock *clock,const struct xz_sync_link_pcm_anchor *a,
 uint64_t now,struct xz_sync_link_plan *p){
 if(!p)return 0;
 memset(p,0,sizeof *p);p->reason=XZ_SYNC_LINK_BAD_INPUT;
 if(!s||!a||!clock)return 0;
 p->reason=XZ_SYNC_LINK_LEASE_LOST;if(!s->active)return 0;
 p->reason=XZ_SYNC_LINK_SOURCE_CHANGED;
 if(a->route_epoch!=s->claim.route_epoch||a->source_revision!=s->claim.source_revision||a->playback_epoch!=s->claim.playback_epoch||memcmp(a->music_id,s->claim.music_id,8))return 0;
 p->reason=XZ_SYNC_LINK_NO_COMPLETED_PCM;
 if(!a->completed_pcm||!a->completed_output_frame||!a->clock_domain_mapped)return 0;
 p->reason=XZ_SYNC_LINK_STALE_PCM;
 if(now<a->captured_mono_us||now-a->captured_mono_us>XZ_SYNC_LINK_MAX_AGE_US)return 0;
 p->reason=XZ_SYNC_LINK_CLOCK_UNCERTAIN;
 if(!clock->valid||now<clock->pair.mono_us||now-clock->pair.mono_us>XZ_SYNC_LINK_CALIBRATION_AGE_US||
    a->uncertainty_us>XZ_SYNC_LINK_MAX_UNCERTAINTY_US||clock->pair.uncertainty_us>XZ_SYNC_LINK_MAX_UNCERTAINTY_US)return 0;
 p->reason=XZ_SYNC_LINK_NO_PHASE;
 if(!a->playing||!a->phase_eligible||a->source_rate<8000||a->source_rate>384000||a->bpm100<4000||a->bpm100>24000||
    a->beat_in_bar<1||a->beat_in_bar>4||a->lane>1||!isfinite(a->effective_speed)||a->effective_speed<.25||a->effective_speed>4||
    !isfinite(a->source_frame)||!isfinite(a->beat_source_frame)||a->beat_source_frame<0||(a->source_frame<a->beat_source_frame&&(!a->completed_beat_history||a->beat_source_frame-a->source_frame>=1.)))return 0;
 if(a->bpm100*.01*a->effective_speed<40||a->bpm100*.01*a->effective_speed>240)return 0;
 for(unsigned i=0;i<8;i++)if(!a->future_beat_ms[i]||a->future_beat_ms[i]>120000||(i&&a->future_beat_ms[i]<=a->future_beat_ms[i-1]))return 0;
 double elapsed_source_ms=(a->source_frame-a->beat_source_frame)*1000/a->source_rate;
 if(elapsed_source_ms>a->future_beat_ms[0]+1000./a->source_rate)return 0;
 /* Packet time is a virtual origin at its Q20 rate, not a heard crossing time. */
 uint32_t q20_speed=(uint32_t)(a->effective_speed*1048576.0);
 double elapsed_us=fmax(0.,elapsed_source_ms)*1000/(q20_speed/1048576.);
 if(a->completed_beat_history){
  if(!isfinite(a->completed_beat_age_frames)||a->completed_beat_age_frames<0||
     a->completed_beat_age_frames*1000000./44100.>(double)a->captured_mono_us)return 0;
 }
 p->reason=XZ_SYNC_LINK_TIME_RANGE;
 if(!isfinite(elapsed_us)||elapsed_us<0||elapsed_us>(double)a->captured_mono_us||a->captured_mono_us>INT64_MAX)return 0;
 int64_t beat_mono=(int64_t)a->captured_mono_us-(int64_t)llround(elapsed_us);
 if((clock->offset_us>0&&beat_mono>INT64_MAX-clock->offset_us)||(clock->offset_us<0&&beat_mono< -clock->offset_us))return 0;
 int64_t beat_real=beat_mono+clock->offset_us;
 if(beat_real<0||beat_real/1000000>INT32_MAX)return 0;
 memset(p->beat_packet,255,sizeof p->beat_packet);
 unsigned pick[6]={0,1,4-a->beat_in_bar,3,8-a->beat_in_bar,7};
 for(unsigned i=0;i<6;i++)be32(p->beat_packet+i*4,a->future_beat_ms[pick[i]]);
 p->q20_speed=q20_speed;be32(p->beat_packet+0x30,p->q20_speed);be32(p->beat_packet+0x34,a->bpm100);
 p->beat_packet[0x38]=(unsigned char)a->beat_in_bar;p->beat_packet[0x39]=(unsigned char)a->lane;p->beat_packet[0x3a]=0;p->beat_packet[0x3b]=(unsigned char)s->claim.source_id;
 p->beat_sec=(int32_t)(beat_real/1000000);p->beat_usec=(int32_t)(beat_real%1000000);
 p->uncertainty_us=a->uncertainty_us+clock->pair.uncertainty_us+(1000000u+a->source_rate-1)/a->source_rate;
 if(p->uncertainty_us>XZ_SYNC_LINK_MAX_UNCERTAINTY_US){p->reason=XZ_SYNC_LINK_CLOCK_UNCERTAIN;return 0;}
 p->publication=++s->publication;p->owner=s->claim.owner;p->route_epoch=a->route_epoch;p->source_revision=a->source_revision;p->playback_epoch=a->playback_epoch;
 p->external_writer_epoch=s->claim.external_writer_epoch;p->master_choice_epoch=s->claim.master_choice_epoch;
 p->source_id=s->claim.source_id;p->bpm100=a->bpm100;memcpy(p->music_id,a->music_id,8);
 p->request_master=!s->master_committed;p->input_master_id=(int)s->claim.source_id;p->valid=1;p->reason=XZ_SYNC_LINK_OK;return 1;
}

