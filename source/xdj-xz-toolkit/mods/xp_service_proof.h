// SPDX-License-Identifier: GPL-2.0-only
#ifndef XZ_XP_SERVICE_PROOF_H
#define XZ_XP_SERVICE_PROOF_H
/* Optional own-player proof. All wire words are lock-free 32-bit atomics.
 * A block is coherent only across equal nonzero even sequences. No whole-object snapshot.
 * PRIVATE structs below belong to one deck worker. Inputs use the existing queue lock
 * for depth and a single bounded CAS for latest-event publication. */
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#define XP_SP_VERSION 1u
#define XP_SP_HISTORY 16u
#define XP_SP_OPS 6u
#define XP_SP_UNKNOWN UINT32_MAX
enum xp_sp_current_field {
 XP_SP_CURRENT_INSTANCE,
 XP_SP_CURRENT_SESSION,
 XP_SP_CURRENT_ATTEMPT,
 XP_SP_CURRENT_AT_MS,
 XP_SP_CURRENT_STATE,
 XP_SP_CURRENT_CONTACT,
 XP_SP_CURRENT_VINYL,
 XP_SP_CURRENT_RESUME,
 XP_SP_CURRENT_ROUTE,
 XP_SP_CURRENT_GENERATION,
 XP_SP_CURRENT_LOAD_REVISION,
 XP_SP_CURRENT_SOURCE_RATE,
 XP_SP_CURRENT_RELEASING,
 XP_SP_CURRENT_OVERFLOW,
 XP_SP_CURRENT_NEED_FLUSH,
 XP_SP_CURRENT_CONTROLS,
 XP_SP_CURRENT_LAST_ARRIVAL,
 XP_SP_CURRENT_ARRIVAL_AGE,
 XP_SP_CURRENT_ARRIVAL_FUTURE,
 XP_SP_CURRENT_QUEUE_DEPTH,
 XP_SP_CURRENT_MAX_QUEUE,
 XP_SP_CURRENT_TRAJECTORY,
 XP_SP_CURRENT_MAX_TRAJECTORY,
 XP_SP_CURRENT_PENDING,
 XP_SP_CURRENT_MAX_PENDING,
 XP_SP_CURRENT_TARGET_LO,
 XP_SP_CURRENT_TARGET_HI,
 XP_SP_CURRENT_HEARD_LO,
 XP_SP_CURRENT_HEARD_HI,
 XP_SP_CURRENT_SUBMITTED_LO,
 XP_SP_CURRENT_SUBMITTED_HI,
 XP_SP_CURRENT_COMPLETED_LO,
 XP_SP_CURRENT_COMPLETED_HI,
 XP_SP_CURRENT_WORKER_START,
 XP_SP_CURRENT_WORKER_GAP,
 XP_SP_CURRENT_MAX_WORKER_GAP,
 XP_SP_CURRENT_WORKER_COST,
 XP_SP_CURRENT_MAX_WORKER_COST,
 XP_SP_CURRENT_CLOCK_FUTURE,
 XP_SP_CURRENT_POSITION_SEEN,
 XP_SP_CURRENT_POSITION_AT,
 XP_SP_CURRENT_POSITION_AGE,
 XP_SP_CURRENT_POSITION_FUTURE,
 XP_SP_CURRENT_POSITION_APPLIED,
 XP_SP_CURRENT_REQUESTED_GENERATION,
 XP_SP_CURRENT_APPLIED_GENERATION,
 XP_SP_CURRENT_SOURCE_VALID,
 XP_SP_CURRENT_WIRE_SUBMITTED_LO,
 XP_SP_CURRENT_WIRE_SUBMITTED_HI,
 XP_SP_CURRENT_WIRE_COMPLETED_LO,
 XP_SP_CURRENT_WIRE_COMPLETED_HI,
 XP_SP_CURRENT_STAGED_LO,
 XP_SP_CURRENT_STAGED_HI,
 XP_SP_CURRENT_OBSERVED_COMPLETED_LO,
 XP_SP_CURRENT_OBSERVED_COMPLETED_HI,
 XP_SP_CURRENT_FIRST_REASON,
 XP_SP_CURRENT_FIRST_AT,
 XP_SP_CURRENT_INPUT_CALLS,
 XP_SP_CURRENT_INPUT_ACCEPTED,
 XP_SP_CURRENT_INPUT_REFUSED,
 XP_SP_CURRENT_INPUT_LATEST_MISSED,
 XP_SP_CURRENT_WORDS
};
enum xp_sp_history_field {
 XP_SP_HISTORY_AT_MS,
 XP_SP_HISTORY_EVENT,
 XP_SP_HISTORY_SESSION,
 XP_SP_HISTORY_ATTEMPT,
 XP_SP_HISTORY_STATE,
 XP_SP_HISTORY_CONTACT,
 XP_SP_HISTORY_ROUTE,
 XP_SP_HISTORY_GENERATION,
 XP_SP_HISTORY_TRAJECTORY,
 XP_SP_HISTORY_QUEUE_DEPTH,
 XP_SP_HISTORY_PENDING,
 XP_SP_HISTORY_WORKER_GAP,
 XP_SP_HISTORY_WORKER_COST,
 XP_SP_HISTORY_POSITION_AGE,
 XP_SP_HISTORY_POSITION_APPLIED,
 XP_SP_HISTORY_SUBMIT_RESULT,
 XP_SP_HISTORY_TRANSITION_RESULT,
 XP_SP_HISTORY_FLUSH_RESULT,
 XP_SP_HISTORY_POSITION_RESULT,
 XP_SP_HISTORY_RESOLVER_RESULT,
 XP_SP_HISTORY_READER_RESULT,
 XP_SP_HISTORY_SUBMITTED_LO,
 XP_SP_HISTORY_SUBMITTED_HI,
 XP_SP_HISTORY_COMPLETED_LO,
 XP_SP_HISTORY_COMPLETED_HI,
 XP_SP_HISTORY_WORDS
};
enum xp_sp_input_field {
 XP_SP_INPUT_AT_MS,
 XP_SP_INPUT_KIND,
 XP_SP_INPUT_PRESSED,
 XP_SP_INPUT_DELTA,
 XP_SP_INPUT_ROUTE,
 XP_SP_INPUT_PUBLISHED_STATE,
 XP_SP_INPUT_ACCEPTED,
 XP_SP_INPUT_REASON,
 XP_SP_INPUT_QUEUE_DEPTH,
 XP_SP_INPUT_RELEASE_OWED,
 XP_SP_INPUT_RESERVED_RELEASE,
 XP_SP_INPUT_WORDS
};
enum xp_sp_counter_field {
 XP_SP_COUNTER_CALLS,
 XP_SP_COUNTER_ACCEPTED,
 XP_SP_COUNTER_REFUSED,
 XP_SP_COUNTER_NO_ROUTE,
 XP_SP_COUNTER_NO_RESOLVER,
 XP_SP_COUNTER_STATE_REFUSED,
 XP_SP_COUNTER_ROUTE_CHANGED,
 XP_SP_COUNTER_FIFO_FULL,
 XP_SP_COUNTER_LATEST_MISSED,
 XP_SP_COUNTER_MAX_DEPTH,
 XP_SP_COUNTER_ACCEPTED_TOUCH,
 XP_SP_COUNTER_ACCEPTED_RELEASE,
 XP_SP_COUNTER_START_REQUESTS,
 XP_SP_COUNTER_STOP_REQUESTS,
 XP_SP_COUNTER_WORDS
};
enum xp_sp_op { XP_SP_SUBMIT,XP_SP_TRANSITION,XP_SP_FLUSH,XP_SP_POSITION,XP_SP_RESOLVER,XP_SP_READER };
enum xp_sp_refusal { XP_SP_ADMITTED,XP_SP_NO_ROUTE,XP_SP_NO_RESOLVER,XP_SP_STATE_REFUSED,XP_SP_ROUTE_CHANGED,XP_SP_FIFO_FULL };
/* last result, calls, failures, streak calls, streak begin, max streak ms,
 * last duration ms, max duration ms. Result UINT32_MAX means never called. */
