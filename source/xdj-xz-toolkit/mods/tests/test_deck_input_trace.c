/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Input trace tests require assertions
#endif
#include "../deck_input_trace.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
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
 /* Motion history (r12 stop qualification): first stillness, speed-class changes including the first
    zero and non-finite values, and a bounded tail of changing still speeds are retained; repeated
    identical idle updates are counted only. */
 struct xz_deck_input_motion m;memset(&m,0,sizeof m);
 xz_deck_input_trace_init(&p,1);
 struct xz_ds_key jog={.side=0,.code=0x4305,.op=4,.delta=5,.value=1.0f};
 xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,1);
 jog.delta=0;jog.value=0.4f;
 xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,2);   /* first stillness: kept */
 xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,3);   /* identical: idle */
 assert(p.next==2&&p.idle==1);
 jog.value=0.3f;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,4); /* decaying: kept */
 jog.value=0.3f;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,5);
 assert(p.next==3&&p.idle==2);
 jog.value=0.0f;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,6); /* first zero: kept */
 xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,7);
 assert(p.next==4&&p.idle==3);
 float zero;memcpy(&zero,&p.record[3].value_bits,4);assert(zero==0.0f&&p.record[3].at_ms==6);
 /* The tail is bounded: a still platter reporting new bits forever stops being recorded. */
 for(unsigned i=0;i<100u;i++){jog.value=0.001f*(float)(i+1);xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,8+i);}
 assert(p.next==4+1+(XZ_DECK_INPUT_STILL_TAIL-4u));       /* class change, then the rest of the 8-record tail */
 uint32_t kept=p.next;
 jog.value=-0.2f;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,200); /* reverse class */
 jog.value=NAN;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,201); /* invalid */
 jog.value=INFINITY;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,202); /* same class */
 assert(p.next==kept+2);
 /* Motion resets the tail. */
 jog.delta=3;jog.value=0.5f;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,203);
 jog.delta=0;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,204);
 assert(p.next==kept+4);
 /* Paths and sides keep separate history; the plain writer still counts every idle update. */
 uint32_t before=p.next;
 jog.side=1;jog.value=-0.2f;xz_deck_input_trace_write_motion(&p,&m,&jog,2,XZ_DS_DELIVER,1,300);
 assert(p.next==before+1);
 xz_deck_input_trace_write_motion(&p,&m,&jog,1,XZ_DS_DELIVER,1,301);
 assert(p.next==before+2);
 uint32_t idle=p.idle;
 xz_deck_input_trace_write(&p,&jog,1,XZ_DS_DELIVER,1,302);
 assert(p.next==before+2&&p.idle==idle+1);
 /* A contended idle update is idle, not a dropped gesture. */
 p.writer=1;xz_deck_input_trace_write_motion(&p,&m,&jog,1,XZ_DS_DELIVER,1,303);
 assert(p.idle==idle+2&&p.dropped==0);p.writer=0;
 for(unsigned i=0;i<10000u;i++)xz_deck_input_trace_write_motion(&p,&m,&jog,1,XZ_DS_DELIVER,1,400+i);
 assert(p.next==before+2);

 /* Code table: every (side, code), any operation and path, including codes the trace ignores. */
 static struct xz_deck_input_codes t;
 xz_deck_input_codes_init(&t,0);
 struct xz_ds_key knob={.side=1,.code=0x410b,.op=5,.value=0.25f};
 xz_deck_input_codes_write(&t,&knob,3,0,6,1);
 assert(!t.used);
 xz_deck_input_codes_init(&t,1);
 assert(t.bytes==sizeof t&&t.capacity==XZ_DECK_INPUT_CODES);
 xz_deck_input_codes_write(&t,&knob,3,0,6,10);
 knob.value=-0.5f;xz_deck_input_codes_write(&t,&knob,1,0,6,20);
 assert(t.used==1&&t.entry[0].count==2&&t.entry[0].key==(0x410bu|1u<<16|1u<<17));
 assert(t.entry[0].ops==1u<<5&&t.entry[0].paths==((1u<<3)|(1u<<1))&&t.entry[0].first_ms==10&&t.entry[0].last_ms==20);
 float last;memcpy(&last,&t.entry[0].last_value_bits,4);assert(last==-0.5f&&!(t.entry[0].sequence&1));
 knob.side=0;xz_deck_input_codes_write(&t,&knob,3,0,6,30);
 assert(t.used==2);
 for(unsigned c=0;c<200u;c++){knob.code=(uint16_t)(0x5000+c);xz_deck_input_codes_write(&t,&knob,3,0,6,40);}
 assert(t.used==XZ_DECK_INPUT_CODES&&t.overflow==200u-(XZ_DECK_INPUT_CODES-2u));
 knob.code=0x410b;knob.op=16;xz_deck_input_codes_write(&t,&knob,3,0,6,50);
 knob.op=0;knob.side=2;xz_deck_input_codes_write(&t,&knob,3,0,6,50);
 t.writer=1;knob.side=0;xz_deck_input_codes_write(&t,&knob,3,0,6,50);
 assert(t.dropped==1&&t.entry[1].count==1);
 puts("PASS input trace: opt-in, bounded records, idle filtering, channels, contention, wrap, "
      "motion transitions, code table");
}
