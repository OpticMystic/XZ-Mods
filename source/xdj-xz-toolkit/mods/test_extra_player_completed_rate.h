/* Actual player and existing fake transport. No native engine or controller. */
static int completed_rate40_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=sync_resolve(source,id,out);if(!rc)out->track_bpm_x100=4000;return rc;
}
static xz_usb_result completed_retry_result;
static int completed_retry_known,completed_retry_watch;
static uint32_t completed_retry_frames,completed_retry_generation;
static uint64_t completed_retry_after;
static float completed_retry_pcm[XZ_USB_BLOCK_FRAMES*2];
static xz_usb_result completed_retry_submit(void *ctx,uint32_t epoch,int side,uint32_t generation,const float *pcm,uint32_t frames,uint64_t after){
 if(completed_retry_watch){
  if(!completed_retry_known){
   completed_retry_known=1;completed_retry_frames=frames;completed_retry_after=after;completed_retry_generation=generation;
   memcpy(completed_retry_pcm,pcm,frames*2*sizeof(float));
  }else{
   assert(frames==completed_retry_frames&&after==completed_retry_after&&generation==completed_retry_generation);
   assert(!memcmp(pcm,completed_retry_pcm,frames*2*sizeof(float)));
  }
  if(completed_retry_result!=XZ_USB_OK)return completed_retry_result;
  completed_retry_watch=0;
 }
 return fake_submit(ctx,epoch,side,generation,pcm,frames,after);
}
static void test_completed_rate(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,0};
 for(int side=0;side<2;side++)for(unsigned id=1;id<=2;id++){
  p=xz_xp_create(&env);assert(p);fake_route(1700);step();load(side,id);seek(side,0);
  if(!view.deck[side].playing)press(side,XZ_EXTRA_IN_PLAY);
  advance(50);assert(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY));
  sync_loop_track(side,id,350);
  if(!view.deck[side].playing)press(side,XZ_EXTRA_IN_PLAY);
  advance(100);assert(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_APPLIED_RATE);
  static const int positions[]={7000,5123,-4123,0};
  for(unsigned k=0;k<sizeof positions/sizeof *positions;k++){
   double before=view.deck[side].timeline.completed_rate;tempo(side,positions[k]);
   assert(view.deck[side].timeline.completed_rate==before);
   advance(100);
   double step=floor((double)debug(side).source_rate/XZ_USB_RATE*debug(side).speed*4294967296.);
   double expected=step/4294967296.*XZ_USB_RATE/debug(side).source_rate;
   assert(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_APPLIED_RATE);
   assert(fabs(view.deck[side].timeline.completed_rate-expected)<1e-12);
  }
  unsigned wraps=0;uint64_t previous=view.deck[side].timeline.source_frame;
  for(unsigned ms=0;ms<5000;ms+=10){
   advance(10);const struct xz_extra_timeline *t=&view.deck[side].timeline;
   if(t->source_frame<previous)wraps++;
   previous=t->source_frame;
   assert(t->flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY);
   if(t->source_frame<debug(side).source_rate/10)assert(t->completed_beat_ms==1600);
  }
  assert(wraps>=2);
  press(side,XZ_EXTRA_IN_PLAY);assert(!view.deck[side].playing);
  assert(!(view.deck[side].timeline.flags&(XZ_EXTRA_TIMELINE_APPLIED_RATE|XZ_EXTRA_TIMELINE_BEAT_HISTORY)));
  load(side,id);assert(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY));
  xz_xp_destroy(p);p=NULL;
 }
 /* The fader can change while exactly the same prepared PCM retries AGAIN and BUSY. */
 env.sink.submit=completed_retry_submit;p=xz_xp_create(&env);assert(p);fake_route(1702);step();sync_loop_track(0,1,350);
 if(!view.deck[0].playing)press(0,XZ_EXTRA_IN_PLAY);
 advance(100);completed_retry_known=0;completed_retry_watch=1;completed_retry_result=XZ_USB_AGAIN;
 for(unsigned n=0;n<20&&!completed_retry_known;n++){clock_ms++;consume(0,44.1);step();}
 assert(completed_retry_known);tempo(0,7000);completed_retry_result=XZ_USB_BUSY;step();
 completed_retry_result=XZ_USB_OK;step();assert(!completed_retry_watch);
 int heard_retry=0;
 for(unsigned n=0;n<100;n++){
  clock_ms++;consume(0,44.1);step();
  if(f.position.deck[0].source_after_completed==completed_retry_after){
   heard_retry=1;assert(view.deck[0].timeline.flags&XZ_EXTRA_TIMELINE_APPLIED_RATE);
   assert(fabs(view.deck[0].timeline.completed_rate-1.)<1e-12);break;
  }
 }
 assert(heard_retry);advance(100);assert(fabs(view.deck[0].timeline.completed_rate-1.07)<1./4294967296.);
 xz_xp_destroy(p);p=NULL;env.sink.submit=fake_submit;
 /* A 40 BPM beat outlives 64 continuously changing accepted spans. */
 static struct buf original,slow;original=dat;file_start(&slow);
 uint32_t section=open_section(&slow,"PQTZ",24);u32(&slow,0);u32(&slow,0x80000);u32(&slow,2);
 for(unsigned i=0;i<2;i++){u16(&slow,i+1);u16(&slow,4000);u32(&slow,100+1500*i);}
 close_section(&slow,section);file_end(&slow);write_file("ANLZ0000.DAT",&slow);
 env.resolve=completed_rate40_resolve;p=xz_xp_create(&env);assert(p);fake_route(1701);step();
 load(0,1);seek(0,100);press(0,XZ_EXTRA_IN_LOOP_IN);seek(0,1600);press(0,XZ_EXTRA_IN_LOOP_OUT);seek(0,100);
 if(!view.deck[0].playing)press(0,XZ_EXTRA_IN_PLAY);
 advance(100);
 for(unsigned ms=0;ms<5000;ms+=10){
  tempo(0,1000+(int)(ms%6000));advance(10);
  assert(view.deck[0].timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY);
  assert(view.deck[0].timeline.completed_beat_ms==100);
 }
 xz_xp_destroy(p);p=NULL;write_file("ANLZ0000.DAT",&original);
 printf("PASS completed applied Q32 rate, delayed tempo request, both decks/source rates, full loop wrap, paused/load gates, AGAIN/BUSY PCM/rate provenance, >64 changing spans at40BPM\n");
}
