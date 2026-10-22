// SPDX-License-Identifier: GPL-2.0-only
/* Included after private player types. No native/private transport access. */
__attribute__((visibility("default"),used))
struct xp_sp_wire xz_mods_xp_service_proof_v1={
 .version=XP_SP_VERSION,.bytes=sizeof(struct xp_sp_wire),.header_bytes=48,.deck_bytes=sizeof(struct xp_sp_deck_wire),
 .current_words=XP_SP_CURRENT_WORDS,.input_words=XP_SP_INPUT_WORDS,.history_words=XP_SP_HISTORY_WORDS,.latch_words=XP_SP_LATCH_WORDS,
 .history_capacity=XP_SP_HISTORY,.service_words=XP_SP_SERVICE_WORDS,.counter_words=XP_SP_COUNTER_WORDS,.deck_count=2
};
static atomic_uint sp_instance,sp_owner;
static uint32_t sp_elapsed(uint32_t end,uint32_t begin,uint32_t *future){
 uint32_t delta=end-begin;
 if(delta>INT32_MAX){if(future)*future=1;return XP_SP_UNKNOWN;}
 return delta;
}
static void sp_publish(atomic_uint *seq,atomic_uint *dst,const uint32_t *src,unsigned n){
 uint32_t a=atomic_load_explicit(seq,memory_order_seq_cst);
 atomic_store_explicit(seq,a+1,memory_order_seq_cst);
 for(unsigned i=0;i<n;i++)atomic_store_explicit(dst+i,src[i],memory_order_seq_cst);
 atomic_store_explicit(seq,a==UINT32_MAX-1?2:a+2,memory_order_seq_cst);
}
static void sp_pair(uint32_t *p,uint64_t value){p[0]=(uint32_t)value;p[1]=(uint32_t)(value>>32);}
static void sp_service(struct xp_sp_private *v,unsigned op,int result,uint32_t begin,uint32_t end){
 uint32_t *a=v->current+XP_SP_CURRENT_WORDS+op*8;
 uint32_t cost=sp_elapsed(end,begin,&v->clock_future);
 if(a[0]!=(uint32_t)result)v->history_dirty=1;
 a[0]=(uint32_t)result;a[1]++;
 a[6]=cost;if(cost!=XP_SP_UNKNOWN&&cost>a[7])a[7]=cost;
 if(result){a[2]++;if(!a[3])a[4]=begin;a[3]++;
  uint32_t duration=sp_elapsed(end,a[4],&v->clock_future);
  if(duration!=XP_SP_UNKNOWN&&duration>a[5])a[5]=duration;
 }else{a[3]=0;a[4]=end;}
}
static void sp_capture(struct deck *d,uint32_t now){
 struct xp_sp_private *v=&d->sp;
 struct xp_sp_deck_wire *w=&xz_mods_xp_service_proof_v1.deck[d->index];
 uint32_t *c=v->current;
 c[XP_SP_CURRENT_SESSION]=v->session;c[XP_SP_CURRENT_ATTEMPT]=v->attempt;c[XP_SP_CURRENT_AT_MS]=now;
 c[XP_SP_CURRENT_STATE]=d->state;c[XP_SP_CURRENT_CONTACT]=v->contact;c[XP_SP_CURRENT_VINYL]=d->vinyl;
 c[XP_SP_CURRENT_RESUME]=d->scratch.resume;c[XP_SP_CURRENT_ROUTE]=d->epoch;c[XP_SP_CURRENT_GENERATION]=d->generation;
 c[XP_SP_CURRENT_LOAD_REVISION]=d->load_revision;c[XP_SP_CURRENT_SOURCE_RATE]=d->r->rate;
 c[XP_SP_CURRENT_RELEASING]=d->scratch.releasing;c[XP_SP_CURRENT_OVERFLOW]=d->scratch.overflow;c[XP_SP_CURRENT_NEED_FLUSH]=d->need_flush;
 c[XP_SP_CURRENT_CONTROLS]=v->controls;c[XP_SP_CURRENT_LAST_ARRIVAL]=v->last_arrival;
 c[XP_SP_CURRENT_ARRIVAL_FUTURE]=0;
 c[XP_SP_CURRENT_ARRIVAL_AGE]=v->arrival_seen?sp_elapsed(now,v->last_arrival,c+XP_SP_CURRENT_ARRIVAL_FUTURE):XP_SP_UNKNOWN;
 c[XP_SP_CURRENT_QUEUE_DEPTH]=v->queue_depth;c[XP_SP_CURRENT_MAX_QUEUE]=v->queue_max;
 c[XP_SP_CURRENT_TRAJECTORY]=d->scratch.count;
 if(d->scratch.count>c[XP_SP_CURRENT_MAX_TRAJECTORY])c[XP_SP_CURRENT_MAX_TRAJECTORY]=d->scratch.count;
 c[XP_SP_CURRENT_PENDING]=d->pending;if(d->pending>c[XP_SP_CURRENT_MAX_PENDING])c[XP_SP_CURRENT_MAX_PENDING]=d->pending;
 sp_pair(c+XP_SP_CURRENT_TARGET_LO,d->scratch.target);sp_pair(c+XP_SP_CURRENT_HEARD_LO,d->heard);
 sp_pair(c+XP_SP_CURRENT_SUBMITTED_LO,d->submitted);sp_pair(c+XP_SP_CURRENT_COMPLETED_LO,d->completed);
 c[XP_SP_CURRENT_POSITION_SEEN]=v->position_seen;c[XP_SP_CURRENT_POSITION_AT]=v->position_at;c[XP_SP_CURRENT_POSITION_FUTURE]=0;
 c[XP_SP_CURRENT_POSITION_AGE]=v->position_seen?sp_elapsed(now,v->position_at,c+XP_SP_CURRENT_POSITION_FUTURE):XP_SP_UNKNOWN;
 c[XP_SP_CURRENT_POSITION_APPLIED]=v->position_seen&&v->position_route==d->epoch&&v->position_generation==d->generation;
 for(unsigned op=0;op<XP_SP_OPS;op++){
  uint32_t *a=c+XP_SP_CURRENT_WORDS+op*8;
  if(a[3]){uint32_t duration=sp_elapsed(now,a[4],&v->clock_future);if(duration!=XP_SP_UNKNOWN&&duration>a[5])a[5]=duration;}
 }
 c[XP_SP_CURRENT_CLOCK_FUTURE]=v->clock_future;
 c[XP_SP_CURRENT_INPUT_CALLS]=atomic_load(&w->counter[XP_SP_COUNTER_CALLS]);
 c[XP_SP_CURRENT_INPUT_ACCEPTED]=atomic_load(&w->counter[XP_SP_COUNTER_ACCEPTED]);
 c[XP_SP_CURRENT_INPUT_REFUSED]=atomic_load(&w->counter[XP_SP_COUNTER_REFUSED]);
 c[XP_SP_CURRENT_INPUT_LATEST_MISSED]=atomic_load(&w->counter[XP_SP_COUNTER_LATEST_MISSED]);
}
static void sp_history(struct deck *d,uint32_t now,uint32_t event){
 struct xp_sp_private *v=&d->sp;
 sp_capture(d,now);const uint32_t *c=v->current;
 uint32_t *h=v->history[v->history_next];
 unsigned pairs[][2]={
 {XP_SP_HISTORY_AT_MS,XP_SP_CURRENT_AT_MS},{XP_SP_HISTORY_SESSION,XP_SP_CURRENT_SESSION},
 {XP_SP_HISTORY_ATTEMPT,XP_SP_CURRENT_ATTEMPT},{XP_SP_HISTORY_STATE,XP_SP_CURRENT_STATE},
 {XP_SP_HISTORY_CONTACT,XP_SP_CURRENT_CONTACT},{XP_SP_HISTORY_ROUTE,XP_SP_CURRENT_ROUTE},
 {XP_SP_HISTORY_GENERATION,XP_SP_CURRENT_GENERATION},{XP_SP_HISTORY_TRAJECTORY,XP_SP_CURRENT_TRAJECTORY},
 {XP_SP_HISTORY_QUEUE_DEPTH,XP_SP_CURRENT_QUEUE_DEPTH},{XP_SP_HISTORY_PENDING,XP_SP_CURRENT_PENDING},
 {XP_SP_HISTORY_WORKER_GAP,XP_SP_CURRENT_WORKER_GAP},{XP_SP_HISTORY_WORKER_COST,XP_SP_CURRENT_WORKER_COST},
 {XP_SP_HISTORY_POSITION_AGE,XP_SP_CURRENT_POSITION_AGE},{XP_SP_HISTORY_POSITION_APPLIED,XP_SP_CURRENT_POSITION_APPLIED},
 {XP_SP_HISTORY_SUBMITTED_LO,XP_SP_CURRENT_SUBMITTED_LO},{XP_SP_HISTORY_SUBMITTED_HI,XP_SP_CURRENT_SUBMITTED_HI},
 {XP_SP_HISTORY_COMPLETED_LO,XP_SP_CURRENT_COMPLETED_LO},{XP_SP_HISTORY_COMPLETED_HI,XP_SP_CURRENT_COMPLETED_HI}};
 for(unsigned i=0;i<sizeof pairs/sizeof *pairs;i++)h[pairs[i][0]]=c[pairs[i][1]];
 h[XP_SP_HISTORY_EVENT]=event;
 for(unsigned i=0;i<XP_SP_OPS;i++)h[XP_SP_HISTORY_SUBMIT_RESULT+i]=c[XP_SP_CURRENT_WORDS+i*8];
 struct xp_sp_history_block *b=&xz_mods_xp_service_proof_v1.deck[d->index].history[v->history_next];
 sp_publish(&b->seq,b->word,h,XP_SP_HISTORY_WORDS);
 v->history_next=(v->history_next+1)%XP_SP_HISTORY;if(v->history_count<XP_SP_HISTORY)v->history_count++;
 v->history_at=now;v->history_dirty=0;
}
static void sp_incident(struct deck *d,uint32_t now){
 struct xp_sp_private *v=&d->sp;if(v->latched)return;
 v->current[XP_SP_CURRENT_WORKER_COST]=sp_elapsed(now,v->step_begin,&v->clock_future);
 v->current[XP_SP_CURRENT_FIRST_REASON]=1;v->current[XP_SP_CURRENT_FIRST_AT]=now;
 sp_history(d,now,3); /* pre-reset trajectory full, not a guessed physical cause */
 memcpy(v->first,v->current,sizeof v->current);
 unsigned at=XP_SP_CURRENT_WORDS+XP_SP_SERVICE_WORDS;
 v->first[at++]=v->history_count;v->first[at++]=v->history_next;
 memcpy(v->first+at,v->history,sizeof v->history);at+=XP_SP_HISTORY*XP_SP_HISTORY_WORDS;
 struct xp_sp_deck_wire *w=&xz_mods_xp_service_proof_v1.deck[d->index];
 for(unsigned i=0;i<XP_SP_COUNTER_WORDS;i++)v->first[at++]=atomic_load(&w->counter[i]);
 struct xp_sp_input_block *input[2]={&w->latest,&w->last_refusal};
 for(unsigned i=0;i<2;i++){
  uint32_t status=atomic_load(&input[i]->seq)?2:0;
  if(xp_sp_trycopy(&input[i]->seq,input[i]->word,v->first+at+1,XP_SP_INPUT_WORDS))status=1;
  v->first[at]=status;at+=XP_SP_INPUT_WORDS+1;
 }
 struct xp_sp_latch_block *b=&xz_mods_xp_service_proof_v1.deck[d->index].first;
 sp_publish(&b->seq,b->word,v->first,XP_SP_LATCH_WORDS);v->latched=1;
}
static void sp_newsession(struct deck *d,uint32_t now){
 struct xp_sp_private *v=&d->sp;struct xp_sp_deck_wire *w=&xz_mods_xp_service_proof_v1.deck[d->index];
 if(v->latched){memcpy(v->previous,v->first,sizeof v->first);sp_publish(&w->previous.seq,w->previous.word,v->previous,XP_SP_LATCH_WORDS);}
 memset(v->first,0,sizeof v->first);sp_publish(&w->first.seq,w->first.word,v->first,XP_SP_LATCH_WORDS);
 v->latched=0;v->session++;v->history_next=v->history_count=0;
 /* Resolver is called before a successful load establishes this session.
    Keep its lifetime counters and latest measured call across that boundary. */
 for(unsigned op=0;op<XP_SP_OPS;op++)if(op!=XP_SP_RESOLVER){
  uint32_t *a=v->current+XP_SP_CURRENT_WORDS+op*8;memset(a,0,8*sizeof *a);a[0]=XP_SP_UNKNOWN;
 }
 v->position_seen=0;
 v->current[XP_SP_CURRENT_FIRST_REASON]=v->current[XP_SP_CURRENT_FIRST_AT]=0;
 v->current[XP_SP_CURRENT_MAX_TRAJECTORY]=v->current[XP_SP_CURRENT_MAX_PENDING]=0;
 sp_history(d,now,4);
}
static void sp_input(struct deck *d,const struct xz_extra_deck_event *e,uint32_t now,uint32_t route,int state,int taken,unsigned reason,uint32_t depth,uint32_t owed,int reserved){
 struct xp_sp_deck_wire *w=&xz_mods_xp_service_proof_v1.deck[d->index];
 atomic_fetch_add(&w->counter[XP_SP_COUNTER_CALLS],1);
 atomic_fetch_add(&w->counter[taken?XP_SP_COUNTER_ACCEPTED:XP_SP_COUNTER_REFUSED],1);
 if(reason)atomic_fetch_add(&w->counter[XP_SP_COUNTER_NO_ROUTE+reason-1],1);
 if(taken&&e->kind==XZ_EXTRA_IN_JOG_TOUCH)atomic_fetch_add(&w->counter[e->pressed?XP_SP_COUNTER_ACCEPTED_TOUCH:XP_SP_COUNTER_ACCEPTED_RELEASE],1);
 uint32_t words[XP_SP_INPUT_WORDS]={now,e->kind,(uint32_t)e->pressed,(uint32_t)e->jog.delta,route,(uint32_t)state,(uint32_t)taken,reason,depth,owed,(uint32_t)reserved};
 struct xp_sp_input_block *blocks[2]={&w->latest,&w->last_refusal};
 for(unsigned i=0;i<(taken?1u:2u);i++){
  struct xp_sp_input_block *b=blocks[i];uint32_t a=atomic_load(&b->seq);
  if((a&1)||!atomic_compare_exchange_strong(&b->seq,&a,a+1)){atomic_fetch_add(&w->counter[XP_SP_COUNTER_LATEST_MISSED],1);continue;}
  for(unsigned j=0;j<XP_SP_INPUT_WORDS;j++)atomic_store(&b->word[j],words[j]);
  atomic_store(&b->seq,a==UINT32_MAX-1?2:a+2);
 }
}
static void sp_position_result(struct deck *d,const xz_usb_position *q,uint32_t now){
 struct xp_sp_private *v=&d->sp;uint32_t *c=v->current;
 v->position_seen=1;v->position_at=now;v->position_route=d->epoch;v->position_generation=q->deck[d->index].applied_generation;
 c[XP_SP_CURRENT_REQUESTED_GENERATION]=q->requested_generation[d->index];c[XP_SP_CURRENT_APPLIED_GENERATION]=v->position_generation;
 c[XP_SP_CURRENT_SOURCE_VALID]=q->deck[d->index].source_valid;
 sp_pair(c+XP_SP_CURRENT_WIRE_SUBMITTED_LO,q->wire_submitted_frames);sp_pair(c+XP_SP_CURRENT_WIRE_COMPLETED_LO,q->wire_completed_frames);
 sp_pair(c+XP_SP_CURRENT_STAGED_LO,q->deck[d->index].staged_frames);sp_pair(c+XP_SP_CURRENT_OBSERVED_COMPLETED_LO,q->deck[d->index].completed_frames);
}
static xz_usb_result sp_position(struct xz_xp_players *p,struct deck *d,xz_usb_position *q){
 uint32_t begin=p->env.now_ms();xz_usb_result r=p->env.sink.position(p->env.sink.ctx,d->epoch,q);uint32_t end=p->env.now_ms();
 sp_service(&d->sp,XP_SP_POSITION,r,begin,end);if(r==XZ_USB_OK)sp_position_result(d,q,end);return r;
}
static int sp_read(struct reader *r,int64_t at,uint32_t n,float *pcm){
 if(!r->sp)return xz_stem_media_read(r->media,at,n,pcm);
 uint32_t begin=r->sp_clock();int result=xz_stem_media_read(r->media,at,n,pcm);
 sp_service(r->sp,XP_SP_READER,result,begin,r->sp_clock());return result;
}
#define SP_POSITION(p,d,q) ((p)->sp_enabled?sp_position(p,d,q):(p)->env.sink.position((p)->env.sink.ctx,(d)->epoch,q))
#define SP_READ(r,at,n,pcm) sp_read(r,at,n,pcm)
#define SP_ONLY(x) do{if(p->sp_enabled){x;}}while(0)
