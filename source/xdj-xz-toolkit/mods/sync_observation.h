/* Optional own-code observations. Counters are independent modulo-2^32 values.
   Records retain their last material transition, not the latest advancing clock.
   One bounded CAS attempt, no waits. Never interpreted as a whole-object snapshot. */
#ifndef XZ_SYNC_OBSERVATION_H
#define XZ_SYNC_OBSERVATION_H
#include <stdint.h>
#ifndef XZ_SYNC_OBSERVATION
#define XZ_SYNC_OBSERVATION 0
#endif
#if XZ_SYNC_OBSERVATION
#include <stdatomic.h>
_Static_assert(sizeof(uint32_t)==4 && ATOMIC_INT_LOCK_FREE==2,"32-bit lock-free atomics required");
enum xz_syo_counter {
 SYO_C_RECORD_BUSY,
 SYO_C_RECORD_EXHAUSTED,
 SYO_C_RESETS,
 SYO_C_STOPS,
 SYO_C_INSTALL_ATTEMPT,
 SYO_C_INSTALL_ALREADY,
 SYO_C_INSTALL_OK,
 SYO_C_INSTALL_FAILED,
 SYO_C_TEST_INSTALL,
 SYO_C_OWNER_TICK,
 SYO_C_OWNER_DISABLED,
 SYO_C_OWNER_ENABLED,
 SYO_C_OWNER_BUSY,
 SYO_C_OWNER_LOCKED,
 SYO_C_OWNER_NO_DEMAND,
 SYO_C_OWNER_DEMAND,
 SYO_C_OWNER_NO_CLOCK,
 SYO_C_OWNER_THROTTLE,
 SYO_C_OWNER_POLL,
 SYO_C_NATIVE_PUBLISH,
 SYO_C_NATIVE_CLEAR,
 SYO_C_NATIVE_UNQUALIFIED,
 SYO_C_NATIVE_LOAD_FENCE,
 SYO_C_NATIVE_QUALIFIED,
 SYO_C_CACHE_COPY,
 SYO_C_CACHE_NULL,
 SYO_C_CACHE_UNAVAILABLE,
 SYO_C_CACHE_NO_REVISION,
 SYO_C_CACHE_STALE,
 SYO_C_CACHE_OK,
 SYO_C_SNAPSHOT,
 SYO_C_SNAPSHOT_EMPTY,
 SYO_C_REQUEST,
 SYO_C_REQUEST_ACCEPTED,
 SYO_C_REQUEST_REFUSED,
 SYO_C_STEP,
 SYO_C_STEP_DISABLED,
 SYO_C_NO_MASTER,
 SYO_C_CLOCK_BAD,
 SYO_C_ANCHOR_BAD,
 SYO_C_LEASE_RELEASE,
 SYO_C_LEASE_LOST,
 SYO_C_CLAIM_OK,
 SYO_C_CLAIM_FAIL,
 SYO_C_VA_CLAIM_FAIL,
 SYO_C_NOT_READY,
 SYO_C_OCCUPIED,
 SYO_C_PREVIOUS_MASTER_INVALID,
 SYO_C_NO_TICKET,
 SYO_C_BUILD_FAIL,
 SYO_C_OFFER_OK,
 SYO_C_OFFER_FAIL,
 SYO_C_MASTER_PENDING,
 SYO_C_CONSUMER,
 SYO_C_CONSUMER_DISABLED,
 SYO_C_CONSUMER_NO_FRAME,
 SYO_C_CONSUMER_ORIGINAL,
 SYO_C_STATUS_READ,
 SYO_C_STATUS_SCOPE,
 SYO_C_STATUS_FALLBACK,
 SYO_C_STATUS_OK,
 SYO_C_STATUS_REFUSED,
 SYO_C_BEAT_READ,
 SYO_C_BEAT_SCOPE,
 SYO_C_BEAT_FALLBACK,
 SYO_C_BEAT_OK,
 SYO_C_BEAT_REFUSED,
 SYO_C_FRESH_WRONG_SCOPE,
 SYO_C_FRESH_DEAD,
 SYO_C_FRESH_GENERATION,
 SYO_C_FRESH_CLOCK_FAIL,
 SYO_C_FRESH_FUTURE,
 SYO_C_FRESH_EXPIRED,
 SYO_C_FRESH_OK,
 SYO_C_EXPIRED_WITHDRAW,
 SYO_C_FOREIGN_STATUS,
 SYO_C_FOREIGN_BEAT,
 SYO_C_OPERATOR_EXTERNAL,
 SYO_C_OPERATOR_INTERNAL,
 SYO_C_OPERATOR_DUMMY,
 SYO_C_MASTER_CALL,
 SYO_C_MASTER_CALL_OK,
 SYO_C_MASTER_CALL_FAIL,
 SYO_C_MASTER_BEFORE,
 SYO_C_MASTER_AFTER,
 SYO_C_STATUS_NO_SCOPE,
 SYO_C_STATUS_WRONG_MANAGER,
 SYO_C_STATUS_WRONG_SOURCE,
 SYO_C_BEAT_NO_SCOPE,
 SYO_C_BEAT_WRONG_MANAGER,
 SYO_C_BEAT_WRONG_SOURCE,
 SYO_C_PLAN_REASON_0,
 SYO_C_PLAN_REASON_1,
 SYO_C_PLAN_REASON_2,
 SYO_C_PLAN_REASON_3,
 SYO_C_PLAN_REASON_4,
 SYO_C_PLAN_REASON_5,
 SYO_C_PLAN_REASON_6,
 SYO_C_PLAN_REASON_7,
 SYO_C_PLAN_REASON_8,
 SYO_C_PLAN_REASON_9,
 SYO_C_PLAN_REASON_10,
 SYO_C_PLAN_REASON_11,
 SYO_C_NATIVE_REASON_0,
 SYO_C_NATIVE_REASON_1,
 SYO_C_NATIVE_REASON_2,
 SYO_C_NATIVE_REASON_3,
 SYO_C_NATIVE_REASON_4,
 SYO_C_NATIVE_REASON_5,
 SYO_C_NATIVE_REASON_6,
 SYO_C_NATIVE_REASON_7,
 SYO_C_NATIVE_REASON_8,
 SYO_C_NATIVE_REASON_9,
 SYO_C_CACHE_REASON_0,
 SYO_C_CACHE_REASON_1,
 SYO_C_CACHE_REASON_2,
 SYO_C_CACHE_REASON_3,
 SYO_C_CACHE_REASON_4,
 SYO_C_CACHE_REASON_5,
 SYO_C_CACHE_REASON_6,
 SYO_C_CACHE_REASON_7,
 SYO_C_CACHE_REASON_8,
 SYO_C_CACHE_REASON_9,
 SYO_C_SOFT3_REASON_0,
 SYO_C_SOFT3_REASON_1,
 SYO_C_SOFT3_REASON_2,
 SYO_C_SOFT3_REASON_3,
 SYO_C_SOFT3_REASON_4,
 SYO_C_SOFT3_REASON_5,
 SYO_C_SOFT3_REASON_6,
 SYO_C_SOFT3_REASON_7,
 SYO_C_SOFT3_REASON_8,
 SYO_C_SOFT3_REASON_9,
 SYO_C_SOFT3_REASON_10,
 SYO_C_SOFT3_REASON_11,
 SYO_C_SOFT3_REASON_12,
 SYO_C_SOFT3_REASON_13,
 SYO_C_SOFT4_REASON_0,
 SYO_C_SOFT4_REASON_1,
 SYO_C_SOFT4_REASON_2,
 SYO_C_SOFT4_REASON_3,
 SYO_C_SOFT4_REASON_4,
 SYO_C_SOFT4_REASON_5,
 SYO_C_SOFT4_REASON_6,
 SYO_C_SOFT4_REASON_7,
 SYO_C_SOFT4_REASON_8,
 SYO_C_SOFT4_REASON_9,
 SYO_C_SOFT4_REASON_10,
 SYO_C_SOFT4_REASON_11,
 SYO_C_SOFT4_REASON_12,
 SYO_C_SOFT4_REASON_13,
 SYO_COUNTER_COUNT
};
enum xz_syo_record_id {
 SYO_R_INSTALL,
 SYO_R_OWNER,
 SYO_R_NATIVE,
 SYO_R_CACHE,
 SYO_R_SOFT3,
 SYO_R_SOFT4,
 SYO_R_REQUEST3,
 SYO_R_REQUEST4,
 SYO_R_LINK,
 SYO_R_LEASE,
 SYO_R_FRESH,
 SYO_R_MASTER_CALL,
 SYO_R_MASTER_OBSERVED,
 SYO_R_OPERATOR,
 SYO_RECORD_COUNT
};
#define SYO_WORDS 28u
struct xz_syo_record { _Atomic uint32_t seq; _Atomic uint32_t words[SYO_WORDS]; };
struct xz_syo_object {
 uint32_t magic,version,size,counter_count,record_count,record_words,reserved0,reserved1;
 _Atomic uint32_t counters[SYO_COUNTER_COUNT];
 struct xz_syo_record records[SYO_RECORD_COUNT];
};
extern struct xz_syo_object xz_mods_sync_observation_v1;
_Static_assert(sizeof(struct xz_syo_record)==116,"observation record layout");
_Static_assert(sizeof(struct xz_syo_object)==2260,"observation object layout");

