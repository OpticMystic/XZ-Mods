/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Input trace tests require assertions
#endif
#include "../deck_input_trace.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void){
 struct xz_deck_input_trace p;
 struct xz_ds_key key={.side=1,.code=0x4305,.op=4,.delta=-7,.value=-1.25f};
 xz_deck_input_trace_init(&p,0);
 xz_deck_input_trace_write(&p,&key,XZ_DECK_TRACE_GLOBAL,XZ_DS_DELIVER,15,10);
 assert(!p.next&&!p.record[0].sequence);
 xz_deck_input_trace_init(&p,1);
 xz_deck_input_trace_write(&p,&key,XZ_DECK_TRACE_GLOBAL,XZ_DS_DELIVER,15,0);
 struct xz_deck_input_trace_record *r=&p.record[0];
 int32_t delta;float value;
 memcpy(&delta,&r->delta_bits,4);memcpy(&value,&r->value_bits,4);
 assert(r->sequence==2&&r->ordinal==0&&r->at_ms==0&&r->path==2);
 assert(r->channel==2&&r->logical_deck==4&&delta==-7&&value==-1.25f);
 assert(r->operation==4&&r->controls==15&&r->verdict==XZ_DS_DELIVER);
 key.delta=0;xz_deck_input_trace_write(&p,&key,2,XZ_DS_PASS,12,1);
 assert(p.next==1&&p.idle==1&&r->sequence==2);
 key.code=0x4104;key.op=0;key.shift=1;
 xz_deck_input_trace_write(&p,&key,1,XZ_DS_PASS,12,2);
 assert(p.next==2&&p.record[1].logical_deck==2&&p.record[1].shift==1);
 p.writer=1;xz_deck_input_trace_write(&p,&key,1,XZ_DS_PASS,12,2);
 assert(p.next==2&&p.dropped==1);p.writer=0;
 key.side=-1;xz_deck_input_trace_write(&p,&key,1,XZ_DS_PASS,12,2);
 key.side=2;xz_deck_input_trace_write(&p,&key,1,XZ_DS_PASS,12,2);
 key.side=0;key.code=0x9999;xz_deck_input_trace_write(&p,&key,1,XZ_DS_PASS,12,2);
 key.code=0x4103;xz_deck_input_trace_write(&p,&key,4,XZ_DS_PASS,12,2);
 assert(p.next==2);
 for(unsigned i=0;i<XZ_DECK_INPUT_TRACE_RECORDS+5u;i++)
  xz_deck_input_trace_write(&p,&key,2,XZ_DS_PASS,12,i);
 assert(p.next==XZ_DECK_INPUT_TRACE_RECORDS+7u);
 r=&p.record[(p.next-1u)%XZ_DECK_INPUT_TRACE_RECORDS];
 assert(r->ordinal==p.next-1u&&!(r->sequence&1)&&r->sequence);
 p.next=UINT32_MAX;
 xz_deck_input_trace_write(&p,&key,2,XZ_DS_PASS,12,UINT32_MAX);
 assert(p.next==0&&p.record[UINT32_MAX%XZ_DECK_INPUT_TRACE_RECORDS].ordinal==UINT32_MAX);
 xz_deck_input_trace_write(&p,&key,2,XZ_DS_PASS,12,0);
 assert(p.next==1&&p.record[0].ordinal==0);
 puts("PASS input trace: opt-in, bounded records, idle filtering, channels, contention, wrap");
}
