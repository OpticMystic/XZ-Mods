/* SPDX-License-Identifier: MIT */
#ifndef XZ_DECK_INPUT_TRACE_H
#define XZ_DECK_INPUT_TRACE_H
#include <stdint.h>
#include <string.h>
#include "deck_select.h"

/* Optional PC-read diagnostic, never a control interface. All words are ARM32
   little-endian. A record is usable only when two external reads agree on its
   nonzero even sequence and ordinal. The whole ring is not an atomic snapshot.
   Only selector outcomes are observed: game-consumed input and player queue
   admission/rendering are outside this proof. Zero-motion updates are counted
   but do not evict the last gesture. No input-thread logging or allocation. */
#define XZ_DECK_INPUT_TRACE_RECORDS 1024u
enum { XZ_DECK_TRACE_FALLBACK=1u, XZ_DECK_TRACE_GLOBAL=2u, XZ_DECK_TRACE_GLOBAL_KEY=3u };
struct xz_deck_input_trace_record {
 uint32_t sequence,ordinal,at_ms,path,channel,code,operation;
 uint32_t delta_bits,value_bits,shift,verdict,logical_deck,controls;
};
struct xz_deck_input_trace {
 uint32_t version,bytes,enabled,capacity,next,dropped,idle,writer;
 struct xz_deck_input_trace_record record[XZ_DECK_INPUT_TRACE_RECORDS];
};
_Static_assert(sizeof(struct xz_deck_input_trace_record)==52,"Input trace record ABI");
_Static_assert(sizeof(struct xz_deck_input_trace)==53280,"Input trace ABI");
_Static_assert(__atomic_always_lock_free(4,0),"Input trace requires lock-free 32-bit atomics");

static inline void xz_deck_input_trace_init(struct xz_deck_input_trace *p,int enabled){
 memset(p,0,sizeof *p);p->version=1;p->bytes=sizeof *p;
 p->capacity=XZ_DECK_INPUT_TRACE_RECORDS;p->enabled=!!enabled;
}
static inline int xz_deck_input_trace_key(uint16_t code){
 return code==0x4101||code==0x4102||code==0x4103||code==0x4104||
        code==0x4305||code==0x4306||code==0x4312||code==0x4313;
}
static inline void xz_deck_input_trace_write(struct xz_deck_input_trace *p,
 const struct xz_ds_key *key,unsigned path,unsigned verdict,uint32_t controls,uint32_t at_ms){
 if(!p||!__atomic_load_n(&p->enabled,__ATOMIC_RELAXED)||!key||key->side<0||key->side>1||
    (path!=XZ_DECK_TRACE_FALLBACK&&path!=XZ_DECK_TRACE_GLOBAL&&path!=XZ_DECK_TRACE_GLOBAL_KEY)||!xz_deck_input_trace_key(key->code))return;
 if(key->code==0x4305&&key->op==XZ_DS_OP_JOG&&!key->delta){
  __atomic_fetch_add(&p->idle,1,__ATOMIC_RELAXED);return;
 }
 uint32_t unlocked=0;
 if(!__atomic_compare_exchange_n(&p->writer,&unlocked,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
  __atomic_fetch_add(&p->dropped,1,__ATOMIC_RELAXED);return;
 }
 uint32_t next=__atomic_load_n(&p->next,__ATOMIC_RELAXED);
 struct xz_deck_input_trace_record *r=&p->record[next%XZ_DECK_INPUT_TRACE_RECORDS];
 uint32_t seq=__atomic_load_n(&r->sequence,__ATOMIC_RELAXED);
 /* The writer is exclusive. Odd/even markers qualify external memory reads. */
 __atomic_store_n(&r->sequence,seq+1u,__ATOMIC_SEQ_CST);
 r->ordinal=next;r->at_ms=at_ms;r->path=path;r->channel=(uint32_t)key->side+1u;
 r->code=key->code;r->operation=key->op;
 memcpy(&r->delta_bits,&key->delta,4);memcpy(&r->value_bits,&key->value,4);
 r->shift=!!key->shift;r->verdict=verdict;r->controls=controls;
 r->logical_deck=(uint32_t)key->side+1u+((controls&(1u<<key->side))?2u:0u);
 __atomic_store_n(&r->sequence,seq+2u,__ATOMIC_RELEASE);
 __atomic_store_n(&p->next,next+1u,__ATOMIC_RELEASE);
 __atomic_store_n(&p->writer,0,__ATOMIC_RELEASE);
}
#endif
