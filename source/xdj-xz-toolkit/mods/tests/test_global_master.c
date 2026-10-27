/* SPDX-License-Identifier: MIT */
#define main legacy_runtime_main
#include "test_sync_link_runtime.c"
#undef main
#include <stdlib.h>

/* The frozen baseline links too: absence is an explicit failing contract. */
extern int xz_sync_link_runtime_master_observe(struct xz_master_observation *) __attribute__((weak));
static int observe(struct xz_master_observation *o){
 memset(o,0xa5,sizeof *o);
 if(xz_sync_link_runtime_master_observe)return xz_sync_link_runtime_master_observe(o);
 memset(o,0,sizeof *o);o->deck=XZ_MASTER_UNKNOWN;return 0;
}
static struct xz_master_observation actual(int deck){
 struct xz_master_observation o;assert(observe(&o));assert(o.deck==deck);assert(o.revision);return o;
}
static void unknown(void){
 struct xz_master_observation o,empty={.deck=XZ_MASTER_UNKNOWN};
 int ok=observe(&o);assert(o.deck==XZ_MASTER_UNKNOWN);
 if(ok){assert(o.operator_epoch);empty.operator_epoch=o.operator_epoch;}
 assert(!memcmp(&o,&empty,sizeof o));
}
static int setter_mode,original_mode,inside_native,deferred_calls,emulate_withdrawal;
static uint32_t fixture_request=10,base_request,reader_request;
static struct xz_extra_decks global_decks;
static struct xz_sync_link_clock_pair global_clock;
static _Atomic int parked,release_park;
static _Thread_local int park_this_thread;
static void *preempting_operator(void *sm){int native=1;hooks.set_master_on(sm,&native);return NULL;}
static int global_setter(void *sm,const int *id){
 if(setter_mode==1){int wrong=*id==3?4:3;return fake_set_link_master(sm,&wrong);}
 if(setter_mode==2)return 0;
 if(setter_mode==4){
  unsigned char status[56];assert(!hooks.get_player_info(fake_link_mgr,id,status));
  pthread_t t;assert(!pthread_create(&t,NULL,preempting_operator,sm));assert(!pthread_join(t,NULL));
  int stale=*id-1;return hooks.set_master_on(sm,&stale);
 }
 int rc=fake_set_link_master(sm,id);return setter_mode==3?-1:rc;
}
static int global_original(void *mgr){
 inside_native++;
 if(park_this_thread){atomic_store(&parked,1);while(!atomic_load(&release_park))sched_yield();}
 if(original_mode==1){original_mode=0;hooks.sync_mgr_sync(mgr);}
 if(original_mode==2){int native=1;hooks.set_master_on(fake_sync_master,&native);original_mode=0;}
 if(original_mode==3){inside_native--;return -1;}
 if(original_mode==4){int invalid=-2;memcpy(fake_sync_master+8,&invalid,4);}
 struct xz_master_observation o;
 if(xz_sync_link_runtime_master_observe)(void)observe(&o);
 int rc=orig_sync_mgr(mgr);inside_native--;return rc;
}
static void deferred(void){
 assert(!inside_native);deferred_calls++;
 if(emulate_withdrawal)global_decks.master_intent.deck=XZ_MASTER_NONE;
 /* Re-enter publication. This deadlocks if delivery holds rt_pub_mutex. */
 xz_sync_link_runtime_step(&global_decks,&global_clock);
}
static void global_reset(int side){
 reset_sim();setter_mode=original_mode=inside_native=deferred_calls=emulate_withdrawal=0;
 atomic_store(&parked,0);atomic_store(&release_park,0);
 struct xz_sync_link_test_ops ops={
  .sync_mgr_sync=global_original,.get_player_info=orig_get_player,.get_beat_info=orig_get_beat,
  .set_player_info=orig_set_player,.set_beat_info=orig_set_beat,.set_master_on=orig_set_master_on,
  .set_link_master=global_setter,.on_use_native_master=deferred,.monotonic_us=fake_monotonic_us
 };
 xz_sync_link_runtime_test_install(&ops,&hooks);
 global_decks=make_decks(side,1000,44100,1000000);
 global_decks.deck[side].master=0;
 struct xz_master_observation token;assert(observe(&token)&&token.operator_epoch);
 base_request=fixture_request+=16;
 global_decks.master_intent=(struct xz_master_intent){.deck=side+2,.epoch=base_request,.explicit_choice=1,.operator_epoch=token.operator_epoch};
 global_clock=(struct xz_sync_link_clock_pair){1000000,UINT64_C(1700000000500000),50};
}
static void step(void){xz_sync_link_runtime_step(&global_decks,&global_clock);}
static void tick(void){
 global_clock.mono_us+=1000;global_clock.realtime_us+=1000;sim_now_us=global_clock.mono_us;
 unsigned side=(unsigned)global_decks.master_intent.deck-2;
 if(side<2){global_decks.deck[side].timeline.at_ms=(uint32_t)(sim_now_us/1000);global_decks.deck[side].timeline.source_frame+=44;}
 step();
}
static void acquire(int side){global_reset(side);hooks.sync_mgr_sync(fake_sync_mgr);step();hooks.sync_mgr_sync(fake_sync_mgr);}
static void pending(void){
 global_reset(0);unknown();hooks.sync_mgr_sync(fake_sync_mgr);
 struct xz_master_observation a=actual(0);step();
 assert(actual(0).revision==a.revision);assert(!xz_sync_link_runtime_master_committed(0));
 hooks.sync_mgr_sync(fake_sync_mgr);assert(simulated_master()==2);actual(2);
 puts("PASS pending request retains confirmed native0; false deck.master still prepares requested source3");
 global_reset(1);int native=1;memcpy(fake_sync_master+8,&native,4);
 hooks.sync_mgr_sync(fake_sync_mgr);step();actual(1);hooks.sync_mgr_sync(fake_sync_mgr);actual(3);
}
static void local(void){
 for(int side=0;side<2;side++){
  acquire(side);struct xz_master_observation a=actual(side+2);
  assert(a.request_epoch==base_request&&a.operator_epoch&&a.lease_generation);
  assert(a.route_epoch==7&&a.load_revision==3&&a.transport_generation==5);
  tick();assert(xz_sync_link_runtime_master_committed(side));
 }
 for(int mode=1;mode<=4;mode++){
  global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);step();setter_mode=mode;
  hooks.sync_mgr_sync(fake_sync_mgr);struct xz_master_observation a;
  if(observe(&a))assert(a.deck!=2);
  if(mode==4)assert(simulated_master()==1);
 }
 /* A matching native ID observed before requesting is foreign, never our ack. */
 global_reset(0);int stale_id=2;memcpy(fake_sync_master+8,&stale_id,4);
 hooks.sync_mgr_sync(fake_sync_mgr);actual(XZ_MASTER_EXTERNAL);step();hooks.sync_mgr_sync(fake_sync_mgr);actual(XZ_MASTER_EXTERNAL);
 puts("PASS source3/4 correlated post-original ack; wrong source, unchanged ID, failed setter and pre-request matching ID refuse owned confirmation");
}
static void guards(void){
 struct xz_va va;struct xz_va_bundle b;struct xz_va_frame f={0};
 memset(&b,0,sizeof b);b.source_id=3;put_le32(b.status,3);
 xz_va_init(&va);assert(xz_va_claim(&va,3,1,xz_va_probe_epoch(&va,3)));
 uint32_t ticket=xz_va_ticket(&va);assert(!xz_va_offer(&va,&b,ticket+1));
 b.source_id=4;assert(!xz_va_offer(&va,&b,ticket));b.source_id=3;put_le32(b.status,4);
 assert(!xz_va_offer(&va,&b,ticket));put_le32(b.status,3);assert(xz_va_offer(&va,&b,ticket));
 xz_va_explicit_request(&va);assert(xz_va_begin(&va,&f)==1);
 xz_va_operator(&va);assert(!xz_va_take_request(&va,&f));xz_va_end(&va,&f);
 atomic_store(&va.operator_epoch,UINT32_MAX);xz_va_operator(&va);assert(!atomic_load(&va.operator_epoch));
 xz_va_operator(&va);assert(!atomic_load(&va.operator_epoch));
 atomic_store(&va.foreign_epoch[0],UINT32_MAX);xz_va_foreign(&va,3);assert(!xz_va_probe_epoch(&va,3));
 for(unsigned boundary=0;boundary<3;boundary++){
  acquire(0);actual(2);
  if(boundary==0)global_decks.deck[0].timeline.route_epoch++;
  if(boundary==1)global_decks.deck[0].timeline.load_revision++;
  if(boundary==2)global_decks.deck[0].timeline.transport_generation++;
  tick();struct xz_master_observation o;if(observe(&o))assert(o.deck!=2);
 }
 acquire(0);actual(2);int id=3;hooks.set_beat_info(fake_link_mgr,&id,NULL,NULL);unknown();
 hooks.sync_mgr_sync(fake_sync_mgr);actual(XZ_MASTER_EXTERNAL);
 struct xz_master_observation a=actual(XZ_MASTER_EXTERNAL);
 hooks.set_beat_info(fake_link_mgr,&id,NULL,NULL);unknown();hooks.sync_mgr_sync(fake_sync_mgr);
 assert(actual(XZ_MASTER_EXTERNAL).revision!=a.revision);
 puts("PASS actual source/tuple/ticket fences and foreign preemption");
}
static void operator_test(void){
 acquire(0);struct xz_master_observation a=actual(2);
 int native=1;hooks.set_master_on(fake_sync_master,&native);unknown();assert(!deferred_calls);
 hooks.sync_mgr_sync(fake_sync_mgr);struct xz_master_observation b=actual(1);
 assert(b.operator_epoch!=a.operator_epoch&&b.revision!=a.revision);step();assert(deferred_calls==0);
 hooks.set_master_on(fake_sync_master,&native);unknown();hooks.sync_mgr_sync(fake_sync_mgr);
 a=actual(1);assert(a.operator_epoch!=b.operator_epoch&&a.revision!=b.revision);
 acquire(0);a=actual(2);global_decks.master_intent.epoch++;tick();unknown();
 assert(!xz_sync_link_runtime_master_committed(0));hooks.sync_mgr_sync(fake_sync_mgr);b=actual(2);
 assert(b.request_epoch==base_request+1&&b.lease_generation!=a.lease_generation&&b.revision!=a.revision);
 /* An unchanged request must not steal back after an operator chooses native. */
 native=1;hooks.set_master_on(fake_sync_master,&native);hooks.sync_mgr_sync(fake_sync_mgr);
 for(unsigned i=0;i<4;i++){tick();hooks.sync_mgr_sync(fake_sync_mgr);actual(1);}
 assert(!xz_sync_link_runtime_master_committed(0));
 /* A new input admitted after the event succeeds on this first press. */
 acquire(0);native=1;hooks.set_master_on(fake_sync_master,&native);
 struct xz_master_observation admission;assert(observe(&admission));
 assert(admission.deck==XZ_MASTER_UNKNOWN&&admission.operator_epoch);
 global_decks.master_intent.epoch++;global_decks.master_intent.operator_epoch=admission.operator_epoch;
 hooks.sync_mgr_sync(fake_sync_mgr);tick();hooks.sync_mgr_sync(fake_sync_mgr);actual(2);
 assert(!deferred_calls);
 puts("PASS native same-ID operator reselection and local same-ID new request require new provenance; callback delivery is deferred and reentrant");
}
static void stale(void){
 acquire(0);actual(2);sim_now_us+=XZ_SYNC_LINK_MAX_AGE_US+1;unknown();
 global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);actual(0);sim_now_us+=XZ_SYNC_LINK_MAX_AGE_US+1;unknown();
 global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);sim_clock_failed=1;unknown();
 global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);sim_now_us--;unknown();
 global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);original_mode=3;hooks.sync_mgr_sync(fake_sync_mgr);unknown();
 original_mode=0;hooks.sync_mgr_sync(fake_sync_mgr);actual(0);
 original_mode=4;hooks.sync_mgr_sync(fake_sync_mgr);unknown();
 global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);void *missing=NULL;
 memcpy(fake_sync_mgr+4,&missing,sizeof missing);hooks.sync_mgr_sync(fake_sync_mgr);unknown();
 global_reset(0);int none=-1;memcpy(fake_sync_master+8,&none,4);hooks.sync_mgr_sync(fake_sync_mgr);actual(XZ_MASTER_NONE);
 int external=4;memcpy(fake_sync_master+8,&external,4);hooks.sync_mgr_sync(fake_sync_mgr);actual(XZ_MASTER_EXTERNAL);
 xz_sync_link_runtime_stop();unknown();struct xz_master_observation stopped;
 assert(!observe(&stopped)&&!stopped.operator_epoch);assert(!xz_sync_link_runtime_master_observe(NULL));
 puts("PASS native and owned expiration, clock failure/backwards, explicit NONE/EXTERNAL and cleared UNKNOWN");
}
static void reentrant(void){
 acquire(0);actual(2);original_mode=1;hooks.sync_mgr_sync(fake_sync_mgr);actual(2);
 original_mode=2;hooks.sync_mgr_sync(fake_sync_mgr);unknown();assert(!deferred_calls);
 hooks.sync_mgr_sync(fake_sync_mgr);actual(1);step();assert(deferred_calls==0);
 puts("PASS observer inside native original, recursive consumer and operator event during borrowed frame");
}
static void *parked_consumer(void *unused){(void)unused;park_this_thread=1;hooks.sync_mgr_sync(fake_sync_mgr);return NULL;}
static void inflight(void){
 global_reset(0);hooks.sync_mgr_sync(fake_sync_mgr);step();
 pthread_t t;assert(!pthread_create(&t,NULL,parked_consumer,NULL));while(!atomic_load(&parked))sched_yield();
 global_decks.master_intent.epoch++;tick();
 atomic_store(&release_park,1);assert(!pthread_join(t,NULL));
 struct xz_master_observation o;if(observe(&o))assert(o.deck!=2);
 assert(!xz_sync_link_runtime_master_committed(0));hooks.sync_mgr_sync(fake_sync_mgr);
 o=actual(2);assert(o.request_epoch==base_request+1);
 /* The older frame's eventual return cannot overwrite a newer operator sample. */
 acquire(0);atomic_store(&parked,0);atomic_store(&release_park,0);
 assert(!pthread_create(&t,NULL,parked_consumer,NULL));while(!atomic_load(&parked))sched_yield();
 int native=1;hooks.set_master_on(fake_sync_master,&native);hooks.sync_mgr_sync(fake_sync_mgr);
 struct xz_master_observation newest=actual(1);
 atomic_store(&release_park,1);assert(!pthread_join(t,NULL));
 o=actual(1);assert(o.operator_epoch==newest.operator_epoch&&o.revision==newest.revision);
 puts("PASS old in-flight consumer cannot confirm new same-ID request");
}
static _Atomic int reader_stop,reader_count;
static void *reader(void *unused){
 (void)unused;while(!atomic_load(&reader_stop)){
  struct xz_master_observation o;if(observe(&o)&&o.deck!=XZ_MASTER_UNKNOWN){
   assert(o.deck==2&&o.request_epoch==reader_request&&o.route_epoch==7&&o.load_revision==3&&o.transport_generation==5);
   assert(o.operator_epoch&&o.lease_generation&&o.revision);atomic_fetch_add(&reader_count,1);
  }else assert(o.deck==XZ_MASTER_UNKNOWN&&!o.revision&&!o.request_epoch&&!o.lease_generation&&
               !o.route_epoch&&!o.load_revision&&!o.transport_generation);
 }return NULL;
}
static void concurrent(void){
 acquire(0);actual(2);reader_request=global_decks.master_intent.epoch;atomic_store(&reader_stop,0);atomic_store(&reader_count,0);pthread_t t[6];
 for(unsigned i=0;i<6;i++)assert(!pthread_create(&t[i],NULL,reader,NULL));
 for(unsigned i=0;i<2000;i++)hooks.sync_mgr_sync(fake_sync_mgr);
 atomic_store(&reader_stop,1);for(unsigned i=0;i<6;i++)assert(!pthread_join(t[i],NULL));assert(atomic_load(&reader_count));
 puts("PASS six concurrent observers during 2000 actual native consumer publications");
}
static void steady(void){
 acquire(0);struct xz_master_observation a=actual(2);
 for(unsigned i=0;i<40;i++){tick();hooks.sync_mgr_sync(fake_sync_mgr);struct xz_master_observation b=actual(2);assert(b.revision==a.revision);assert(b.observed_ms>=a.observed_ms);}
 puts("PASS steady PCM/sample ticks preserve semantic revision");
}
static void native_return(void){
 for(int deck=0;deck<2;deck++)for(int preempt=0;preempt<2;preempt++){
  acquire(1);actual(3);
  global_decks.deck[1].playing=0;global_decks.master_intent.deck=deck;
  global_decks.master_intent.epoch++;global_decks.master_intent.explicit_choice=0;
  if(preempt){int external=4;hooks.set_master_on(fake_sync_master,&external);}
  step();hooks.sync_mgr_sync(fake_sync_mgr);
  if(preempt){int32_t current;memcpy(&current,fake_sync_master+8,4);assert(current==4);}
  else actual(deck);
  assert(!xz_sync_link_runtime_master_committed(1));
 }
 puts("PASS stop handoff restores the requested playing stock deck; operator/external preemption wins");
}
static void paused(void){
 global_reset(0);global_decks.deck[0].playing=0;hooks.sync_mgr_sync(fake_sync_mgr);step();hooks.sync_mgr_sync(fake_sync_mgr);
 actual(0);assert(!xz_sync_link_runtime_master_committed(0));
 puts("PASS negative acceptance: paused software source has no completed playing-PCM bridge authority");
}
static void admission(void){
 reset_sim();struct xz_master_observation token;
 assert(observe(&token)&&token.deck==XZ_MASTER_UNKNOWN&&token.operator_epoch==1&&!token.revision);
 acquire(0);int native=1;
 /* Native event is queued. Bridge has a snapshot before the newer admission. */
 hooks.set_master_on(fake_sync_master,&native);
 struct xz_extra_decks stale_snapshot=global_decks;
 assert(observe(&token)&&token.deck==XZ_MASTER_UNKNOWN&&token.operator_epoch);
 uint32_t accepted=++global_decks.master_intent.epoch;
 global_decks.master_intent.operator_epoch=token.operator_epoch;
 emulate_withdrawal=1;
 xz_sync_link_runtime_step(&stale_snapshot,&global_clock);
 assert(!deferred_calls&&global_decks.master_intent.epoch==accepted&&global_decks.master_intent.deck==2);
 hooks.sync_mgr_sync(fake_sync_mgr);tick();hooks.sync_mgr_sync(fake_sync_mgr);
 struct xz_master_observation o=actual(2);assert(o.request_epoch==accepted&&o.operator_epoch==token.operator_epoch);
 assert(global_decks.master_intent.epoch==accepted); /* No second press. */
 /* A later bridge poll must not rebind an older admission to the new token. */
 acquire(0);uint32_t admitted_before=global_decks.master_intent.operator_epoch;
 native=1;hooks.set_master_on(fake_sync_master,&native);hooks.sync_mgr_sync(fake_sync_mgr);
 global_decks.master_intent.epoch++;assert(global_decks.master_intent.operator_epoch==admitted_before);
 for(unsigned i=0;i<3;i++){tick();hooks.sync_mgr_sync(fake_sync_mgr);actual(1);}
 assert(observe(&token)&&token.operator_epoch!=admitted_before);
 global_decks.master_intent.epoch++;global_decks.master_intent.operator_epoch=token.operator_epoch;
 tick();hooks.sync_mgr_sync(fake_sync_mgr);actual(2);
 /* A native intent does not prepare software ownership or fabricate its ack. */
 acquire(0);global_decks.master_intent.deck=1;global_decks.master_intent.epoch++;
 global_decks.master_intent.operator_epoch=0;tick();
 hooks.sync_mgr_sync(fake_sync_mgr);actual(0); /* guarded restore, not requested native1 */
 native=1;hooks.set_master_on(fake_sync_master,&native);hooks.sync_mgr_sync(fake_sync_mgr);actual(1);
 puts("PASS UNKNOWN/token admission; stale snapshot/new admission/deferred event retains first press; old queued token rejected; native intent stays passthrough");
}
static void missing_snapshot(void){
 acquire(0);int native=1;hooks.set_master_on(fake_sync_master,&native);hooks.sync_mgr_sync(fake_sync_mgr);
 emulate_withdrawal=1;xz_sync_link_runtime_step(NULL,&global_clock);assert(!deferred_calls);
 struct xz_extra_decks empty={0};xz_sync_link_runtime_step(&empty,&global_clock);assert(!deferred_calls);
 actual(1);assert(!xz_sync_link_runtime_master_committed(0));
 puts("PASS missing/zero snapshots preserve global mode and never enable legacy withdrawal");
}
static void restarted_request(void){
 for(int side=0;side<2;side++)for(int same=0;same<2;same++){
  acquire(side);struct xz_master_observation before=actual(side+2);
  struct xz_extra_decks old=global_decks;
  global_reset(side);
  global_decks.master_intent.epoch=same?before.request_epoch:1;
  assert(global_decks.master_intent.operator_epoch>before.operator_epoch);
  hooks.sync_mgr_sync(fake_sync_mgr);step();hooks.sync_mgr_sync(fake_sync_mgr);
  struct xz_master_observation fresh=actual(side+2);
  assert(fresh.request_epoch==global_decks.master_intent.epoch&&fresh.operator_epoch>before.operator_epoch);
  assert(fresh.lease_generation>before.lease_generation);
  /* Even a larger sequence from the retired operator cannot replace it. */
  old.master_intent.epoch=UINT32_MAX-1;
  xz_sync_link_runtime_step(&old,&global_clock);hooks.sync_mgr_sync(fake_sync_mgr);
  struct xz_master_observation after=actual(side+2);
  assert(after.request_epoch==fresh.request_epoch&&after.operator_epoch==fresh.operator_epoch&&after.lease_generation==fresh.lease_generation);
 }
 puts("PASS restart accepts first request with newer operator and reused/lower request number; retired operator never supersedes it");
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"pending"))pending();else if(!strcmp(argv[1],"local"))local();else if(!strcmp(argv[1],"guards"))guards();
 else if(!strcmp(argv[1],"operator"))operator_test();else if(!strcmp(argv[1],"stale"))stale();else if(!strcmp(argv[1],"reentrant"))reentrant();
 else if(!strcmp(argv[1],"inflight"))inflight();else if(!strcmp(argv[1],"concurrent"))concurrent();else if(!strcmp(argv[1],"steady"))steady();else if(!strcmp(argv[1],"paused")){paused();native_return();}
 else if(!strcmp(argv[1],"admission"))admission();else if(!strcmp(argv[1],"missing"))missing_snapshot();
 else if(!strcmp(argv[1],"restart-request"))restarted_request();else assert(0);
 return 0;
}
