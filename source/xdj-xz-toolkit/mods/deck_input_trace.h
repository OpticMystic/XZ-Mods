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
/* Writer-private jog history per side and path (not exported; the trace ABI is unchanged). A
   zero-motion jog update (delta 0) is retained when it
   - is the first after a recorded motion update,
   - changes the speed class: zero, forward, reverse or non-finite (first known zero after moving), or
   - carries new speed bits, for at most XZ_DECK_INPUT_STILL_TAIL records per still period,
   so a platter slowing to rest leaves its speed decay and its first zero in the ring. Repeated
   identical idle updates, and still updates past the tail, are only counted. */
#define XZ_DECK_INPUT_STILL_TAIL 8u
struct xz_deck_input_motion {
 uint8_t moving[2][4], speed_class[2][4], tail[2][4];
 uint32_t value_bits[2][4];
};
static inline uint8_t xz_deck_input_speed_class(float value){
 if(value!=value||value-value!=0.0f)return 3u;  /* NaN or infinite */
 return value>0.0f?1u:value<0.0f?2u:0u;
}
static inline void xz_deck_input_trace_write_motion(struct xz_deck_input_trace *p,struct xz_deck_input_motion *m,
 const struct xz_ds_key *key,unsigned path,unsigned verdict,uint32_t controls,uint32_t at_ms){
 if(!p||!__atomic_load_n(&p->enabled,__ATOMIC_RELAXED)||!key||key->side<0||key->side>1||
    (path!=XZ_DECK_TRACE_FALLBACK&&path!=XZ_DECK_TRACE_GLOBAL&&path!=XZ_DECK_TRACE_GLOBAL_KEY)||!xz_deck_input_trace_key(key->code))return;
 int jog=key->code==0x4305&&key->op==XZ_DS_OP_JOG,idle=jog&&!key->delta;
 if(idle&&!m){__atomic_fetch_add(&p->idle,1,__ATOMIC_RELAXED);return;}
 uint32_t unlocked=0;
 if(!__atomic_compare_exchange_n(&p->writer,&unlocked,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
  __atomic_fetch_add(idle?&p->idle:&p->dropped,1,__ATOMIC_RELAXED);return;
 }
 if(jog&&m){
  uint8_t *moving=&m->moving[key->side][path],*speed=&m->speed_class[key->side][path];
  uint8_t *tail=&m->tail[key->side][path];uint32_t *last=&m->value_bits[key->side][path],bits;
  uint8_t speed_class=xz_deck_input_speed_class(key->value);memcpy(&bits,&key->value,4);
  if(idle&&!*moving&&*speed==speed_class&&(*last==bits||*tail>=XZ_DECK_INPUT_STILL_TAIL)){
   __atomic_fetch_add(&p->idle,1,__ATOMIC_RELAXED);
   __atomic_store_n(&p->writer,0,__ATOMIC_RELEASE);return;
  }
  *tail=idle?(uint8_t)(*moving?1u:*tail+1u):0u;
  *moving=!idle;*speed=speed_class;*last=bits;
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
static inline void xz_deck_input_trace_write(struct xz_deck_input_trace *p,
 const struct xz_ds_key *key,unsigned path,unsigned verdict,uint32_t controls,uint32_t at_ms){
 xz_deck_input_trace_write_motion(p,NULL,key,path,verdict,controls,at_ms);
}

/* Every distinct (side, code) either deck hook sees, any operation, from either deck: finds a
   control's code without filtering it through the routing table. One entry per (side, code);
   the first XZ_DECK_INPUT_CODES distinct pairs are kept, later ones only counted in overflow.
   Same opt-in and odd/even sequence rule as the trace. */
#define XZ_DECK_INPUT_CODES 128u
struct xz_deck_input_code {
 uint32_t sequence,key;      /* key: code | side << 16 | 1 << 17 (set) */
 uint32_t count,ops;         /* ops: bit per operation 0..15 seen */
 uint32_t paths;             /* bit per trace path seen */
 uint32_t first_ms,last_ms,last_op,last_delta_bits,last_value_bits,last_verdict,last_controls;
};
struct xz_deck_input_codes {
 uint32_t version,bytes,enabled,capacity,used,overflow,dropped,writer;
 struct xz_deck_input_code entry[XZ_DECK_INPUT_CODES];
};
_Static_assert(sizeof(struct xz_deck_input_code)==48,"Input code record ABI");
_Static_assert(sizeof(struct xz_deck_input_codes)==6176,"Input code table ABI");
static inline void xz_deck_input_codes_init(struct xz_deck_input_codes *t,int enabled){
 memset(t,0,sizeof *t);t->version=1;t->bytes=sizeof *t;t->capacity=XZ_DECK_INPUT_CODES;t->enabled=!!enabled;
}
static inline void xz_deck_input_codes_write(struct xz_deck_input_codes *t,const struct xz_ds_key *key,
 unsigned path,unsigned verdict,uint32_t controls,uint32_t at_ms){
 if(!t||!__atomic_load_n(&t->enabled,__ATOMIC_RELAXED)||!key||key->side<0||key->side>1||path>31)return;
 uint32_t unlocked=0;
 if(!__atomic_compare_exchange_n(&t->writer,&unlocked,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
  __atomic_fetch_add(&t->dropped,1,__ATOMIC_RELAXED);return;
 }
 uint32_t want=(uint32_t)key->code|(uint32_t)key->side<<16|1u<<17,used=t->used;
 struct xz_deck_input_code *e=NULL;
 for(uint32_t i=0;i<used;i++)if(t->entry[i].key==want){e=&t->entry[i];break;}
 if(!e&&used<XZ_DECK_INPUT_CODES)e=&t->entry[used];
 if(!e){__atomic_fetch_add(&t->overflow,1,__ATOMIC_RELAXED);__atomic_store_n(&t->writer,0,__ATOMIC_RELEASE);return;}
 uint32_t seq=__atomic_load_n(&e->sequence,__ATOMIC_RELAXED);
 __atomic_store_n(&e->sequence,seq+1u,__ATOMIC_SEQ_CST);
 if(e->key!=want){e->key=want;e->first_ms=at_ms;e->count=0;e->ops=0;e->paths=0;}
 if(e->count!=UINT32_MAX)e->count++;
 if(key->op<16)e->ops|=1u<<key->op;
 e->paths|=1u<<path;e->last_ms=at_ms;e->last_op=key->op;
 memcpy(&e->last_delta_bits,&key->delta,4);memcpy(&e->last_value_bits,&key->value,4);
 e->last_verdict=verdict;e->last_controls=controls;
 __atomic_store_n(&e->sequence,seq+2u,__ATOMIC_RELEASE);
 if(e==&t->entry[used])__atomic_store_n(&t->used,used+1u,__ATOMIC_RELEASE);
 __atomic_store_n(&t->writer,0,__ATOMIC_RELEASE);
}
#endif
