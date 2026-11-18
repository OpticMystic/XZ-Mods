/* SPDX-License-Identifier: MIT */
#include "diary.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

static const char *const components[]={"runtime","usb","settings","deck","sync","ram","ui"};
static const char *const states[]={"unknown","started","present","removed","changed","empty","loading","ready","playing","paused","error","off","following","locked","shown"};
static const char *const reasons[]={"none","observed","track_load","ram_copy","read_busy","space_low","read_only","io_error","queue_full","source_changed","no_grid","manual","clock_lost","range_limit","no_master","phase_unavailable","discontinuity","loop","aligning","external_master","no_bpm","tempo_only"};
static const char *const notices[]={"none","other","track_missing","track_read","source_changed","route_changed","key_busy","key_pending","jump_limit","cues_session","cues_invalid","scratch_truncated"};
static const char *version(const char *v) {
 if(!v)return "unknown";
 const char *p=v;unsigned n=0;
 for(unsigned part=0;part<3;part++){
  unsigned digits=0;while(*p>='0'&&*p<='9'&&n<64){p++;n++;digits++;}
  if(!digits)return "unknown";
  if(part<2){if(*p++!='.')return "unknown";n++;}
 }
 for(unsigned part=0;part<2;part++){
  if(*p==(part?'+':'-')){
   p++;n++;unsigned count=0;
   while(n<64&&((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='.')){p++;n++;count++;}
   if(!count)return "unknown";
  }
 }
 return *p||n>64?"unknown":v;
}
static int done(int n,size_t cap){return n<0||(size_t)n>=cap?-1:n;}
static void hex(char *out,const unsigned char *v,unsigned n){
 static const char digits[]="0123456789abcdef";
 for(unsigned i=0;i<n;i++){out[i*2]=digits[v[i]>>4];out[i*2+1]=digits[v[i]&15];}out[n*2]=0;
}
int xz_diary_header(char *out,size_t cap,const struct xz_diary_session *s,unsigned generation){
 char id[33],hash[67],epoch[16];hex(id,s->id,16);hash[0]='"';hex(hash+1,s->settings_hash,32);hash[65]='"';hash[66]=0;
 if(s->epoch>=1767225600u&&s->epoch<4102444800u)snprintf(epoch,sizeof epoch,"%u",s->epoch);else strcpy(epoch,"null");
 const char *slots=(s->slots&3)==3?"1,2":(s->slots&1)?"1":(s->slots&2)?"2":"";
 if(generation>INT_MAX)return -1;
 return done(snprintf(out,cap,"{\"type\":\"session\",\"schema\":\"xz-mods-diary/2\",\"session_id\":\"%s\",\"epoch\":%s,\"monotonic_ms\":0,\"version\":\"%s\",\"firmware\":\"%s\",\"settings_sha256\":%s,\"slots\":[%s],\"interface\":\"%s\",\"deck_view\":%d,\"segment_generation\":%u}\n",
  id,epoch,version(s->version),s->firmware_verified?"1.26":"unknown",s->have_settings?hash:"null",slots,s->modern?"MODERN":"CLASSIC",s->deck_view==4?4:2,generation),cap);
}
int xz_diary_event_line(char *out,size_t cap,unsigned sequence,uint32_t ms,const struct xz_diary_event *e){
 if(sequence>INT_MAX||ms>XZ_DIARY_SESSION_MS||e->component>=XZ_DIARY_COMPONENTS||e->state>=XZ_DIARY_STATES||e->reason>=XZ_DIARY_REASONS||e->notice>=XZ_DIARY_NOTICES||e->deck>4||e->slot>2||e->value>1000000)return -1;
 if(e->value&&e->value!=7206&&e->value!=8302&&e->value!=8304&&e->value!=8305&&e->value!=8306&&e->value!=8307)return -1;
 return done(snprintf(out,cap,"{\"type\":\"event\",\"sequence\":%u,\"monotonic_ms\":%u,\"component\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\",\"notice\":\"%s\",\"deck\":%u,\"slot\":%u,\"detail_code\":%u}\n",sequence,ms,components[e->component],states[e->state],reasons[e->reason],notices[e->notice],e->deck,e->slot,e->value),cap);
}
static const char *number(char out[16],int32_t v,int32_t lo,int32_t hi){
 if(v<lo||v>hi)return "null";snprintf(out,16,"%d",v);return out;
}
static void array(char out[80],const int32_t v[4],int32_t lo,int32_t hi){
 char a[4][16];snprintf(out,80,"[%s,%s,%s,%s]",number(a[0],v[0],lo,hi),number(a[1],v[1],lo,hi),number(a[2],v[2],lo,hi),number(a[3],v[3],lo,hi));
}
void xz_diary_metrics_unknown(struct xz_diary_metrics *m){
 memset(m,0,sizeof *m);m->cpu_percent=m->free_ram_kib=m->usb_read_latency_us=-1;
 for(unsigned i=0;i<4;i++){m->phase_ms[i]=INT32_MIN;m->key_max_us[i]=m->underrun_frames[i]=-1;}
}
int xz_diary_metrics_line(char *out,size_t cap,unsigned sequence,uint32_t ms,const struct xz_diary_metrics *m){
 if(sequence>INT_MAX||ms>XZ_DIARY_SESSION_MS)return -1;
 char cpu[16],ram[16],usb[16],phase[80],key[80],underruns[80];
 array(phase,m->phase_ms,-10000,10000);array(key,m->key_max_us,0,1000000);array(underruns,m->underrun_frames,0,INT_MAX);
 return done(snprintf(out,cap,"{\"type\":\"metrics\",\"sequence\":%u,\"monotonic_ms\":%u,\"cpu_percent\":%s,\"free_ram_kib\":%s,\"audio_underrun_frames\":%s,\"sync_phase_error_ms\":%s,\"key_shift_max_us\":%s,\"usb_read_latency_us\":%s,\"dropped_records\":%u}\n",sequence,ms,number(cpu,m->cpu_percent,0,100),number(ram,m->free_ram_kib,0,1048576),underruns,phase,key,number(usb,m->usb_read_latency_us,0,1000000),m->dropped>1000000?1000000:m->dropped),cap);
}
unsigned xz_diary_notice_code(const char *text){
 static const struct {const char *text;unsigned code;} allowed[]={
  {"TRACK NOT FOUND",XZ_DIARY_NOTICE_TRACK_MISSING},{"TRACK CANNOT BE READ",XZ_DIARY_NOTICE_TRACK_READ},
  {"NEXT TRACK CANNOT BE READ",XZ_DIARY_NOTICE_TRACK_READ},{"SOURCE CHANGED / LOAD REFUSED",XZ_DIARY_NOTICE_SOURCE_CHANGED},
  {"USB SOURCE CHANGED: SELECT TRACK AGAIN",XZ_DIARY_NOTICE_SOURCE_CHANGED},{"ROUTE CHANGED / LOAD REFUSED",XZ_DIARY_NOTICE_ROUTE_CHANGED},
  {"KEY QUEUE BUSY / RETRY",XZ_DIARY_NOTICE_KEY_BUSY},{"KEY REQUEST QUEUED / PLAY TO VERIFY",XZ_DIARY_NOTICE_KEY_PENDING},
  {"JUMP UNAVAILABLE: TRACK EDGE, BUFFER OR DECK STATE",XZ_DIARY_NOTICE_JUMP_LIMIT},
  {"CUES KEPT FOR THIS SESSION",XZ_DIARY_NOTICE_CUES_SESSION},{"SAVED CUES INVALID / USING EXPORT",XZ_DIARY_NOTICE_CUES_INVALID},
  {"SCRATCH RELEASE TRUNCATED",XZ_DIARY_NOTICE_SCRATCH_TRUNCATED}};
 if(!text||!text[0])return XZ_DIARY_NOTICE_NONE;
 for(unsigned i=0;i<sizeof allowed/sizeof allowed[0];i++)if(!strcmp(text,allowed[i].text))return allowed[i].code;
 return XZ_DIARY_NOTICE_OTHER;
}
unsigned xz_diary_error_code(const char *text){
 static const unsigned codes[]={7206,8302,8304,8305,8306,8307};char expected[16];
 if(!text)return 0;
 for(unsigned i=0;i<sizeof codes/sizeof codes[0];i++){snprintf(expected,sizeof expected,"E-%u",codes[i]);if(!strcmp(text,expected))return codes[i];}
 return 0;
}
