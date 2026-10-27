/* Actual software worker, fake PCM sink, no device access. */
#define main retained_extra_main
#include "test_extra_player.c"
#undef main
static struct xz_master_observation key_authority;
static uint32_t native_epoch=7;
static int key_observe(struct xz_master_observation *out){*out=key_authority;out->observed_ms=clock_ms;return 1;}
static int key_owner(int deck,struct xz_key_owner *out){
 *out=(struct xz_key_owner){.native_epoch=native_epoch};return deck<2;
}
static float capture_pcm[200000];static unsigned capture_n,tag_n;
static uint64_t tags[1000];static uint32_t counts[1000];
static int sink_blocked,retry_seen;static float retry_pcm[512];static uint32_t retry_frames;static uint64_t retry_after;
static void record_pcm(const float *pcm,unsigned frames,uint64_t after){
 assert(capture_n+frames*2<=200000&&tag_n<1000);
 memcpy(capture_pcm+capture_n,pcm,frames*2*sizeof(float));capture_n+=frames*2;
 tags[tag_n]=after;counts[tag_n++]=frames;
}
static int retry_check(const float *pcm,unsigned frames,uint64_t after){
 if(retry_seen){assert(frames==retry_frames&&after==retry_after);assert(!memcmp(pcm,retry_pcm,frames*2*sizeof(float)));}
 if(sink_blocked){memcpy(retry_pcm,pcm,frames*2*sizeof(float));retry_frames=frames;retry_after=after;retry_seen=1;return 1;}
 retry_seen=0;return 0;
}
static xz_usb_result key_submit(void *ctx,uint32_t epoch,int d,uint32_t generation,const float *pcm,uint32_t frames,uint64_t after){
 if(d==0&&retry_check(pcm,frames,after))return XZ_USB_AGAIN;
 xz_usb_result r=fake_submit(ctx,epoch,d,generation,pcm,frames,after);
 if(!d&&r==XZ_USB_OK)record_pcm(pcm,frames,after);return r;
}
static xz_usb_result key_transition(void *ctx,uint32_t epoch,int d,uint32_t generation,const float *pcm,uint32_t frames,uint64_t after,uint32_t *next){
 if(d==0&&retry_check(pcm,frames,after))return XZ_USB_BUSY;
 xz_usb_result r=fake_transition(ctx,epoch,d,generation,pcm,frames,after,next);
 if(!d&&r==XZ_USB_OK)record_pcm(pcm,frames,after);return r;
}
static int key_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=resolve(source,id==6?1:id,out);
 if(!rc&&id==6){out->track_id=6;snprintf(out->path,sizeof out->path,"%s/silence.wav",fixtures);}
 return rc;
}
static void begin_key(void){
 clock_ms=1000;wire_horizon=0;position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 capture_n=tag_n=0;sink_blocked=retry_seen=0;native_epoch=7;
 struct xz_xp_env env={{NULL,fake_link,key_submit,fake_flush,fake_position,key_transition},now_ms,key_resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);fake_route(12345);step();load(0,1);load(1,1);
 key_authority=(struct xz_master_observation){.deck=0,.revision=1,.operator_epoch=1};
 xz_xp_set_master_observer(p,key_observe);xz_xp_set_key_owner_reader(p,key_owner);
}
static struct xz_extra_deck_event key_event(int d,enum xz_key_command_kind kind,int step_value){
 struct xz_extra_deck_event e=event(d,XZ_EXTRA_IN_KEY,1);e.key.kind=kind;e.key.step=step_value;
 e.key.source_owner=view.deck[d].key_state.owner;e.key.target_deck=0;e.key.target_valid=1;
 e.key.target_owner.native_epoch=native_epoch;e.key.target=(struct xz_playing_key){.key={6,1},.cents=23};
 e.key.master=key_authority;return e;
}
static void key_press(int d,enum xz_key_command_kind kind,int value){assert(input(key_event(d,kind,value)));step();}
static void end_key(void){xz_xp_destroy(p);p=NULL;}
static double tone_power(int channel,double hz,unsigned first,unsigned frames){
 double re=0,im=0;
 for(unsigned i=0;i<frames;i++){
  double phase=2*3.141592653589793*hz*i/44100.;double v=capture_pcm[(first+i)*2+channel];
  re+=v*cos(phase);im+=v*sin(phase);
 }
 return re*re+im*im;
}
static void pcm_and_tags(void){
 uint64_t baseline_tags[1000];uint32_t baseline_counts[1000];unsigned baseline_n=0;
 for(int shifted=0;shifted<2;shifted++){
  begin_key();press(0,XZ_EXTRA_IN_MASTER_TEMPO);
  if(shifted)key_press(0,XZ_KEY_STEP,1);
  assert(view.deck[0].key_state.applied_revision==0);
  sink_blocked=1;press(0,XZ_EXTRA_IN_PLAY);assert(retry_seen&&capture_n==0);
  unsigned generation=debug(0).generation;step();assert(capture_n==0);
  assert(view.deck[0].key_state.audio_state!=XZ_KEY_AUDIO_APPLIED);
  sink_blocked=0;step();assert(capture_n>0);
  assert(view.deck[0].key_state.applied_revision==0); /* accepted, not completed */
  advance(1100);
  assert(view.deck[0].key_state.audio_state==XZ_KEY_AUDIO_APPLIED);
  assert(abs(view.deck[0].key_state.applied_cents-shifted*100)<=1);
  assert(view.deck[0].key_state.applied_revision==view.deck[0].key_state.request_revision);
  assert(view.deck[0].tempo_x100==0&&!view.deck[0].sync.requested&&debug(0).generation==generation);
  if(!shifted){baseline_n=tag_n;memcpy(baseline_tags,tags,sizeof tags);memcpy(baseline_counts,counts,sizeof counts);}
  else {assert(tag_n==baseline_n&&!memcmp(tags,baseline_tags,tag_n*sizeof *tags)&&!memcmp(counts,baseline_counts,tag_n*sizeof *counts));}
  unsigned frames=16384,first=capture_n/2-frames;double expected=440*exp2(shifted/12.);
  for(int channel=0;channel<2;channel++){
   double best=0,frequency=0;
   for(int hz=100;hz<=1800;hz++){double power=tone_power(channel,hz,first,frames);if(power>best){best=power;frequency=hz;}}
   assert(fabs(frequency-expected)<2);printf("PCM shift=%d channel=%d peak=%.1f expected=%.2f\n",shifted,channel,frequency,expected);
  }
  sink_blocked=1;advance(50);assert(retry_seen);float held[512];memcpy(held,retry_pcm,sizeof held);
  step();assert(!memcmp(held,retry_pcm,sizeof held));sink_blocked=0;step();
  sink_blocked=1;retry_seen=0;seek(0,300);unsigned old=debug(0).generation;
  assert(retry_seen&&!view.deck[0].key_state.playing_valid);step();
  sink_blocked=0;step();assert(debug(0).generation==old+1);advance(100);
  assert(view.deck[0].key_state.playing_valid);
  end_key();
 }
 puts("PASS actual transition/submit PCM, both channels, +100 frequency, identical source tags/frame counts, completion proof, BUSY/AGAIN identity");
}
static void commands_and_owners(void){
 begin_key();
 struct xz_extra_deck_event e=key_event(0,XZ_KEY_STEP,1);
 for(int i=0;i<12;i++)assert(input(e));step();assert(view.deck[0].key_state.desired_cents==1200);
 key_press(0,XZ_KEY_STEP,1);assert(view.deck[0].key_state.desired_cents==1200);
 key_press(1,XZ_KEY_STEP,-1);assert(view.deck[1].key_state.desired_cents==-100&&view.deck[0].key_state.desired_cents==1200);
 key_press(0,XZ_KEY_RESET,0);key_press(1,XZ_KEY_RESET,0);key_press(1,XZ_KEY_STEP,-1);assert(view.deck[1].key_state.desired_cents==-100);
 key_press(1,XZ_KEY_RESET,0);
 e=key_event(0,XZ_KEY_SYNC_TOGGLE,0);assert(input(e));key_authority.revision++;step();assert(!view.deck[0].key_state.engaged);
 e=key_event(0,XZ_KEY_SYNC_TOGGLE,0);assert(input(e));native_epoch++;step();assert(!view.deck[0].key_state.engaged);
 e=key_event(0,XZ_KEY_STEP,1);load(0,1);assert(input(e));step();assert(!view.deck[0].key_state.desired_cents);
 e=key_event(0,XZ_KEY_STEP,1);e.key.source_owner.media_generation++;assert(input(e));step();assert(!view.deck[0].key_state.desired_cents);
 e=key_event(0,XZ_KEY_SYNC_TOGGLE,0);
 for(int i=0;i<XZ_XP_QUEUE;i++){struct xz_extra_deck_event noop=event(0,XZ_EXTRA_IN_KEY,0);assert(input(noop));}
 assert(!input(e));step();assert(!view.deck[0].key_state.engaged&&!view.deck[0].master_tempo);
 for(int deck=0;deck<2;deck++)for(int mt=0;mt<2;mt++)for(int tempo_sync=0;tempo_sync<2;tempo_sync++){
  if(view.deck[deck].master_tempo!=mt)press(deck,XZ_EXTRA_IN_MASTER_TEMPO);
  if(view.deck[deck].sync.requested!=tempo_sync)press(deck,XZ_EXTRA_IN_SYNC);
  struct xz_extra_sync before=view.deck[deck].sync;struct xz_xp_debug db=debug(deck);uint32_t rev=view.deck[deck].tempo_revision;
  key_press(deck,XZ_KEY_SYNC_TOGGLE,0);
  assert(view.deck[deck].key_state.engaged&&view.deck[deck].key_state.desired_cents==123&&view.deck[deck].master_tempo);
  assert(view.deck[deck].sync.requested==before.requested&&view.deck[deck].tempo_revision==rev&&debug(deck).speed==db.speed);
  assert(debug(deck).position==db.position&&debug(deck).generation==db.generation&&!view.deck[deck].master);
  struct xz_extra_deck_event off=key_event(deck,XZ_KEY_SYNC_TOGGLE,0);off.key.target_valid=0;assert(input(off));step();
  assert(!view.deck[deck].key_state.engaged&&view.deck[deck].master_tempo==mt&&!view.deck[deck].key_state.desired_cents);
  key_press(deck,XZ_KEY_SYNC_TOGGLE,0);key_press(deck,XZ_KEY_STEP,1);
  assert(!view.deck[deck].key_state.engaged&&view.deck[deck].master_tempo&&view.deck[deck].key_state.desired_cents==223);
  key_press(deck,XZ_KEY_RESET,0);assert(view.deck[deck].master_tempo);
  key_press(deck,XZ_KEY_SYNC_TOGGLE,0);press(deck,XZ_EXTRA_IN_MASTER_TEMPO);
  assert(!view.deck[deck].key_state.engaged&&!view.deck[deck].key_state.desired_cents);
 }
 key_press(0,XZ_KEY_SYNC_TOGGLE,0);hold(0,XZ_EXTRA_IN_MASTER_TEMPO,1);
 for(int i=0;i<XZ_XP_QUEUE;i++){struct xz_extra_deck_event noop=event(0,XZ_EXTRA_IN_KEY,0);assert(input(noop));}
 assert(input(event(0,XZ_EXTRA_IN_MASTER_TEMPO,0)));step();
 int old_mt=view.deck[0].master_tempo;press(0,XZ_EXTRA_IN_MASTER_TEMPO);assert(view.deck[0].master_tempo!=old_mt);
 key_press(0,XZ_KEY_STEP,1);seek(0,500);assert(view.deck[0].key_state.desired_cents==100);
 e=key_event(0,XZ_KEY_STEP,1);assert(input(e));fake_route(12346);step();assert(view.deck[0].key_state.desired_cents==100&&!view.deck[0].key_state.playing_valid);
 load(0,1);assert(!view.deck[0].key_state.desired_cents&&!view.deck[0].key_state.engaged);
 key_press(0,XZ_KEY_STEP,1);press(0,XZ_EXTRA_IN_PLAY);advance(100);
 capture_n=tag_n=0;load(0,6);press(0,XZ_EXTRA_IN_PLAY);advance(100);
 assert(capture_n&&!view.deck[0].key_state.desired_cents);
 for(unsigned i=0;i<capture_n;i++)assert(capture_pcm[i]==0);
 end_key();puts("PASS FIFO, range, simultaneous software shifts, route/load/media/master identity, queue full, MT ownership, cue persistence and load reset");
}
static void software_master_targets(void){
 for(int source=0;source<2;source++){
  begin_key();int target=1-source;tempo(target,5000);press(target,XZ_EXTRA_IN_PLAY);advance(120);
  const struct xz_deck_key *key=&view.deck[target].key_state;
  assert(key->playing_valid&&key->audio_state==XZ_KEY_AUDIO_APPLIED);
  key_authority=(struct xz_master_observation){.deck=target+2,.revision=2,.operator_epoch=1,
   .request_epoch=1,.lease_generation=1,.route_epoch=key->owner.route_epoch,.load_revision=key->owner.load_revision,
   .transport_generation=view.deck[target].timeline.transport_generation};
  struct xz_extra_deck_event e=key_event(source,XZ_KEY_SYNC_TOGGLE,0);
  e.key.master=key_authority;e.key.target_deck=target+2;e.key.target_owner=key->owner;e.key.target=key->playing;
  assert(input(e));step();assert(view.deck[source].key_state.engaged);
  assert(view.deck[source].key_state.desired_cents==(int)lround(1200*log2(1.05)));
  assert(!view.deck[source].sync.requested&&view.deck[source].tempo_x100==0);
  key_press(source,XZ_KEY_RESET,0);
  e=key_event(source,XZ_KEY_SYNC_TOGGLE,0);e.key.master=key_authority;e.key.target_deck=target+2;
  e.key.target_owner=view.deck[target].key_state.owner;e.key.target=view.deck[target].key_state.playing;
  load(target,1);assert(input(e));step();assert(!view.deck[source].key_state.engaged);
  end_key();
 }
 puts("PASS both software-master directions use completed resampler pitch; target reload rejects captured engagement");
}
static void simultaneous_shifts(void){
 begin_key();
 key_press(0,XZ_KEY_SYNC_TOGGLE,0);key_press(1,XZ_KEY_SYNC_TOGGLE,0);
 assert(view.deck[0].key_state.engaged&&view.deck[1].key_state.engaged);
 assert(view.deck[0].key_state.desired_cents==123&&view.deck[1].key_state.desired_cents==123);
 press(0,XZ_EXTRA_IN_PLAY);press(1,XZ_EXTRA_IN_PLAY);advance(1200);
 for(int i=0;i<2;i++){
  assert(view.deck[i].key_state.audio_state==XZ_KEY_AUDIO_APPLIED);
  assert(abs(view.deck[i].key_state.applied_cents-123)<=1);
  assert(view.deck[i].worker_load.samples&&view.deck[i].worker_load.key_active);
  assert(view.deck[i].worker_load.p99_us<=view.deck[i].worker_load.max_us);
 }
 struct xz_worker_load measured[2];assert(xz_xp_worker_load(p,measured));
 assert(measured[0].key_active&&measured[1].key_active&&measured[0].key_max_us&&measured[1].key_max_us);
 end_key();
 puts("PASS simultaneous software KEY SYNC with fractional shift, MT, completed PCM and measured worker readout");
}
int main(int argc,char **argv){
 assert(argc==2);snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",fixtures);
 pthread_mutex_init(&f.lock,NULL);build_analysis();pcm_and_tags();commands_and_owners();software_master_targets();simultaneous_shifts();pthread_mutex_destroy(&f.lock);return 0;
}