static inline void xz_syo_inc(unsigned n){
 if(n<SYO_COUNTER_COUNT)atomic_fetch_add_explicit(&xz_mods_sync_observation_v1.counters[n],1,memory_order_relaxed);
}
static inline uint32_t xz_syo_request_seq(unsigned side){
 uint32_t seq=atomic_load_explicit(&xz_mods_sync_observation_v1.records[side?SYO_R_REQUEST4:SYO_R_REQUEST3].seq,memory_order_acquire);
 return seq&&!(seq&1u)&&seq<UINT32_MAX-2u?seq:0;
}
static inline void xz_syo_record(unsigned row,unsigned keys,const uint32_t *v,unsigned count,int force){
 if(row>=SYO_RECORD_COUNT||count>=SYO_WORDS||keys>count)return;
 struct xz_syo_record *r=&xz_mods_sync_observation_v1.records[row];
 uint32_t seq=atomic_load_explicit(&r->seq,memory_order_acquire);
 if(seq&1u){xz_syo_inc(SYO_C_RECORD_BUSY);return;}
 if(seq>=UINT32_MAX-2u){xz_syo_inc(SYO_C_RECORD_EXHAUSTED);return;}
 uint32_t session=atomic_load_explicit(&xz_mods_sync_observation_v1.counters[SYO_C_RESETS],memory_order_relaxed);
 int changed=force||!seq||atomic_load_explicit(&r->words[0],memory_order_relaxed)!=session;
 for(unsigned i=0;i<keys;i++)changed|=atomic_load_explicit(&r->words[i+1],memory_order_relaxed)!=v[i];
 if(!changed)return;
 uint32_t expected=seq;
 if(!atomic_compare_exchange_strong_explicit(&r->seq,&expected,seq+1,memory_order_acq_rel,memory_order_relaxed)){
  xz_syo_inc(SYO_C_RECORD_BUSY);return;
 }
 atomic_store_explicit(&r->words[0],session,memory_order_relaxed);
 for(unsigned i=0;i<SYO_WORDS-1;i++)atomic_store_explicit(&r->words[i+1],i<count?v[i]:0,memory_order_relaxed);
 atomic_store_explicit(&r->seq,seq+2,memory_order_release);
}
#define SYO_INC(n) xz_syo_inc(SYO_C_##n)
#define SYO_REASON(base,n,bound) do { unsigned syo_n=(unsigned)(n); if(syo_n<(bound))xz_syo_inc(SYO_C_##base+syo_n); } while(0)
#define SYO_REC(row,keys,...) xz_syo_record(SYO_R_##row,keys,(const uint32_t[]){__VA_ARGS__},sizeof((const uint32_t[]){__VA_ARGS__})/sizeof(uint32_t),0)
#define SYO_EVENT(row,keys,...) xz_syo_record(SYO_R_##row,keys,(const uint32_t[]){__VA_ARGS__},sizeof((const uint32_t[]){__VA_ARGS__})/sizeof(uint32_t),1)
#define SYO_LO(v) ((uint32_t)(uint64_t)(v))
#define SYO_HI(v) ((uint32_t)((uint64_t)(v)>>32))
#else
#define SYO_INC(n) ((void)0)
#define SYO_REASON(base,n,bound) ((void)0)
#define SYO_REC(...) ((void)0)
#define SYO_EVENT(...) ((void)0)
#endif
#endif
