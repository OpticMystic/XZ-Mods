#include "extra_player.c"
#include "sync_link_plan.h"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
static unsigned checks;
#define VERIFY(x) do{checks++;assert(x);}while(0)
static void complete(struct deck *d,uint64_t end,uint64_t source){
 xz_usb_deck_position q={.applied_generation=d->generation,.completed_frames=end,
  .source_completed_output_frames=end,.source_after_completed=source>>32,.source_completed_wire_frame=end+882};
 completed_timeline(d,&q,end+882,1000+(uint32_t)(end*1000/XZ_USB_RATE));
}
int main(void){
 static struct deck d;static struct reader reader;static struct analysis analysis;
 static struct xz_extra_beat grid[]={{100,1,0},{1600,2,0},{3100,3,0},{4600,4,0}};
 reader.rate=44100;reader.frames=44100*20;d.r=&reader;d.analysis=&analysis;analysis.grid=grid;analysis.grid_count=4;
 d.state=XZ_XP_PLAYING;d.generation=7;
 uint64_t source=4410ULL<<32,lo=source,hi=70560ULL<<32;
 d.loop.active=1;d.loop.in=4410;d.loop.out=70560;
 for(unsigned i=0;i<800;i++){
  uint64_t step=(uint64_t)((1.+(i%100)/10000.)*4294967296.);
  d.output.pending=(struct output_span){.source=source,.step=step,.loop_in=lo,.loop_out=hi};
  output_accept(&d.output,i*256ULL,256);source=lo+(source-lo+step*256)%(hi-lo);
  complete(&d,(i+1)*256ULL,source);
  VERIFY(d.output.rate_valid&&fabs(d.output.rate-(double)step/ONE)<1e-14);
  VERIFY(d.output.beat_valid&&d.output.beat_ms==100);
  VERIFY(d.output.count<=OUTPUT_SPANS);
  if(i==200)VERIFY(d.output.count==OUTPUT_SPANS&&d.output.crossing<256.);
 }
 struct output_history before=d.output;
 complete(&d,800*256ULL,source);VERIFY(!memcmp(&before,&d.output,sizeof before));
 complete(&d,799*256ULL,source);VERIFY(!memcmp(&before,&d.output,sizeof before));
 output_reset(&d.output);d.timeline_output_frames=d.completed=0;d.loop.active=0;
 d.output.pending=(struct output_span){.source=4410ULL<<32,.step=(uint64_t)(1.07*ONE)};
 output_accept(&d.output,0,256);d.tempo_x100=-400;
 source=(4410ULL<<32)+128*d.output.pending.step;complete(&d,128,source);
 VERIFY(d.output.rate_valid&&fabs(d.output.rate-1.07)<1./ONE);
 VERIFY(d.output.beat_valid&&fabs(d.output.beat_age-128)<1e-10);
 source=(4410ULL<<32)+256*d.output.pending.step;complete(&d,256,source);
 VERIFY(d.output.rate_valid&&fabs(d.output.beat_age-256)<1e-10);
 /* An unmatched completion tag must not become an applied-rate promise. */
 d.output.pending.source=source;output_accept(&d.output,256,256);
 complete(&d,512,source+256*d.output.pending.step+(2ULL<<32));VERIFY(!d.output.rate_valid&&!d.output.beat_valid);
 /* Overflow of uncompleted history cannot fabricate the lost crossing. */
 output_reset(&d.output);d.timeline_output_frames=d.completed=0;source=4410ULL<<32;
 for(unsigned i=0;i<80;i++){
  uint64_t step=(uint64_t)((1.+(i%2)*.07)*ONE);
  d.output.pending=(struct output_span){.source=source,.step=step};output_accept(&d.output,i*16,16);source+=16*step;
 }
 complete(&d,80*16,source);VERIFY(d.output.rate_valid&&!d.output.beat_valid&&d.output.count==64);
 /* Unknown/scratch PCM is retained as an explicit gap in crossing provenance. */
 d.output.pending=(struct output_span){0};output_accept(&d.output,1280,16);
 complete(&d,1296,source);VERIFY(!d.output.rate_valid&&!d.output.beat_valid);
 /* A fractional grid point can be crossed before its integer source tag advances. */
 output_reset(&d.output);d.timeline_output_frames=d.completed=0;d.loop.active=0;
 grid[0].ms=101;grid[1].ms=601;grid[2].ms=1101;grid[3].ms=1601;
 d.output.pending=(struct output_span){.source=(uint64_t)(4453.08*ONE),.step=(uint64_t)(1.07*ONE)};
 output_accept(&d.output,0,1);source=d.output.pending.source+d.output.pending.step;complete(&d,1,source);
 VERIFY(d.output.beat_valid&&d.output.beat_ms==101&&d.output.beat_age>0&&d.output.beat_age<1);
 struct xz_sync_link_claim claim={.source_id=3,.owner=1,.route_epoch=1,.source_revision=1,.playback_epoch=7,.music_id={1},.music_id_known=1,.previous_master_id=0};
 struct xz_sync_link_lease lease={0};VERIFY(xz_sync_link_lease_claim(&lease,&claim)==XZ_SYNC_LINK_OK);
 struct xz_sync_link_clock clock={0};struct xz_sync_link_clock_pair pair={1000000,11000000,1};VERIFY(xz_sync_link_clock_update(&clock,&pair)==XZ_SYNC_LINK_OK);
 struct xz_sync_link_pcm_anchor a={.route_epoch=1,.source_revision=1,.playback_epoch=7,.completed_output_frame=883,.captured_mono_us=1000000,
  .music_id={1},.completed_pcm=1,.clock_domain_mapped=1,.playing=1,.phase_eligible=1,.source_rate=44100,.bpm100=12000,
  .source_frame=(double)(source>>32),.beat_source_frame=4454.1,.effective_speed=d.output.rate,.beat_in_bar=1,
  .completed_beat_age_frames=d.output.beat_age,.completed_beat_history=1};
 for(unsigned i=0;i<8;i++)a.future_beat_ms[i]=(i+1)*500;
 struct xz_sync_link_plan plan;VERIFY(xz_sync_link_build(&lease,&clock,&a,1000000,&plan));
 printf("COMPLETED_OUTPUT checks=%u span=%zu history=%zu deck=%zu timeline=%zu extra_deck=%zu extra_decks=%zu anchor=%zu proof=%zu\n",checks,
  sizeof(struct output_span),sizeof(struct output_history),sizeof(struct deck),sizeof(struct xz_extra_timeline),
  sizeof(struct xz_extra_deck),sizeof(struct xz_extra_decks),sizeof(struct xz_sync_link_pcm_anchor),sizeof(struct xp_sp_wire));
 return 0;
}
