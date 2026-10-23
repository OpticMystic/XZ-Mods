/* Packet-to-current-phase oracle v2. Historical crossing is separate evidence. */
#include "extra_player.c"
#include "sync_link_runtime.h"
#include <stdio.h>
#include <assert.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static struct xz_extra_beat grid[]={{100,1,0},{600,2,0},{1100,3,0},{1600,4,0},{2100,1,0},{2600,2,0},{3100,3,0},{3600,4,0},{4100,1,0}};
static void packet_case(unsigned sr,int side,int loop,int midbeat){
 static struct deck d;static struct reader r;static struct analysis an;
 memset(&d,0,sizeof d);memset(&r,0,sizeof r);memset(&an,0,sizeof an);
 r.rate=sr;r.frames=sr*10;d.r=&r;d.analysis=&an;d.state=XZ_XP_PLAYING;d.generation=7;
 an.grid=grid;an.grid_count=9;
 unsigned start_ms=loop?1600:midbeat?350:100;
 uint64_t source=((uint64_t)sr*start_ms/1000)<<32;
 uint64_t first_step=(uint64_t)((double)sr/44100.*ONE);
 uint64_t last_step=(uint64_t)((double)sr/44100.*1.1*ONE);
 unsigned first=midbeat?0:11025,last=loop?6615:4410,total=first+last;
 uint64_t hi=loop?((uint64_t)sr*2<<32):0;
 if(loop){d.loop.active=1;d.loop.in=0;d.loop.out=sr*2;}
 if(first){d.output.pending=(struct output_span){.source=source,.step=first_step,.loop_out=hi};output_accept(&d.output,0,first);source+=first*first_step;if(hi)source%=hi;}
 d.output.pending=(struct output_span){.source=source,.step=last_step,.loop_out=hi};
 output_accept(&d.output,first,last);source+=last*last_step;if(hi)source%=hi;
 uint64_t now=2000000ULL+total*1000000ULL/44100;
 xz_usb_deck_position q={.applied_generation=7,.completed_frames=total,.source_completed_output_frames=total,
  .source_after_completed=source>>32,.source_completed_wire_frame=total};
 completed_timeline(&d,&q,total,(uint32_t)(now/1000));CHECK(d.output.rate_valid);
 CHECK(midbeat?!d.output.beat_valid:d.output.beat_valid);
 struct xz_extra_deck v={0};v.loaded=v.playing=1;v.grid=grid;v.grid_count=9;v.track_bpm_x100=12000;
 v.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID);
 v.timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_SINK_FRAME|XZ_EXTRA_TIMELINE_APPLIED_RATE;
 v.timeline.route_epoch=1;v.timeline.load_revision=2;v.timeline.transport_generation=7;
 v.timeline.source_rate=sr;v.timeline.source_frame=source>>32;v.timeline.completed_rate=d.output.rate;
 v.timeline.rate_ppm=2000000; /* Current display/request rate must not replace the completed rate. */
 v.timeline.sink_frame=v.timeline.observed_wire_frame=total;v.timeline.at_ms=(uint32_t)(now/1000);
 if(d.output.beat_valid){v.timeline.flags|=XZ_EXTRA_TIMELINE_BEAT_HISTORY;v.timeline.completed_beat_ms=d.output.beat_ms;v.timeline.completed_beat_age_frames=d.output.beat_age;}
 if(loop){v.loop.active=1;v.loop.in_ms=0;v.loop.out_ms=2000;}
 struct xz_sync_link_pcm_anchor a;CHECK(xz_sync_link_anchor_from_deck(side,&v,now,&a));
 CHECK(a.effective_speed==d.output.rate&&a.lane==(unsigned)side);
 double actual_ms=(double)source/ONE*1000./sr;
 unsigned predecessor=actual_ms<100?1600:100+500*(unsigned)((actual_ms-100)/500);
 CHECK(a.beat_source_frame==(double)predecessor*sr/1000.);
 struct xz_sync_link_claim c={.source_id=(uint32_t)side+3,.owner=1,.route_epoch=1,.source_revision=2,.playback_epoch=7,.music_id_known=1,.previous_master_id=0};
 memcpy(c.music_id,a.music_id,8);struct xz_sync_link_lease lease={0};CHECK(xz_sync_link_lease_claim(&lease,&c)==XZ_SYNC_LINK_OK);
 struct xz_sync_link_clock clock={0};struct xz_sync_link_clock_pair pair={now,now+10000000,1};CHECK(xz_sync_link_clock_update(&clock,&pair)==XZ_SYNC_LINK_OK);
 struct xz_sync_link_plan plan;CHECK(xz_sync_link_build(&lease,&clock,&a,now,&plan));
 double rate=plan.q20_speed/1048576.;
 double distance_ms=actual_ms-predecessor;if(distance_ms<0)distance_ms+=2000;
 double elapsed_us=(double)((int64_t)now+10000000-((int64_t)plan.beat_sec*1000000+plan.beat_usec));
 double phase=elapsed_us*rate/(plan.beat_packet[0]*16777216.+plan.beat_packet[1]*65536.+plan.beat_packet[2]*256.+plan.beat_packet[3])/1000.;
 double expected=distance_ms/500.;double error_us=fabs(phase-expected)*500000./rate;
 CHECK(error_us<=1000000./sr/rate+1.);
 CHECK(plan.q20_speed==(uint32_t)(d.output.rate*1048576.));
 if(loop)CHECK(actual_ms<100&&predecessor==1600&&a.completed_beat_history);
 printf("PHASE_V2 sr=%u side=%d loop=%d midbeat=%d phase=%.12f expected=%.12f error_us=%.6f\n",sr,side,loop,midbeat,phase,expected,error_us);
}
static void acquisition(void){
 struct xz_extra_deck v={0};v.loaded=v.playing=1;v.grid=grid;v.grid_count=9;v.track_bpm_x100=12000;
 v.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID);
 v.timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_SINK_FRAME|XZ_EXTRA_TIMELINE_APPLIED_RATE;
 v.timeline.route_epoch=1;v.timeline.load_revision=2;v.timeline.transport_generation=7;v.timeline.source_rate=44100;
 v.timeline.source_frame=2205;v.timeline.completed_rate=1.1;v.timeline.sink_frame=v.timeline.observed_wire_frame=256;v.timeline.at_ms=2000;
 struct xz_sync_link_pcm_anchor a;
 CHECK(!xz_sync_link_anchor_from_deck(0,&v,2000000,&a));
 v.loop.active=1;v.loop.out_ms=2000;CHECK(!xz_sync_link_anchor_from_deck(0,&v,2000000,&a));
 v.timeline.completed_beat_ms=1600;v.timeline.completed_beat_age_frames=18040.;v.timeline.flags|=XZ_EXTRA_TIMELINE_BEAT_HISTORY;
 CHECK(xz_sync_link_anchor_from_deck(0,&v,2000000,&a)&&a.completed_beat_history&&a.source_frame==90405.);
 v.loop.active=0;CHECK(!xz_sync_link_anchor_from_deck(0,&v,2000000,&a));
 v.timeline.flags&=~XZ_EXTRA_TIMELINE_BEAT_HISTORY;v.timeline.source_frame=15876;
 v.timeline.completed_beat_age_frames=NAN;
 CHECK(xz_sync_link_anchor_from_deck(0,&v,2000000,&a)&&!a.completed_beat_history&&a.completed_beat_age_frames==0);
 v.timeline.flags&=~XZ_EXTRA_TIMELINE_APPLIED_RATE;CHECK(!xz_sync_link_anchor_from_deck(0,&v,2000000,&a));
}
int main(void){
 for(unsigned sr=44100;sr<=48000;sr+=3900)for(int side=0;side<2;side++){
  packet_case(sr,side,0,0);packet_case(sr,side,0,1);packet_case(sr,side,1,0);
 }
 acquisition();printf("PHASE_V2 checks=%u failures=0\n",checks);return 0;
}
