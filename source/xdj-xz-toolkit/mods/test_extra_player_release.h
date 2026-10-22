// SPDX-License-Identifier: GPL-2.0-only
/* Real input FIFO -> worker -> PCM sink. No private state assertions. */
static unsigned release_frames,release_calls,release_max_calls,release_trace;
static uint64_t release_last;
static double release_from,release_rate;
static double release_reverse_at;
static double release_reverse_source,release_reverse_rate;
static int release_side,release_bad_pcm,release_fault_stage,release_fault_left;
static int release_complete_transition;
static xz_usb_result release_fault;
static void release_pcm(int side,const float *pcm,uint32_t frames,uint64_t after){
 if(!release_trace||side!=release_side||debug(side).state!=XZ_XP_SCRATCHING)return;
 for(unsigned n=0;n<frames;n++)for(unsigned ch=0;ch<2;ch++){
  double at=release_from+release_rate*debug(side).source_rate/XZ_USB_RATE*(release_frames+n);
  if(release_reverse_at&&release_frames+n>=release_reverse_at)
   at=release_reverse_source+release_reverse_rate*debug(side).source_rate/XZ_USB_RATE*(release_frames+n-release_reverse_at);
  if(fabs(pcm[n*2+ch]-.5*sin(6.283185307179586*440*at/debug(side).source_rate))>=.001)release_bad_pcm=1;
 }
 release_frames+=frames;release_last=after;
}
static xz_usb_result release_inject(int side,int stage){
 release_calls++;
 if(side==release_side&&stage==release_fault_stage&&release_fault_left){
  if(release_fault_left>0)release_fault_left--;
  if(release_fault==XZ_USB_STALE)f.queue_generation[side]++;
  return release_fault;
 }
 return XZ_USB_OK;
}
static xz_usb_result release_submit(void *ctx,uint32_t epoch,int side,uint32_t generation,const float *pcm,uint32_t frames,uint64_t after){
 xz_usb_result r=release_inject(side,1);if(r!=XZ_USB_OK)return r;
 r=fake_submit(ctx,epoch,side,generation,pcm,frames,after);
 if(r==XZ_USB_OK)release_pcm(side,pcm,frames,after);return r;
}
static xz_usb_result release_transition(void *ctx,uint32_t epoch,int side,uint32_t expected,const float *pcm,uint32_t frames,uint64_t after,uint32_t *generation){
 xz_usb_result r=release_inject(side,2);if(r!=XZ_USB_OK)return r;
 r=fake_transition(ctx,epoch,side,expected,pcm,frames,after,generation);
 if(r==XZ_USB_OK){release_pcm(side,pcm,frames,after);if(release_complete_transition)consume(side,frames);}return r;
}
static void release_step(void){
 release_calls=0;step();if(release_calls>release_max_calls)release_max_calls=release_calls;
 assert(release_calls<=18); /* Existing 16-submit worker budget plus transition/retry. */
}
static int release_resolve(int source,uint32_t id,struct xz_extra_track *out){
 if(id!=5)return resolve(source,id,out);
 assert(!resolve(source,4,out));snprintf(out->path,sizeof out->path,"%s/read-failure.wav",fixtures);return 0;
}
static void release_setup(int side,int rate,int playing){
 release_side=side;release_trace=release_frames=release_max_calls=release_bad_pcm=0;release_fault_left=release_complete_transition=0;
 release_reverse_at=0;
 struct xz_xp_env env={{NULL,fake_link,release_submit,fake_flush,fake_position,release_transition},now_ms,release_resolve,master_bpm,NULL,NULL};
 clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(1600);release_step();
 load(side,rate==44100?4:2);seek(side,700);press(side,XZ_EXTRA_IN_VINYL);
 if(playing)press(side,XZ_EXTRA_IN_PLAY);
 release_from=rate*700u/1000u;release_last=(uint64_t)release_from;
}
static void release_enqueue(int side,int delta,int touched){
 clock_ms=2000;if(!touched)assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,1)));
 clock_ms=2020;jog(side,delta);
 clock_ms=2040;assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));
 release_rate=delta*5./16.;if(release_rate>8)release_rate=8;if(release_rate< -8)release_rate=-8;
 release_trace=1;
}
static unsigned release_drain(int side,int consume_pcm){
 unsigned ms=0;
 while(debug(side).state==XZ_XP_SCRATCHING&&ms<65){
  clock_ms++;if(consume_pcm){consume(0,44.1);consume(1,44.1);}
  release_step();ms++;
 }
 assert(ms<=64&&debug(side).state!=XZ_XP_SCRATCHING);release_trace=0;return ms;
}
static void release_close(void){release_trace=0;xz_xp_destroy(p);p=NULL;}
static void test_release_gestures(void){
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int playing=0;playing<2;playing++)for(int sign=-1;sign<=1;sign+=2)for(int touched=0;touched<2;touched++){
  release_setup(side,rate,playing);
  if(touched)hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);
  release_enqueue(side,sign*4,touched);release_step();
  /* This is the original audio RED, independent of terminal-state policy. */
  assert(release_frames>=32&&!release_bad_pcm&&f.peak[side]>.1f);
  assert(debug(side).state==XZ_XP_SCRATCHING); /* Audio owed, release not complete. */
  unsigned ms=release_drain(side,1);
  uint64_t target=(uint64_t)(release_from+sign*rate*20./1000.);
  assert(ms>0&&ms<64&&release_frames>500&&release_frames<1000&&!release_bad_pcm);
  assert(debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
  assert((playing?view.deck[side].timeline.source_frame:debug(side).position)==target&&release_last==target);
  assert(f.position.deck[side].completed_frames==release_frames);
  unsigned frames=release_frames;release_trace=!playing;
  for(int i=0;i<5;i++){clock_ms++;consume(side,44.1);release_step();}
  assert(release_frames==frames);release_close();
 }
 for(int side=0;side<2;side++)for(int sign=-1;sign<=1;sign+=2){
  release_setup(side,44100,0);hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);
  release_rate=sign*2.5;release_trace=1;clock_ms=2020;jog(side,sign*8);release_step();clock_ms++;release_step();
  consume(side,256);clock_ms+=6;release_step();
  assert(f.position.deck[side].completed_frames==256);
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));release_step();release_drain(side,1);
  assert(release_frames==706&&!release_bad_pcm&&f.position.deck[side].completed_frames==256);
  assert(debug(side).position==(uint64_t)(release_from+sign*640));release_close();
 }
 puts("PASS release real FIFO: both sides/rates/directions, press-step schedule, prior paused/playing, all PCM samples, exact completed terminal and no repeats");
}
static void test_release_bounds(void){
 const xz_usb_result faults[]={XZ_USB_BUSY,XZ_USB_AGAIN,XZ_USB_STALE};
 for(int side=0;side<2;side++)for(int stage=1;stage<=2;stage++)for(unsigned i=0;i<3;i++){
  release_setup(side,44100,0);release_enqueue(side,4,0);
  release_fault=faults[i];release_fault_stage=stage;release_fault_left=1;
  release_step();release_drain(side,1);
  if(faults[i]==XZ_USB_STALE){
   assert(release_frames<=256&&!release_bad_pcm&&debug(side).position==30870);
   assert(strstr(view.notice,"SCRATCH RELEASE TRUNCATED"));
  }else{
   assert(release_frames>500&&release_frames<1000&&!release_bad_pcm&&release_last==31752);
   assert(debug(side).position==31752&&f.position.deck[side].completed_frames==release_frames);
  }
  assert(debug(side).state==XZ_XP_CUED);release_close();
 }
 for(int side=0;side<2;side++)for(int stalled=0;stalled<3;stalled++){
  release_setup(side,48000,0);release_enqueue(side,360,0);
  if(stalled){release_fault=stalled==1?XZ_USB_BUSY:XZ_USB_AGAIN;release_fault_stage=2;release_fault_left=-1;}
  release_step();unsigned ms=release_drain(side,0);
  assert(ms==64&&release_frames<=2048&&!release_bad_pcm);
  assert(debug(side).position==(uint64_t)release_from); /* No completed output, no fabricated target. */
  assert(strstr(view.notice,"SCRATCH RELEASE TRUNCATED"));
  release_fault_left=0;unsigned frames=release_frames;
  for(int i=0;i<70;i++){clock_ms++;consume(side,44.1);release_step();}
  assert(release_frames==frames&&debug(side).state==XZ_XP_CUED);release_close();
 }
 for(int side=0;side<2;side++)for(int sign=-1;sign<=1;sign+=2){
  release_setup(side,48000,0);release_enqueue(side,sign*360,0);release_step();
  unsigned ms=release_drain(side,1);
  assert(ms<64&&release_frames==2048&&!release_bad_pcm);
  assert(f.position.deck[side].completed_frames==release_frames&&debug(side).position==release_last);
  assert(strstr(view.notice,"SCRATCH RELEASE TRUNCATED"));release_close();
 }
 /* Deadline is unsigned-wrap safe; repeated releases do not renew it. */
 release_setup(0,44100,0);release_enqueue(0,4,0);position_blocked=1;release_step();
 release_drain(0,1);assert(debug(0).position==30870&&strstr(view.notice,"SCRATCH RELEASE TRUNCATED"));
 position_blocked=0;release_close();
 release_setup(0,44100,0);clock_ms=UINT32_MAX-30;
 assert(input(event(0,XZ_EXTRA_IN_JOG_TOUCH,1)));jog(0,360);assert(input(event(0,XZ_EXTRA_IN_JOG_TOUCH,0)));
 release_rate=8;release_trace=1;release_step();
 for(unsigned i=0;i<64;i++){clock_ms++;assert(input(event(0,XZ_EXTRA_IN_JOG_TOUCH,0)));release_step();}
 assert(debug(0).state==XZ_XP_CUED&&release_frames<=2048);release_close();
 puts("PASS bounded release: BUSY/AGAIN/STALE transition and submit retries without duplicate PCM; frame cap, 64ms timeout, wrap, repeat release and last-completed fallback");
}
static void test_release_cancellation(void){
 for(int side=0;side<2;side++)for(int playing=0;playing<2;playing++)for(int cue=0;cue<2;cue++){
  release_setup(side,44100,playing);release_enqueue(side,4,0);release_step();
  assert(input(event(side,cue?XZ_EXTRA_IN_CUE:XZ_EXTRA_IN_PLAY,1)));release_step();release_trace=0;
  assert(debug(side).state==(cue||playing?XZ_XP_CUED:XZ_XP_PLAYING));
  if(cue||playing)assert(debug(side).position==(cue&&playing?0:(uint64_t)release_from));
  release_close();
 }
 for(int side=0;side<2;side++){
  release_setup(side,44100,0);release_enqueue(side,4,0);release_step();
  consume(side,256);clock_ms+=6;release_step();uint64_t verified=completed_tag(side);
  clock_ms++;fake_route(1601);release_step();
  assert(debug(side).state==XZ_XP_CUED&&debug(side).position==verified);release_close();
  release_setup(side,44100,0);release_enqueue(side,4,0);release_step();
  fake_route(1601);assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,1)));release_step();
  assert(debug(side).state==XZ_XP_SCRATCHING); /* Fresh route gesture survives old tail cancellation. */
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));release_step();
  assert(debug(side).state==XZ_XP_CUED);release_close();
  release_setup(side,44100,0);release_enqueue(side,4,0);release_step();release_trace=0;
  load(side,2);assert(debug(side).source_rate==48000&&debug(side).state==XZ_XP_CUED&&debug(side).position==0);
  clock_ms+=100;release_step();assert(debug(side).position==0);release_close();
  release_setup(side,44100,0);release_enqueue(side,4,0);release_step();release_trace=0;
  load(side,3);assert(debug(side).state==XZ_XP_ERROR);clock_ms+=100;release_step();assert(debug(side).state==XZ_XP_ERROR);release_close();
  /* Destroy is the public unload/lifetime boundary; no public UNLOAD command exists. */
  release_setup(side,44100,0);release_enqueue(side,4,0);release_step();release_close();
  release_setup(side,44100,0);hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);
  clock_ms=2000;jog(side,4);clock_ms=2020;jog(side,-4);
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));release_rate=1.25;
  release_reverse_at=705.6;release_reverse_source=release_from+882;release_reverse_rate=-1.;
  release_trace=1;release_step();release_drain(side,1);
  assert(release_frames==1588&&!release_bad_pcm&&debug(side).position==30870);release_close();
  release_setup(side,44100,0);
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,1)));jog(side,4);
  for(unsigned q=2;q<XZ_XP_QUEUE;q++)jog(side,0);
  assert(!input(event(side,XZ_EXTRA_IN_PLAY,1)));
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));release_rate=1.25;release_trace=1;release_step();release_drain(side,1);
  assert(release_frames>500&&!release_bad_pcm&&debug(side).position==31752);release_close();
 }
 puts("PASS release cancellation: PLAY/CUE precedence, fresh route touch, replacement/load failure, destruction, fast reversal and reserved release at full FIFO");
}
static void test_release_read_failure(void){
 char src[700],dst[700];snprintf(src,sizeof src,"%s/44100.wav",fixtures);snprintf(dst,sizeof dst,"%s/read-failure.wav",fixtures);
 FILE *in=fopen(src,"rb"),*out=fopen(dst,"wb");assert(in&&out);int ch;while((ch=fgetc(in))!=EOF)assert(fputc(ch,out)!=EOF);fclose(in);fclose(out);
 release_setup(0,44100,0);load(0,5);seek(0,700);release_from=debug(0).position;
 out=fopen(dst,"wb");assert(out);fclose(out); /* Worker reads a now-unreadable synthetic source. */
 release_enqueue(0,4,0);release_step();release_trace=0;
 assert(debug(0).state==XZ_XP_CUED&&debug(0).position==(uint64_t)release_from&&release_frames==0);
 assert(strstr(view.notice,"TRACK CANNOT BE READ"));clock_ms+=100;release_step();assert(debug(0).state==XZ_XP_CUED);release_close();
 puts("PASS release read failure: actual media read error cancels tail and preserves completed position");
}
static void test_release_expired_transition(void){
 const unsigned delay[]={63,64,65,100};
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int playing=0;playing<2;playing++)for(int fault=0;fault<2;fault++)for(unsigned t=0;t<4;t++){
  release_setup(side,rate,playing);release_enqueue(side,4,0);
  release_fault_stage=2;release_fault=fault?XZ_USB_AGAIN:XZ_USB_BUSY;release_fault_left=-1;
  release_step();assert(!release_frames);
  clock_ms+=delay[t];release_fault_left=0;release_complete_transition=1;
  /* A real consumer may complete a transition before the worker's next operation. */
  release_step();release_complete_transition=0;
  if(delay[t]<64)assert(release_frames>0&&!release_bad_pcm);
  else{
   assert(!release_frames&&debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
   assert(strstr(view.notice,"SCRATCH RELEASE TRUNCATED"));
   if(!playing)assert(!f.position.deck[side].completed_frames&&debug(side).position==(uint64_t)release_from);
  }
  release_close();
 }
 puts("PASS release expiry before initial transition: BUSY/AGAIN unblocks before/at/after deadline, both rates and resume states");
}
static void test_release_completed_before_expiry(void){
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int playing=0;playing<2;playing++)for(int sign=-1;sign<=1;sign+=2){
  release_setup(side,rate,playing);release_enqueue(side,sign*4,0);release_step();
  unsigned accepted=release_frames;assert(accepted>500&&!release_bad_pcm);
  consume(side,accepted);uint64_t completed=completed_tag(side);
  assert(f.position.deck[side].completed_frames==accepted);
  clock_ms+=64;release_step();
  uint64_t resumed=playing?view.deck[side].timeline.source_frame:debug(side).position;
  assert(resumed==completed&&release_frames==accepted);
  assert(debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
  assert(!strstr(view.notice,"SCRATCH RELEASE TRUNCATED"));
  release_close();
 }
 puts("PASS release completion before expiry: observe completed PCM before deciding deadline retirement");
}
static void test_player_release(void){
 test_release_gestures();test_release_bounds();test_release_cancellation();test_release_read_failure();test_release_expired_transition();test_release_completed_before_expiry();
}
