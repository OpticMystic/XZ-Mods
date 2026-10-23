/* Minimal actual-player RED: an old underrun must not erase new-generation accepted provenance. */
#define main retained_main
#include "test_extra_player.c"
#undef main
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("RED line=%d %s\n",__LINE__,#x);}}while(0)
int main(int argc,char **argv){
 assert(argc==2);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 for(int side=0;side<2;side++)for(unsigned id=1;id<=2;id++)for(int underrun=0;underrun<2;underrun++){
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,0};
  p=xz_xp_create(&env);assert(p);fake_route(19100+side*2+underrun);step();sync_loop_track(side,id,350);
  if(!view.deck[side].playing)press(side,XZ_EXTRA_IN_PLAY);
  advance(100);
  if(underrun){
   /* Let queued PCM run out using only the existing consumer; then recover. */
   clock_ms+=100;consume(side,4410);step();advance(100);
  }
  uint64_t old_underrun=f.position.deck[side].underrun_frames;
  CHECK(underrun?old_underrun>0:old_underrun==0);
  seek(side,600);
  uint32_t generation=debug(side).generation;
  CHECK(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY));
  /* This applies the already queued new generation and completes its first block. */
  clock_ms+=6;consume(side,256);step();
  const struct xz_extra_timeline *t=&view.deck[side].timeline;
  CHECK(f.position.deck[side].applied_generation==generation);
  CHECK(f.position.deck[side].underrun_frames==0);
  CHECK(f.position.deck[side].source_valid==3&&f.position.deck[side].source_completed_output_frames==256);
  printf("RESET side=%d prior_underrun=%llu new_underrun=%llu generation=%u completed=%llu tag=%llu flags=%u applied=%d beat=%d\n",side,
   (unsigned long long)old_underrun,(unsigned long long)f.position.deck[side].underrun_frames,generation,
   (unsigned long long)f.position.deck[side].source_completed_output_frames,
   (unsigned long long)f.position.deck[side].source_after_completed,t->flags,
   !!(t->flags&XZ_EXTRA_TIMELINE_APPLIED_RATE),!!(t->flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY));
  CHECK(t->flags&XZ_EXTRA_TIMELINE_APPLIED_RATE);
  CHECK(t->flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY);
  unsigned recovery=0;
  while(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY)&&recovery<1500){advance(10);recovery+=10;}
  printf("RECOVERY side=%d cause=%d beat_recovery_ms=%u\n",side,underrun,recovery);
  CHECK(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY);
  CHECK(recovery==0);
  /* A real same-generation gap must still invalidate the accepted ledger. */
  f.position.deck[side].underrun_frames++;step();
  CHECK(!(view.deck[side].timeline.flags&(XZ_EXTRA_TIMELINE_APPLIED_RATE|XZ_EXTRA_TIMELINE_BEAT_HISTORY)));
  xz_xp_destroy(p);p=NULL;
 }
 printf("COMPLETED_GENERATION checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
