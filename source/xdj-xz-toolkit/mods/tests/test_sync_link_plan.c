#ifdef NDEBUG
#error Sync link plan tests require assertions
#endif
#include "../sync_link_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
static struct xz_sync_link_claim claim={.source_id=3,.owner=42,.route_epoch=7,.source_revision=8,.playback_epoch=9,.external_writer_epoch=10,.music_id={1,2,3,4,5,6,7,8},.music_id_known=1,.previous_master_id=0};
static struct xz_sync_link_pcm_anchor anchor={.route_epoch=7,.source_revision=8,.playback_epoch=9,.completed_output_frame=44100,.captured_mono_us=1000000,.music_id={1,2,3,4,5,6,7,8},.completed_pcm=1,.clock_domain_mapped=1,.playing=1,.phase_eligible=1,.source_rate=44100,.bpm100=12000,.uncertainty_us=25,.source_frame=55125,.beat_source_frame=44100,.effective_speed=1,.beat_in_bar=1,.future_beat_ms={500,1000,1500,2000,2500,3000,3500,4000}};
static struct xz_sync_link_clock_pair pair={.mono_us=1000000,.realtime_us=UINT64_C(1700000000250000),.uncertainty_us=25};
static uint32_t big(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void setup(struct xz_sync_link_lease *lease,struct xz_sync_link_clock *clock){memset(lease,0,sizeof *lease);memset(clock,0,sizeof *clock);assert(xz_sync_link_lease_claim(lease,&claim)==XZ_SYNC_LINK_OK);assert(xz_sync_link_clock_update(clock,&pair)==XZ_SYNC_LINK_OK);}
int main(int argc,char **argv){
 struct xz_sync_link_lease lease;struct xz_sync_link_clock clock;struct xz_sync_link_plan plan;setup(&lease,&clock);
 if(argc>1){
  unsigned id=(unsigned)strtoul(argv[1],NULL,0);double speed=argc>2?strtod(argv[2],NULL):1;unsigned bpm=argc>3?(unsigned)strtoul(argv[3],NULL,0):120;double fraction=argc>4?strtod(argv[4],NULL):.5;
  claim.source_id=id;memset(&lease,0,sizeof lease);assert(xz_sync_link_lease_claim(&lease,&claim)==XZ_SYNC_LINK_OK);anchor.effective_speed=speed;anchor.bpm100=bpm*100;unsigned period=60000/bpm;
  for(unsigned i=0;i<8;i++)anchor.future_beat_ms[i]=(i+1)*period;
  anchor.source_frame=anchor.beat_source_frame+fraction*period*44.1;pair.realtime_us=UINT64_C(1700000000000000)+(uint64_t)llround(period*fraction*1000/speed);memset(&clock,0,sizeof clock);assert(xz_sync_link_clock_update(&clock,&pair)==XZ_SYNC_LINK_OK);
  assert(xz_sync_link_build(&lease,&clock,&anchor,1000000,&plan));
  printf("{\"packet\":\"");for(unsigned i=0;i<60;i++)printf("%02x",plan.beat_packet[i]);printf("\",\"sec\":%d,\"usec\":%d,\"uncertainty_us\":%u}\n",plan.beat_sec,plan.beat_usec,plan.uncertainty_us);return 0;
 }
 assert(xz_sync_link_build(&lease,&clock,&anchor,1000000,&plan));assert(plan.valid&&plan.publication==1&&plan.request_master&&plan.input_master_id==3);
 assert(plan.beat_sec==1700000000&&plan.beat_usec==0&&plan.uncertainty_us==73);
 const unsigned expected[6]={500,1000,2000,2000,4000,4000};for(unsigned i=0;i<6;i++)assert(big(plan.beat_packet+i*4)==expected[i]);
 assert(big(plan.beat_packet+0x30)==0x100000&&big(plan.beat_packet+0x34)==12000&&plan.beat_packet[0x38]==1&&plan.beat_packet[0x3b]==3);
 uint64_t publication=lease.publication;
 struct xz_sync_link_pcm_anchor bad=anchor;bad.completed_pcm=0;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan)&&plan.reason==XZ_SYNC_LINK_NO_COMPLETED_PCM);
 bad=anchor;bad.clock_domain_mapped=0;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan));
 bad=anchor;bad.playback_epoch++;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan)&&plan.reason==XZ_SYNC_LINK_SOURCE_CHANGED);
 assert(!xz_sync_link_build(&lease,&clock,&anchor,1100001,&plan)&&plan.reason==XZ_SYNC_LINK_STALE_PCM);
 bad=anchor;bad.playing=0;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan)&&plan.reason==XZ_SYNC_LINK_NO_PHASE);
 bad=anchor;bad.effective_speed=-1;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan));bad.effective_speed=NAN;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan));
 bad=anchor;bad.future_beat_ms[3]=1;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan));
 bad=anchor;bad.uncertainty_us=1990;assert(!xz_sync_link_build(&lease,&clock,&bad,1000000,&plan)&&plan.reason==XZ_SYNC_LINK_CLOCK_UNCERTAIN);assert(lease.publication==publication);
 struct xz_sync_link_clock_pair moved=pair;moved.realtime_us+=10000;assert(xz_sync_link_clock_update(&clock,&moved)==XZ_SYNC_LINK_CLOCK_STEP&&!clock.valid&&clock.stepped);assert(xz_sync_link_clock_update(&clock,&pair)==XZ_SYNC_LINK_CLOCK_STEP);assert(!xz_sync_link_build(&lease,&clock,&anchor,1000000,&plan));memset(&clock,0,sizeof clock);assert(xz_sync_link_clock_update(&clock,&pair)==XZ_SYNC_LINK_OK);
 struct xz_sync_link_observation observe={.external_writer_epoch=10,.payload_still_ours=1,.current_master_id=2};struct xz_sync_link_cleanup clean;
 assert(xz_sync_link_lease_observe(&lease,&observe,&clean)==XZ_SYNC_LINK_OK&&lease.master_committed);assert(xz_sync_link_build(&lease,&clock,&anchor,1000000,&plan)&&!plan.request_master);
 observe.external_writer_epoch=11;observe.foreign_present=1;assert(xz_sync_link_lease_observe(&lease,&observe,&clean)==XZ_SYNC_LINK_LEASE_LOST);
 assert(clean.inhibit_phase&&!clean.clear_owned_payload&&!clean.restore_master&&clean.restore_input_id==1&&!lease.active);
 assert(!xz_sync_link_build(&lease,&clock,&anchor,1000000,&plan)&&plan.reason==XZ_SYNC_LINK_LEASE_LOST);
 setup(&lease,&clock);observe=(struct xz_sync_link_observation){.external_writer_epoch=10,.payload_still_ours=1,.current_master_id=2};assert(!xz_sync_link_lease_observe(&lease,&observe,&clean));observe.current_master_id=1;
 assert(xz_sync_link_lease_observe(&lease,&observe,&clean)==XZ_SYNC_LINK_LEASE_LOST&&!clean.restore_master&&clean.clear_owned_payload);
 setup(&lease,&clock);observe=(struct xz_sync_link_observation){.external_writer_epoch=10,.master_choice_epoch=1,.payload_still_ours=1,.current_master_id=2};
 assert(xz_sync_link_lease_observe(&lease,&observe,&clean)==XZ_SYNC_LINK_LEASE_LOST&&!clean.restore_master);
 setup(&lease,&clock);observe=(struct xz_sync_link_observation){.external_writer_epoch=10,.payload_still_ours=1,.current_master_id=2};xz_sync_link_lease_release(&lease,&observe,&clean);
 assert(clean.inhibit_phase&&clean.clear_owned_payload&&clean.restore_master&&clean.restore_input_id==1&&!lease.active);
 setup(&lease,&clock);struct xz_sync_link_claim other=claim;other.owner++;assert(xz_sync_link_lease_claim(&lease,&other)==XZ_SYNC_LINK_SLOT_BUSY);
 memset(&lease,0,sizeof lease);other=claim;other.foreign_present=1;assert(xz_sync_link_lease_claim(&lease,&other)==XZ_SYNC_LINK_SLOT_BUSY);
 other=claim;other.previous_master_id=2;assert(xz_sync_link_lease_claim(&lease,&other)==XZ_SYNC_LINK_SLOT_BUSY);
 other=claim;other.music_id_known=0;assert(xz_sync_link_lease_claim(&lease,&other)==XZ_SYNC_LINK_BAD_INPUT);
 /* Only a lease publishes players 3/4. Losing one cannot return the engine to the other. */
 for(unsigned id=3;id<=4;id++){
  struct xz_sync_link_claim software=claim;software.source_id=id;software.previous_master_id=id==3?3:2;
  memset(&lease,0,sizeof lease);assert(xz_sync_link_lease_claim(&lease,&software)==XZ_SYNC_LINK_OK);
  observe=(struct xz_sync_link_observation){.external_writer_epoch=10,.payload_still_ours=1,.current_master_id=(int32_t)id-1};
  xz_sync_link_lease_release(&lease,&observe,&clean);assert(clean.restore_master&&clean.restore_input_id==0);
 }
 puts("PASS pure Link plan: completedPCM provenance, BE packet/time calibration, bounded freshness/uncertainty, source lease and foreign/operator cleanup; no native activation");return 0;
}
