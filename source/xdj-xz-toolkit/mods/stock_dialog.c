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
/* Firmware-1.26 English STR_CHAR_DEV_UNSUPPORTED texts the kernel's hub refusal raises (the loopback cable's hub at
   the second level). A window whose text starts with one of them is the hub warning, whatever follows. USB ACCESS
   ERROR and DEVICE NO RESPONSE, other languages and any unreadable text are ordinary windows. */
static const char *const usb_hub_text[2]={"E-8307: HUBS ARE NOT SUPPORTED","E-8307: UNSUPPORTED DEVICE"};
static unsigned usb_hub_window(xz_stock_dialog_read read,uint32_t at){
 uint16_t a[32],b[32];
 if(read(at,a,sizeof a)||read(at,b,sizeof b)||memcmp(a,b,sizeof a))return 0;
 for(unsigned k=0;k<2;k++){
  const char *t=usb_hub_text[k];size_t i=0;
  while(t[i]&&a[i]==(unsigned char)t[i])i++;
  if(!t[i])return k+1;
 }
 return 0;
}
unsigned xz_stock_usb_hub_at(xz_stock_dialog_read read,uint32_t text){return read?usb_hub_window(read,text):0;}
int xz_stock_dialog_snapshot(xz_stock_dialog_read read,struct xz_stock_dialog_state *s){
 static const unsigned char browse[16]={0xf0,0x4f,0x2d,0xe9,0x04,0x8b,0x2d,0xed,0xfc,0xd0,0x4d,0xe2,0x59,0x6b,0xfe,0xeb};
 static const unsigned char status[16]={0xf0,0x4f,0x2d,0xe9,0xa4,0xd0,0x4d,0xe2,0x55,0x67,0xfe,0xeb,0x00,0x00,0x50,0xe3};
 static const unsigned char counter[16]=XZ_STOCK_COUNTER_GUARD_BYTES;
 uint32_t window;int a;
 memset(s,0,sizeof *s);
 if(!read){s->failure=XZ_STOCK_NO_READER;return -1;}
 const unsigned char *const expected[2]={browse,status};
 const uint32_t code[2]={0x146fcc,0x147f08},cell[2]={0x1adae30,0x1adaea4},text[2]={XZ_STOCK_BROWSE_TEXT,XZ_STOCK_STATUS_TEXT};
 const enum xz_stock_dialog_failure code_read[2]={XZ_STOCK_BROWSE_CODE_READ,XZ_STOCK_STATUS_CODE_READ};
 const enum xz_stock_dialog_failure mismatch[2]={XZ_STOCK_BROWSE_GUARD,XZ_STOCK_STATUS_GUARD};
 for(unsigned i=0;i<2;i++){
  enum xz_stock_dialog_failure failure=XZ_STOCK_OK;
  a=guard(read,code[i],expected[i]);
  if(a)failure=a==1?code_read[i]:mismatch[i];
  else{
   a=stable(read,cell[i],&window,4);
   if(a)failure=a==1?XZ_STOCK_WINDOW_READ:XZ_STOCK_WINDOW_TORN;
   else if(window){
    unsigned hub=usb_hub_window(read,text[i]);
    if(!hub){s->window=1;s->usb_hub=0;s->text_at=text[i];s->failure=XZ_STOCK_OK;return 1;}
    s->usb_hub=hub;
   }
  }
  if(!s->failure)s->failure=failure;
 }
 /* Either proven live window wins over a failure in the other family. */
 if(s->failure)return -1;
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
static unsigned shown_hub;
/* The live window's text, copied under a spin lock: one 64-byte copy each way. A poll that finds the lock taken
   leaves the copy for the next poll. */
static unsigned window_lock;static char window_text[XZ_STOCK_WINDOW_TEXT];
static size_t ascii_text(const uint16_t *u,size_t n,char out[XZ_STOCK_WINDOW_TEXT]){
 size_t k=0;
 for(size_t i=0;i<n&&u[i]&&k<XZ_STOCK_WINDOW_TEXT-1;i++){
  char ch=u[i]>=0x20&&u[i]<0x7f?(char)u[i]:u[i]<0x20?' ':'?';
  if(ch==' '&&(!k||out[k-1]==' '))continue;
  out[k++]=ch;
 }
 while(k&&out[k-1]==' ')k--;
 out[k]=0;return k;
}
static void publish_window(const struct xz_stock_dialog_state *s){
 char t[XZ_STOCK_WINDOW_TEXT]="";
 if(s->window){
  uint16_t a[32],b[32];
  if(!s->text_at||xz_read_memory(s->text_at,a,sizeof a)||xz_read_memory(s->text_at,b,sizeof b)||memcmp(a,b,sizeof a)||!ascii_text(a,32,t))
   snprintf(t,sizeof t,"%s",XZ_STOCK_WINDOW_UNREAD);
 }
 if(__atomic_exchange_n(&window_lock,1u,__ATOMIC_ACQUIRE))return;
 int changed=strcmp(window_text,t)!=0;
 if(changed)memcpy(window_text,t,sizeof t);
 __atomic_store_n(&window_lock,0u,__ATOMIC_RELEASE);
 void (*log)(const char*)=xz_log;
 if(changed&&log){char line[120];snprintf(line,sizeof line,"STOCK_DIALOG: window %s; shown by mods",t[0]?t:"cleared");log(line);}
}
size_t xz_stock_window_text(char out[XZ_STOCK_WINDOW_TEXT]){
 while(__atomic_exchange_n(&window_lock,1u,__ATOMIC_ACQUIRE));
 memcpy(out,window_text,XZ_STOCK_WINDOW_TEXT);
 __atomic_store_n(&window_lock,0u,__ATOMIC_RELEASE);
 out[XZ_STOCK_WINDOW_TEXT-1]=0;return strlen(out);
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
 unsigned hub=s.window?0:s.usb_hub;
 if(__atomic_exchange_n(&shown_hub,hub,__ATOMIC_ACQ_REL)!=hub&&log){
  char line[100];snprintf(line,sizeof line,"STOCK_DIALOG: hub warning %s; not an error",hub?usb_hub_text[hub-1]:"cleared");log(line);
 }
 publish_window(&s);
 unsigned priority=s.window|(mask<<1)|(hub<<3);
 void (*repaint)(void)=xz_native_skin_request_repaint;
 if(__atomic_exchange_n(&previous_priority,priority,__ATOMIC_ACQ_REL)!=priority&&repaint)repaint();
 return s;
}
int xz_stock_dialog_active(void){return poll_state().window!=0;}
const char *xz_stock_usb_hub_text(void){
 unsigned hub=__atomic_load_n(&shown_hub,__ATOMIC_ACQUIRE);
 return hub?usb_hub_text[hub-1]:NULL;
}
unsigned xz_stock_counter_ids(uint32_t out[2]){
 struct xz_stock_dialog_state s=poll_state();unsigned mask=0;
 for(unsigned d=0;d<2;d++){if(out)out[d]=s.counter[d];if(xz_stock_counter_alert(s.counter[d],NULL))mask|=1u<<d;}
 return mask;
}
