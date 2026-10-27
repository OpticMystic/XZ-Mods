/* Included by test_sync_engine.c: real worker, input queue and completed PCM sink. */
static int snap_phase_missing;
static int snap_native(struct xz_sync_native_snapshot *out){
 int rc=copy_sim_native(out);
 if(!out->playing)out->bar_phase=(float)sim_native.anchor_phase;
 if(snap_phase_missing)out->phase_available=0;
 return rc;
}
static void snap_begin(int d,int master,int q,unsigned den,int playing){
 engine_begin(q,1);if(d)load(d,1);
 snap_phase_missing=0;xz_xp_set_quantize_grid(p,den,1);
 if(master<2){
  engine_authority.deck=master;
  sim_native=(__typeof__(sim_native)){.active=1,.native_deck=master,.playing=playing,
   .source_revision=7,.track_identity=123,.bpm=120,.anchor_phase=0,
   .anchor_us=(uint64_t)clock_ms*1000u};
  xz_xp_set_native_sync_clock(p,snap_native);
 }else{
  int m=1-d;load(m,1);seek(m,600);
  if(playing)press(m,XZ_EXTRA_IN_PLAY);
  press(m,XZ_EXTRA_IN_MASTER);
  const struct xz_extra_timeline *t=&view.deck[m].timeline;
  engine_authority=(struct xz_master_observation){.deck=m+2,.revision=2,.operator_epoch=1,
   .request_epoch=view.master_intent.epoch,.lease_generation=2,.route_epoch=t->route_epoch,
   .load_revision=t->load_revision,.transport_generation=t->transport_generation};
  press(m,XZ_EXTRA_IN_SYNC); /* Keep the master's accepted phase segments available while the follower is off. */
 }
 seek(d,805);if(playing)press(d,XZ_EXTRA_IN_PLAY);
 step();if(playing)advance(60);
}
static double snap_delta(double beats,unsigned den){
 double units=beats*den;return (units-floor(units+.5))/den;
}
static double snap_expected(int d,int master,unsigned den){
 double heard=(double)f.position.deck[d].source_after_completed;
 double target;
 if(master<2)target=sim_native_phase_at_us((uint64_t)clock_ms*1000u+(uint64_t)wire_horizon*1000000u/XZ_USB_RATE);
 else{
  const xz_usb_deck_position *m=&f.position.deck[1-d];
  double at=(double)m->source_after_completed+(f.wire[d]+wire_horizon-(double)m->source_completed_wire_frame);
  target=(at/44100.-.1)*2.;
 }
 double phase=(heard/44100.-.1)*2.;
 return heard-snap_delta(phase-target,den)*22050.;
}
static void snap_check_block(int d,unsigned first,double expected,unsigned den){
 assert(f.write[d]>first);
 const struct block *b=&f.queue[d][first%FAKE_BLOCKS];
 assert(b->generation==debug(d).generation);
 double start=(double)b->after-b->frames;
 double distance=fabs(start-expected);
 if(distance>1.)fprintf(stderr,"snap target deck=%d start=%.3f expected=%.3f distance=%.3f quantum=%.3f\n",d+3,start,expected,distance,22050./den);
 assert(distance<=1.); /* zero quantized distance to source-frame rounding */
}
static void snap_running(void){
 unsigned cases=0;
 for(int d=0;d<2;d++)for(int master=0;master<3;master++)for(int q=0;q<2;q++)
 for(unsigned den=1;den<=8;den*=2)for(int large=0;large<2;large++){
  snap_begin(d,master,q,den,1);
  press(d,XZ_EXTRA_IN_SYNC);advance(60);
  press(d,XZ_EXTRA_IN_SYNC);
  /* A real seek while off introduces both small and large errors inside the last snap's cooldown. */
  double offset_ms=large?200./den:5.;
  int32_t seek_ms=(int32_t)llround(snap_expected(d,master,den)/44.1+offset_ms);
  seek(d,seek_ms);advance(40);
  double audible=master<2?actual_native_sync_error(d):actual_sync_error(1-d,d);
  audible=snap_delta(audible*2.,den)/2.;
  assert(fabs(audible-offset_ms/1000.)<=.0006);
  for(int repeat=0;repeat<2;repeat++){
   unsigned before=f.flushes[d],first=f.write[d];double expected=snap_expected(d,master,den);
   press(d,XZ_EXTRA_IN_SYNC);
   assert(view.deck[d].sync.requested&&view.deck[d].sync.tempo_matched);
   assert(f.flushes[d]==before+(unsigned)q);
   if(q)snap_check_block(d,first,expected,den);
   advance(40);assert(f.flushes[d]==before+(unsigned)q);
   if(q){
    double completed=master<2?actual_native_sync_error(d):actual_sync_error(1-d,d);
    assert(fabs(snap_delta(completed*2.,den))/2.<=1./44100.+.000001);
   }
   press(d,XZ_EXTRA_IN_SYNC);
  }
  engine_end();cases++;
 }
 printf("PASS explicit re-engage: %u running cases, two presses within cooldown, all quanta, Q on/off, decks 3/4 and all masters\n",cases);
}
static void snap_stationary(void){
 unsigned cases=0;
 for(int d=0;d<2;d++)for(int master=0;master<3;master++)for(int q=0;q<2;q++)
 for(unsigned den=1;den<=8;den*=2)for(int large=0;large<2;large++){
  snap_begin(d,master,q,den,0);
  for(int repeat=0;repeat<2;repeat++){
   int ms=large?805:605;seek(d,ms);
   uint64_t before=debug(d).position;
   double expected=600.+floor((ms-600.)/(500./den)+.5)*(500./den);
   press(d,XZ_EXTRA_IN_SYNC);
   uint64_t target=q?(uint64_t)llround(expected*debug(d).source_rate/1000.):before;
   if(!near_frames(debug(d).position,target,1))fprintf(stderr,"stationary d=%d m=%d q=%d den=%u large=%d repeat=%d at=%llu target=%llu\n",d,master,q,den,large,repeat,(unsigned long long)debug(d).position,(unsigned long long)target);
   assert(near_frames(debug(d).position,target,1));
   assert(view.deck[d].sync.tempo_matched&&!view.deck[d].sync.locked);
   assert(debug(d).state==XZ_XP_CUED);
   press(d,XZ_EXTRA_IN_SYNC);
  }
  engine_end();cases++;
 }
 printf("PASS explicit stationary snap: %u cases, small/large offsets, all quanta, Q on/off, native/software masters\n",cases);
}
static void snap_late_phase(void){
 for(int d=0;d<2;d++)for(int master=0;master<2;master++){
  snap_begin(d,master,1,1,0);snap_phase_missing=1;seek(d,605);
  press(d,XZ_EXTRA_IN_SYNC);uint64_t before=debug(d).position;
  advance(50);assert(debug(d).position==before&&!view.deck[d].sync.locked);
  snap_phase_missing=0;step();assert(near_frames(debug(d).position,ms_frames(d,600),1));
  unsigned generation=debug(d).generation;step();assert(debug(d).generation==generation);
  sim_native.playing=1;sim_native.anchor_us=(uint64_t)clock_ms*1000u;
  press(d,XZ_EXTRA_IN_PLAY);advance(100);
  assert(view.deck[d].sync.requested);assert(fabs(actual_native_sync_error(d))<=.001);
  engine_end();
 }
 puts("PASS pending native phase: paused press, first valid stationary pass, one snap, paused-then-play recovery");
}
static void snap_manual(void){
 unsigned cases=0;
 for(int d=0;d<2;d++)for(int master=0;master<3;master++)for(int q=0;q<2;q++)
 for(unsigned den=1;den<=8;den*=2)for(int press_again=0;press_again<2;press_again++){
  snap_begin(d,master,q,den,1);press(d,XZ_EXTRA_IN_SYNC);advance(60);
  jog(d,8);step();assert(view.deck[d].sync.reason==XZ_EXTRA_SYNC_MANUAL);
  unsigned before=f.flushes[d];int snapped=0;
  /* An off/on press during the gesture also remains pending until the hand releases control. */
  if(press_again){press(d,XZ_EXTRA_IN_SYNC);press(d,XZ_EXTRA_IN_SYNC);}
  assert(f.flushes[d]==before);
  for(unsigned elapsed=0;elapsed<500;elapsed+=10){
   clock_ms+=10;consume(0,441);consume(1,441);
   unsigned first=f.write[d];double expected=snap_expected(d,master,den);step();
   if(f.flushes[d]!=before){
    assert(q&&!snapped&&f.flushes[d]==before+1);snap_check_block(d,first,expected,den);
    snapped=1;before=f.flushes[d];
   }
  }
  assert(snapped==q);assert(view.deck[d].sync.tempo_matched);
  engine_end();cases++;
 }
 printf("PASS manual re-engage: %u cases, pending during bend, first valid pass snaps once inside cooldown\n",cases);
}
static void snap_source_fences(void){
 for(int d=0;d<2;d++)for(int change=0;change<3;change++){
  snap_begin(d,0,1,1,1);press(d,XZ_EXTRA_IN_SYNC);advance(60);
  press(d,XZ_EXTRA_IN_SYNC);unsigned before=f.flushes[d];
  if(change==0)sim_native.track_identity++;
  if(change==1)sim_native.source_revision++;
  if(change==2){sim_native.native_deck=1;engine_authority.deck=1;engine_authority.revision++;}
  press(d,XZ_EXTRA_IN_SYNC);assert(f.flushes[d]==before);
  step();assert(f.flushes[d]==before); /* Same timestamp cannot qualify a new source. */
  clock_ms+=10;consume(0,441);consume(1,441);
  unsigned first=f.write[d];double expected=snap_expected(d,0,1);step();
  assert(f.flushes[d]==before+1);snap_check_block(d,first,expected,1);
  engine_end();
 }
 puts("PASS explicit pending snap keeps native deck/track/revision coherence fences and snaps on first qualified capture");
}
static void snap_paused_software(void){
 for(int d=0;d<2;d++)for(unsigned den=1;den<=8;den*=2){
  snap_begin(d,2,1,den,0);xz_xp_set_quantize_grid(p,den,0);seek(d,605);
  press(d,XZ_EXTRA_IN_SYNC);uint64_t before=debug(d).position;
  step();assert(debug(d).position==before);
  xz_xp_set_quantize_grid(p,den,1);step();assert(near_frames(debug(d).position,ms_frames(d,600),1));
  unsigned generation=debug(d).generation;step();assert(debug(d).generation==generation);
  press(1-d,XZ_EXTRA_IN_PLAY);
  const struct xz_extra_timeline *t=&view.deck[1-d].timeline;
  engine_authority.transport_generation=t->transport_generation;engine_authority.revision++;
  press(d,XZ_EXTRA_IN_PLAY);advance(100);
  assert(fabs(snap_delta(actual_sync_error(1-d,d)*2.,den))/2.<=.001);
  engine_end();
 }
 puts("PASS pending software phase: unknown quantum restored, stationary once, paused-then-play alignment");
}
static void explicit_snap(void){
 snap_stationary();snap_running();snap_late_phase();snap_manual();snap_source_fences();snap_paused_software();
}
