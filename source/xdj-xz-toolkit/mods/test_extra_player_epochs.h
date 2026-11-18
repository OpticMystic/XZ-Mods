/* Deterministic producer/worker route races over the existing fake sink. */
static struct {
 pthread_mutex_t lock;
 pthread_cond_t condition;
 int armed, sampled, done, side, accepted;
 uint32_t next_epoch;
 enum xz_extra_input kind;
} recovery_route_scheduler = {PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0,0,0,0,0,XZ_EXTRA_IN_PLAY};

static uint32_t recovery_scheduled_link(void *ctx) {
 (void)ctx;
 uint32_t sampled=fake_link(NULL);
 if(pthread_equal(pthread_self(),owner_thread)) {
  pthread_mutex_lock(&recovery_route_scheduler.lock);
  if(recovery_route_scheduler.armed) {
   recovery_route_scheduler.armed=0;recovery_route_scheduler.sampled=1;
   pthread_cond_broadcast(&recovery_route_scheduler.condition);
   while(!recovery_route_scheduler.done)pthread_cond_wait(&recovery_route_scheduler.condition,&recovery_route_scheduler.lock);
  }
  pthread_mutex_unlock(&recovery_route_scheduler.lock);
 }
 return sampled;
}

static void *recovery_new_route_input(void *unused) {
 (void)unused;
 pthread_mutex_lock(&recovery_route_scheduler.lock);
 while(!recovery_route_scheduler.sampled)pthread_cond_wait(&recovery_route_scheduler.condition,&recovery_route_scheduler.lock);
 pthread_mutex_unlock(&recovery_route_scheduler.lock);
 fake_route(recovery_route_scheduler.next_epoch);
 struct xz_extra_deck_event e=event(recovery_route_scheduler.side,recovery_route_scheduler.kind,1);
 if(e.kind==XZ_EXTRA_IN_LOAD){e.load.source=1;e.load.track_id=2;}
 int accepted=input(e);
 pthread_mutex_lock(&recovery_route_scheduler.lock);
 recovery_route_scheduler.accepted=accepted;recovery_route_scheduler.done=1;
 pthread_cond_broadcast(&recovery_route_scheduler.condition);
 pthread_mutex_unlock(&recovery_route_scheduler.lock);
 return NULL;
}

static int recovery_restored_input(int side,enum xz_extra_input kind) {
 struct xz_xp_env env={{NULL,recovery_scheduled_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,order_resolve,0,0};
 owner_thread=pthread_self();
 clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(9000);step();load(side,4);
 if(kind==XZ_EXTRA_IN_JOG_TOUCH)press(side,XZ_EXTRA_IN_VINYL);
 owner_thread=pthread_self();
 pthread_mutex_lock(&recovery_route_scheduler.lock);
 recovery_route_scheduler.side=side;recovery_route_scheduler.kind=kind;recovery_route_scheduler.next_epoch=9001;
 recovery_route_scheduler.sampled=recovery_route_scheduler.done=recovery_route_scheduler.accepted=0;recovery_route_scheduler.armed=1;
 pthread_mutex_unlock(&recovery_route_scheduler.lock);
 pthread_t producer;assert(!pthread_create(&producer,NULL,recovery_new_route_input,NULL));
 stepping_side=side;xz_xp_step(p,side);
 assert(!pthread_join(producer,NULL));
 step();
 int observed;
 if(kind==XZ_EXTRA_IN_VINYL)observed=view.deck[side].vinyl;
 else if(kind==XZ_EXTRA_IN_JOG_TOUCH)observed=debug(side).state==XZ_XP_SCRATCHING;
 else observed=debug(side).source_rate==48000;
 printf("RECOVERED_INPUT side=%d kind=%d accepted=%d observed=%d epoch=%u state=%d rate=%u vinyl=%d\n",
  side,kind,recovery_route_scheduler.accepted,observed,view.deck[side].timeline.route_epoch,debug(side).state,debug(side).source_rate,view.deck[side].vinyl);
 assert(recovery_route_scheduler.accepted==1);
 xz_xp_destroy(p);p=NULL;assert(!xz_extra_order_live());
 return !observed;
}

static uint32_t recovery_load_reconnect;
static int recovery_reconnecting_resolver(int source,uint32_t id,struct xz_extra_track *out) {
 int rc=resolve(source,id,out);
 if(recovery_load_reconnect){uint32_t next=recovery_load_reconnect;recovery_load_reconnect=0;fake_route(next);}
 return rc;
}
static int recovery_stale_inflight_load(int side,int explicit_epoch) {
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,recovery_reconnecting_resolver,master_bpm,NULL,order_resolve,0,0};
 clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(9100);step();load(side,4);
 struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_LOAD,1);
 e.load.source=1;e.load.track_id=2;e.load.route_epoch=explicit_epoch?9100:0;
 recovery_load_reconnect=9101;assert(input(e));step();step();
 int replaced=debug(side).source_rate==48000;
 printf("INFLIGHT_LOAD side=%d public_epoch=%u route=%u replaced=%d rate=%u\n",side,e.load.route_epoch,fake_link(NULL),replaced,debug(side).source_rate);
 xz_xp_destroy(p);p=NULL;assert(!xz_extra_order_live());
 return replaced;
}

static void test_recovery_enqueue_races(void) {
 const enum xz_extra_input kinds[]={XZ_EXTRA_IN_VINYL,XZ_EXTRA_IN_LOAD};
 for(int side=0;side<2;side++)for(unsigned k=0;k<sizeof kinds/sizeof *kinds;k++)
  assert(!recovery_restored_input(side,kinds[k]));
 for(int side=0;side<2;side++)for(int explicit_epoch=0;explicit_epoch<2;explicit_epoch++)
  assert(!recovery_stale_inflight_load(side,explicit_epoch));
 puts("PASS recovered-route commands survive an older worker sample; stale in-flight LOAD preserves its predecessor");
}
