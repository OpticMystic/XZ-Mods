/* SPDX-License-Identifier: MIT */
#include "stock_dialog.h"
#include "runtime.h"
#include "ui/native_skin_runtime.h"
#include <stdio.h>
#include <string.h>
#pragma weak xz_native_skin_request_repaint
#pragma weak xz_log

static int stable(xz_stock_dialog_read read,uint32_t at,void *out,size_t n){
 unsigned char again[16];
 if(n>sizeof again||read(at,out,n)||read(at,again,n))return 1;
 return memcmp(out,again,n)?2:0;
}
static int guard(xz_stock_dialog_read read,uint32_t at,const unsigned char expected[16]){
 unsigned char actual[16];if(read(at,actual,sizeof actual))return 1;return memcmp(actual,expected,sizeof actual)?2:0;
}
int xz_stock_dialog_snapshot(xz_stock_dialog_read read,struct xz_stock_dialog_state *s){
 static const unsigned char browse[16]={0xf0,0x4f,0x2d,0xe9,0x04,0x8b,0x2d,0xed,0xfc,0xd0,0x4d,0xe2,0x59,0x6b,0xfe,0xeb};
 static const unsigned char status[16]={0xf0,0x4f,0x2d,0xe9,0xa4,0xd0,0x4d,0xe2,0x55,0x67,0xfe,0xeb,0x00,0x00,0x50,0xe3};
 static const unsigned char counter[16]=XZ_STOCK_COUNTER_GUARD_BYTES;
 uint32_t windows[2];int a,b;
 memset(s,0,sizeof *s);
 if(!read){s->failure=XZ_STOCK_NO_READER;return -1;}
 a=guard(read,0x146fcc,browse);if(a){s->failure=a==1?XZ_STOCK_BROWSE_CODE_READ:XZ_STOCK_BROWSE_GUARD;return -1;}
 a=guard(read,0x147f08,status);if(a){s->failure=a==1?XZ_STOCK_STATUS_CODE_READ:XZ_STOCK_STATUS_GUARD;return -1;}
 a=stable(read,0x1adae30,windows,4);b=stable(read,0x1adaea4,windows+1,4);
 if(a||b){s->failure=a==1||b==1?XZ_STOCK_WINDOW_READ:XZ_STOCK_WINDOW_TORN;return -1;}
 /* A live window wins even if the counter guard/read would fail. */
 if(windows[0]||windows[1]){s->window=1;return 1;}
 a=guard(read,0xe1d80,counter);if(a){s->failure=a==1?XZ_STOCK_COUNTER_CODE_READ:XZ_STOCK_COUNTER_GUARD;return -1;}
 a=stable(read,0x222c668,s->counter,sizeof s->counter);
 if(a){s->counter[0]=s->counter[1]=0;s->failure=a==1?XZ_STOCK_COUNTER_READ:XZ_STOCK_COUNTER_TORN;return -1;}
 return 0;
}
int xz_stock_dialog_observe(xz_stock_dialog_read read){struct xz_stock_dialog_state s;return xz_stock_dialog_snapshot(read,&s);}
static int ordinary(uint32_t id){
 switch(id){
 case 0:case 99:case 106:case 108:case 109:case 110:case 111:
 case 123:case 124:case 125:case 131:case 132:case 133:
 case 136:case 137:case 138:case 139:return 1;
 default:return 0;
 }
}
static void unknown(uint32_t id){
 static uint32_t seen[2048];
 /* Native caution IDs are 16-bit. A separate bounded set covers malformed or
    future wider values without using those values as memory indices. */
 static uint32_t wide[64],count;static unsigned lock;
 int first=0;
 if(id<65536){uint32_t bit=1u<<(id&31);first=!(__atomic_fetch_or(seen+id/32,bit,__ATOMIC_RELAXED)&bit);}
 else if(!__atomic_exchange_n(&lock,1u,__ATOMIC_ACQUIRE)){
  unsigned i=0;for(;i<count;i++)if(wide[i]==id)break;
  if(i==count&&count<64){wide[count++]=id;first=1;}
  __atomic_store_n(&lock,0u,__ATOMIC_RELEASE);
 }
 void (*log)(const char*)=xz_log;
 if(first&&log){char line[100];snprintf(line,sizeof line,"STOCK_DIALOG: ignored unmapped counter id=%u",id);log(line);}
}
static struct xz_stock_dialog_state poll_state(void){
 static unsigned previous_reason,previous_priority;
 static const char *const reasons[]={"ok","no reader","browse prologue mismatch","status prologue mismatch",
  "window read failed","window read torn","counter prologue mismatch","counter read failed","counter read torn",
  "browse prologue read failed","status prologue read failed","counter prologue read failed"};
 struct xz_stock_dialog_state s;xz_stock_dialog_snapshot(xz_read_memory,&s);
 unsigned before=__atomic_exchange_n(&previous_reason,(unsigned)s.failure,__ATOMIC_ACQ_REL);
 void (*log)(const char*)=xz_log;
 if(before!=(unsigned)s.failure&&log){char line[140];snprintf(line,sizeof line,"STOCK_DIALOG: observation %s; failure policy=draw mods",reasons[s.failure]);log(line);}
 unsigned mask=0;
 for(unsigned d=0;d<2;d++){
  if(xz_stock_counter_alert(s.counter[d],NULL))mask|=1u<<d;
  else if(!ordinary(s.counter[d]))unknown(s.counter[d]);
 }
 unsigned priority=s.window|(mask<<1);
 void (*repaint)(void)=xz_native_skin_request_repaint;
 if(__atomic_exchange_n(&previous_priority,priority,__ATOMIC_ACQ_REL)!=priority&&repaint)repaint();
 return s;
}
int xz_stock_dialog_active(void){return poll_state().window!=0;}
unsigned xz_stock_counter_ids(uint32_t out[2]){
 struct xz_stock_dialog_state s=poll_state();unsigned mask=0;
 for(unsigned d=0;d<2;d++){if(out)out[d]=s.counter[d];if(xz_stock_counter_alert(s.counter[d],NULL))mask|=1u<<d;}
 return mask;
}
