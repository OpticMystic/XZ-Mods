/* Worker-private proof, emergency and discontinuity invariants. */
#include "extra_player.c"
#include <assert.h>
#include <stdio.h>
static uint32_t tick(void){return 1000;}
int main(void){
 static struct xz_xp_players p;static struct reader r;struct deck *d=&p.deck[0];
 p.env.now_ms=tick;pthread_mutex_init(&p.publish_lock,NULL);atomic_init(&p.revision,0);atomic_init(&p.key_active,0);
 d->r=&r;r.rate=44100;r.frames=44100*60;d->state=XZ_XP_PLAYING;d->epoch=1;d->load_revision=2;d->generation=3;
 key_history_reset(d);
 struct xz_key_owner own={.route_epoch=1,.load_revision=2};
 d->output.pending=(struct output_span){.source=0,.step=ONE,.key={.revision=1,.cents=100,.pitch_rate=1,.state=XZ_KEY_AUDIO_APPLIED}};
 output_accept(&d->output,0,256);assert(!d->output.key_valid);
 xz_usb_deck_position q={.applied_generation=3,.completed_frames=256,.source_completed_output_frames=256,.source_after_completed=256};
 completed_output(d,&q);assert(d->output.key_valid&&d->output.key.revision==1&&d->output.key.cents==100);
 d->output.pending.source=256*ONE;d->output.pending.key.revision=2;d->output.pending.key.cents=200;
 output_accept(&d->output,256,256);assert(d->output.count==2&&d->output.key.cents==100);
 q.completed_frames=q.source_completed_output_frames=q.source_after_completed=512;completed_output(d,&q);
 assert(d->output.key_valid&&d->output.key.cents==200);
 q.applied_generation=4;completed_output(d,&q);assert(!d->output.key_valid);
 output_reset(&d->output);q.applied_generation=3;
 for(unsigned i=0;i<65;i++){
  d->output.pending.source=i*256*ONE;d->output.pending.key.revision=i+1;output_accept(&d->output,i*256,256);
 }
 q.completed_frames=q.source_completed_output_frames=q.source_after_completed=256;completed_output(d,&q);assert(!d->output.key_valid);
 output_reset(&d->output);d->output.pending.source=0;output_accept(&d->output,0,256);
 q.source_after_completed=258;completed_output(d,&q);assert(!d->output.key_valid);
 d->key.desired=600;d->key.revision=9;d->key.engaged=1;key_history_reset(d);
 d->emergency.active=1;d->emergency.length=256;d->emergency.start=0;
 for(unsigned i=0;i<512;i++)d->emergency.pcm[i]=(float)i/1024;
 d->loop.in=0;d->loop.out=256;
 uint64_t written=d->key.shift.written;
 assert(render(d,1));assert(!memcmp(d->pcm,d->emergency.pcm,sizeof d->pcm));
 assert(d->key.shift.written==written&&d->output.pending.key.state==XZ_KEY_AUDIO_EMERGENCY);
 struct xz_key_command reset={.kind=XZ_KEY_RESET,.source_owner=own};
 key_command(&p,d,&reset);assert(d->key.desired==600&&d->key.engaged&&strstr(p.notice,"EMERGENCY"));
 d->emergency.active=0;d->key.engaged=0;jump(d,100,1000);
 assert(d->key.desired==600&&!d->key.shift.written&&!d->output.key_valid);
 assert(d->key.shift.target>1&&d->pending==0);
 d->loop.active=0;r.frames=4096;r.begin=0;r.cached=4096;
 for(unsigned i=0;i<4097;i++){r.pcm[i*2]=.4f*sinf((float)i*.1f);r.pcm[i*2+1]=.3f*cosf((float)i*.13f);}
 for(int path=0;path<3;path++){
  d->state=XZ_XP_PLAYING;d->pos=2000*ONE;d->reverse=path==0;d->search=path==1;d->ramp.active=path==2;
  key_history_reset(d);assert(render(d,1));
  assert(d->pending==256&&d->key.shift.written==256&&d->output.pending.key.state==XZ_KEY_AUDIO_TRANSIENT);
  for(unsigned i=0;i<512;i++)assert(isfinite(d->pcm[i]));
 }
 d->reverse=d->search=d->ramp.active=0;d->state=XZ_XP_SCRATCHING;d->pos=2000*ONE;
 d->scratch.head=0;d->scratch.count=1;d->scratch.group_count=0;
 d->scratch.segment[0]=(struct scratch_segment){.begin=2000*ONE,.end=2100*ONE,.frames=256,.rate=100./256,.direction=1};
 key_history_reset(d);assert(render(d,1));
 assert(d->pending==256&&d->key.shift.written==256&&d->output.pending.key.state==XZ_KEY_AUDIO_TRANSIENT);
 puts("PASS rendered/accepted/completed proof, pitch span separation, stale generation/tag, eviction, emergency bypass/refusal and cue history reset");
 pthread_mutex_destroy(&p.publish_lock);return 0;
}