#define XP_SP_SERVICE_WORDS (XP_SP_OPS*8u)
#define XP_SP_LATCH_WORDS (XP_SP_CURRENT_WORDS+XP_SP_SERVICE_WORDS+2u+XP_SP_HISTORY*XP_SP_HISTORY_WORDS+XP_SP_COUNTER_WORDS+2u*(XP_SP_INPUT_WORDS+1u))
#define XP_SP_BLOCK(name,n) struct name { atomic_uint seq; atomic_uint word[n]; }
XP_SP_BLOCK(xp_sp_current_block,XP_SP_CURRENT_WORDS+XP_SP_SERVICE_WORDS);
XP_SP_BLOCK(xp_sp_input_block,XP_SP_INPUT_WORDS);
XP_SP_BLOCK(xp_sp_history_block,XP_SP_HISTORY_WORDS);
XP_SP_BLOCK(xp_sp_latch_block,XP_SP_LATCH_WORDS);
struct xp_sp_deck_wire {
 atomic_uint counter[XP_SP_COUNTER_WORDS];
 struct xp_sp_input_block latest,last_refusal;
 struct xp_sp_current_block current;
 struct xp_sp_history_block history[XP_SP_HISTORY];
 struct xp_sp_latch_block first,previous;
};
struct xp_sp_wire {
 uint32_t version,bytes,header_bytes,deck_bytes,current_words,input_words,history_words,latch_words;
 uint32_t history_capacity,service_words,counter_words,deck_count;
 struct xp_sp_deck_wire deck[2];
};
extern struct xp_sp_wire xz_mods_xp_service_proof_v1;
struct xp_sp_private {
 uint32_t current[XP_SP_CURRENT_WORDS+XP_SP_SERVICE_WORDS];
 uint32_t history[XP_SP_HISTORY][XP_SP_HISTORY_WORDS],history_next,history_count,history_at;
 uint32_t first[XP_SP_LATCH_WORDS],previous[XP_SP_LATCH_WORDS];
 uint32_t step_begin,last_start,have_start,latched,contact,session,attempt;
 uint32_t queue_depth,queue_max,controls,last_arrival,arrival_seen;
 uint32_t position_seen,position_at,position_route,position_generation;
 uint32_t history_dirty,clock_future;
};
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"proof needs lock-free 32-bit atomics");
_Static_assert(sizeof(atomic_uint)==4,"wire requires 32-bit atomic words");
_Static_assert(sizeof(struct xp_sp_wire)==48+2*sizeof(struct xp_sp_deck_wire),"wire padding");
/* Bounded all-atomic trycopy. A reader never reads private worker memory. */
static inline int xp_sp_trycopy(const atomic_uint *seq,const atomic_uint *src,uint32_t *dst,unsigned n){
 uint32_t a=atomic_load_explicit(seq,memory_order_seq_cst);
 if(!a||(a&1))return 0;
 for(unsigned i=0;i<n;i++)dst[i]=atomic_load_explicit(src+i,memory_order_seq_cst);
 uint32_t b=atomic_load_explicit(seq,memory_order_seq_cst);
 return a==b&&!(b&1);
}
#endif
