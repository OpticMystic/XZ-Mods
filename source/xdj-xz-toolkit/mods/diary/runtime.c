/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "../writes.h"
#include "runtime.h"
#include "store.h"
#include "../clock_compat.h"
#include "../extra_decks.h"
#include "../audio/vendor/sha256/sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mntent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>
#pragma weak xz_ui_diary_snapshot
#pragma weak xz_shell_diary_snapshot
#pragma weak xz_extra_decks_snapshot
#ifndef XZ_DIARY_VERSION
#define XZ_DIARY_VERSION "unknown"
#endif
#define MOUNTS 2
static pthread_t thread;
static unsigned running,created,busy[2],audio_frames[4],audio_key[4],audio_valid[4],notices[XZ_DIARY_COMPONENTS];
struct mount {
 struct xz_diary_store store;
 unsigned slot,seen,have_disk,new_mount,last_reason;
 uint64_t reads,sectors,ticks;
 uint32_t quiet_at;
 int disk_safe,latency;
};
static struct mount mounts[MOUNTS];
static struct xz_diary_session session;
static struct xz_diary_view previous;
static uint64_t began,cpu_total,cpu_idle;
static unsigned sequence,have_previous;
__attribute__((visibility("default"))) struct {
 uint32_t version,polls,writes,fsyncs,dropped,disabled,worker_cpu_us_max,worker_wall_us_max,mount_overflow;
 uint32_t started,io_idle;
} xz_mods_diary_proof_v1={.version=1};
static uint64_t micros(clockid_t clock){struct timespec t;if(xz_clock_gettime_compat(clock,&t))return 0;return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;}
void xz_diary_busy_begin(unsigned mask){for(unsigned i=0;i<2;i++)if(mask&(1u<<i))__atomic_add_fetch(&busy[i],1,__ATOMIC_RELAXED);}
void xz_diary_busy_end(unsigned mask){for(unsigned i=0;i<2;i++)if(mask&(1u<<i))__atomic_sub_fetch(&busy[i],1,__ATOMIC_RELAXED);}
void xz_diary_audio(unsigned deck,uint64_t frames,unsigned key,int valid){
 if(deck>=4)return;
 __atomic_store_n(&audio_frames[deck],frames>INT_MAX?INT_MAX:(unsigned)frames,__ATOMIC_RELAXED);
 __atomic_store_n(&audio_key[deck],key>1000000?1000000:key,__ATOMIC_RELAXED);
 __atomic_store_n(&audio_valid[deck],valid?3u:1u,__ATOMIC_RELEASE);
}
void xz_diary_notice(unsigned component,unsigned notice){
 if(component<XZ_DIARY_COMPONENTS&&notice>0&&notice<XZ_DIARY_NOTICES)
  __atomic_fetch_or(&notices[component],1u<<notice,__ATOMIC_RELAXED);
}
static int safe(void *opaque,const struct xz_diary_store *s){
 struct mount *m=opaque;
 if(!__atomic_load_n(&running,__ATOMIC_ACQUIRE)||xz_settings_mount_id(s->root)!=s->mount_id)return -1;
 if(__atomic_load_n(&busy[m->slot-1],__ATOMIC_RELAXED)||!m->disk_safe)return 0;
 return 1;
}
static int slot_of(const char *p){
 if(!strncmp(p,"/media/usb1",11)&&(p[11]==0||p[11]=='/'))return 1;
 if(!strncmp(p,"/media/usb2",11)&&(p[11]==0||p[11]=='/'))return 2;
 return 0;
}
static void enqueue(const char *line,int n){if(n>0)for(unsigned i=0;i<MOUNTS;i++)if(mounts[i].seen)xz_diary_store_queue(&mounts[i].store,line,(size_t)n);}
static void event(uint32_t now,unsigned component,unsigned state,unsigned reason,unsigned notice,unsigned deck,unsigned slot,unsigned value){
 char line[XZ_DIARY_LINE_BYTES];struct xz_diary_event e={component,state,reason,notice,deck,slot,value};
 enqueue(line,xz_diary_event_line(line,sizeof line,++sequence,now,&e));
}
struct root_choice {char existing[1024],fallback[1024];dev_t device;ino_t inode;int ambiguous;};
static void root_candidate(struct root_choice *c,const char *path,int diary,dev_t device,ino_t inode){
 if(!c->fallback[0])strcpy(c->fallback,path);
 if(!diary)return;
 if(c->existing[0]){if(c->device!=device||c->inode!=inode)c->ambiguous=1;return;}
 strcpy(c->existing,path);c->device=device;c->inode=inode;
}
static int find_root(unsigned slot,char root[1024]){
 /* Prefer the existing diary over later settings/loader markers. Ambiguous
    pre-existing rings are left untouched rather than adding another ring. */
 FILE *f=setmntent("/proc/mounts","r");if(!f)return -1;
 struct mntent entry,*e;char buffer[4096];struct root_choice choice={0};
 while((e=getmntent_r(f,&entry,buffer,sizeof buffer))){
  if(slot_of(e->mnt_dir)!=(int)slot+1||strlen(e->mnt_dir)>=sizeof choice.existing)continue;
  char path[1100];struct stat st={0};snprintf(path,sizeof path,"%s/VJ.Tools/XZ-Mods-diary",e->mnt_dir);
  int diary=!lstat(path,&st)&&S_ISDIR(st.st_mode);
  root_candidate(&choice,e->mnt_dir,diary,st.st_dev,st.st_ino);
  if(choice.ambiguous)break;
 }
 endmntent(f);
 if(choice.ambiguous)return -1;
 if(choice.existing[0]){strcpy(root,choice.existing);return 0;}
 if(!xz_settings_find_usb(slot,root))return 0;
 if(choice.fallback[0]){strcpy(root,choice.fallback);return 0;}
 return 1;
}
static void discover(uint32_t now){
 struct stat internal;int root_ok=!stat("/",&internal);
 for(unsigned i=0;i<MOUNTS;i++){
  struct mount *m=&mounts[i];char root[1024]={0};struct stat st;unsigned long id=0;
  /* One ring per physical slot. Keep its partition while mounted, even if the
     settings preference or a loader marker appears on another partition. */
  if(m->slot&&root_ok&&!lstat(m->store.root,&st)&&S_ISDIR(st.st_mode)&&st.st_dev!=internal.st_dev&&
     (id=xz_settings_mount_id(m->store.root))==m->store.mount_id&&st.st_dev==m->store.device&&st.st_ino==m->store.inode)
   strcpy(root,m->store.root);
  else {
   if(m->slot){unsigned slot=m->slot;memset(m,0,sizeof *m);event(now,XZ_DIARY_USB,XZ_DIARY_REMOVED,XZ_DIARY_SOURCE_CHANGED,0,0,slot,0);}
   if(!root_ok||find_root(i,root)||slot_of(root)!=(int)i+1||lstat(root,&st)||
      !S_ISDIR(st.st_mode)||st.st_dev==internal.st_dev||!(id=xz_settings_mount_id(root))){root[0]=0;}
  }
  if(!root[0]){m->seen=0;continue;}
  if(!m->slot){
   if(xz_diary_store_init(&m->store,root,id))continue;
   m->slot=i+1;m->new_mount=1;m->quiet_at=now;m->latency=-1;m->store.context=m;m->store.guard=safe;
  }
  m->seen=1;
 }
 session.slots=0;for(unsigned i=0;i<MOUNTS;i++)if(mounts[i].seen)session.slots|=1u<<(mounts[i].slot-1);
 for(unsigned i=0;i<MOUNTS;i++)if(mounts[i].new_mount){
  mounts[i].new_mount=0;event(now,XZ_DIARY_USB,XZ_DIARY_PRESENT,XZ_DIARY_OBSERVED,0,0,mounts[i].slot,0);
 }
}
/* Up to max unsigned decimal fields after an optional literal prefix. Not sscanf: this toolchain links sscanf to
   __isoc99_sscanf@GLIBC_2.7, and the XZ has only GLIBC 2.4. */
static int read_u64s(const char *text,const char *prefix,unsigned long long *out,int max){
 size_t n=strlen(prefix);if(strncmp(text,prefix,n))return 0;text+=n;
 int got=0;
 while(got<max){
  while(*text==' '||*text=='\t')text++;
  if(*text<'0'||*text>'9')break;
  char *end;errno=0;unsigned long long v=strtoull(text,&end,10);
  if(end==text||errno)break;
  out[got++]=v;text=end;
 }
 return got;
}
static void disk(struct mount *m,uint32_t now){
 char path[128],text[512];snprintf(path,sizeof path,"/sys/dev/block/%u:%u/stat",major(m->store.device),minor(m->store.device));
 FILE *f=fopen(path,"r");m->disk_safe=0;m->latency=-1;if(!f)return;
 int ok=fgets(text,sizeof text,f)!=NULL;fclose(f);if(!ok)return;
 unsigned long long v[11];
 if(read_u64s(text,"",v,11)!=11)return;
 unsigned long long reads=v[0],sectors=v[2],ticks=v[3],flight=v[8];
 if(!m->have_disk||reads<m->reads||sectors<m->sectors||ticks<m->ticks){m->quiet_at=now;}
 else {
  uint64_t count=reads-m->reads;
  if(count)m->latency=(int)(((ticks-m->ticks)*1000u/count)>1000000?1000000:((ticks-m->ticks)*1000u/count));
  if(flight||sectors-m->sectors>=128u||__atomic_load_n(&busy[m->slot-1],__ATOMIC_RELAXED))m->quiet_at=now;
  m->disk_safe=!flight&&(uint32_t)(now-m->quiet_at)>=3000u;
 }
 m->have_disk=1;m->reads=reads;m->sectors=sectors;m->ticks=ticks;
}
static void view(uint32_t now){
 struct xz_diary_view v={0};
 if(xz_ui_diary_snapshot)xz_ui_diary_snapshot(&v);
 if(xz_shell_diary_snapshot)xz_shell_diary_snapshot(&v);
 if(v.have_settings){
  char text[1024];int n=xz_settings_format(&v.settings,text,sizeof text);
  if(n>0){SHA256_CTX hash;sha256_init(&hash);sha256_update(&hash,(unsigned char *)text,(size_t)n);unsigned char digest[32];sha256_final(&hash,digest);
   if(session.have_settings&&memcmp(digest,session.settings_hash,32))event(now,XZ_DIARY_SETTINGS,XZ_DIARY_CHANGED,XZ_DIARY_OBSERVED,0,0,0,0);
   memcpy(session.settings_hash,digest,32);session.have_settings=1;}
  session.modern=v.settings.ui_interface==XZ_INTERFACE_MODERN;session.deck_view=v.settings.deck_view==XZ_DECK_VIEW_TWO?2:4;
  if(!have_previous||v.notice!=previous.notice)event(now,XZ_DIARY_UI,XZ_DIARY_SHOWN,XZ_DIARY_OBSERVED,v.notice,0,0,0);
  if(v.usb_error&&(!have_previous||v.usb_error!=previous.usb_error))event(now,XZ_DIARY_USB,XZ_DIARY_ERROR,XZ_DIARY_OBSERVED,0,0,0,v.usb_error);
  for(unsigned i=0;i<4;i++){
   if(!have_previous||v.ram_state[i]!=previous.ram_state[i]||v.ram_reason[i]!=previous.ram_reason[i])
    event(now,XZ_DIARY_RAM,v.ram_state[i],v.ram_reason[i],0,i+1,0,0);
   if(v.error[i]&&(!have_previous||v.error[i]!=previous.error[i]))event(now,XZ_DIARY_DECK,XZ_DIARY_ERROR,XZ_DIARY_OBSERVED,0,i+1,0,v.error[i]);
  }
 }
 if(v.have_decks)for(unsigned i=0;i<2;i++){
  if(!have_previous||v.deck_state[i]!=previous.deck_state[i])event(now,XZ_DIARY_DECK,v.deck_state[i],XZ_DIARY_OBSERVED,0,i+1,0,0);
  if(!have_previous||v.sync_state[i]!=previous.sync_state[i])event(now,XZ_DIARY_SYNC,v.sync_state[i],XZ_DIARY_OBSERVED,0,i+1,0,0);
 }
 if(v.have_settings||v.have_decks){previous=v;have_previous=1;}
 for(unsigned c=0;c<XZ_DIARY_COMPONENTS;c++){
  unsigned mask=__atomic_exchange_n(&notices[c],0,__ATOMIC_RELAXED);
  for(unsigned n=1;n<XZ_DIARY_NOTICES;n++)if(mask&(1u<<n))event(now,c,XZ_DIARY_SHOWN,XZ_DIARY_OBSERVED,n,0,0,0);
 }
}
static void metrics(uint32_t now,int emit){
 struct xz_diary_metrics m;xz_diary_metrics_unknown(&m);
 char text[512];FILE *f=fopen("/proc/stat","r");
 if(f){if(fgets(text,sizeof text,f)){
  unsigned long long c[8]={0};if(read_u64s(text,"cpu ",c,8)>=4){
   uint64_t total=0,idle=c[3]+c[4];for(unsigned i=0;i<8;i++)total+=c[i];
   if(cpu_total&&total>cpu_total&&idle>=cpu_idle&&idle-cpu_idle<=total-cpu_total)m.cpu_percent=(int)(100u-100u*(idle-cpu_idle)/(total-cpu_total));
   cpu_total=total;cpu_idle=idle;
  }}fclose(f);}
 struct sysinfo info;if(!sysinfo(&info)){uint64_t free_kib=(uint64_t)info.freeram*info.mem_unit/1024u;m.free_ram_kib=free_kib>1048576?1048576:(int)free_kib;}
 struct xz_extra_decks decks={0};static unsigned states[2],reasons[2],transports[2];
 int extra_ready=xz_extra_decks_snapshot&&xz_extra_decks_snapshot(&decks);
 if(extra_ready)for(unsigned i=0;i<2;i++){
  const struct xz_extra_deck *d=&decks.deck[i];unsigned state=!d->sync.requested?XZ_DIARY_OFF:d->sync.locked?XZ_DIARY_LOCKED:XZ_DIARY_FOLLOWING;
  unsigned reason=XZ_DIARY_NONE;
  switch(d->sync.reason){case XZ_EXTRA_SYNC_NO_GRID:reason=XZ_DIARY_NO_GRID;break;case XZ_EXTRA_SYNC_MANUAL:reason=XZ_DIARY_MANUAL;break;
   case XZ_EXTRA_SYNC_OUT_OF_RANGE:reason=XZ_DIARY_RANGE_LIMIT;break;case XZ_EXTRA_SYNC_WAIT_MASTER:reason=XZ_DIARY_NO_MASTER;break;
   case XZ_EXTRA_SYNC_MASTER_CLOCK:reason=XZ_DIARY_CLOCK_LOST;break;
   case XZ_EXTRA_SYNC_WAIT_ANCHOR:case XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE:reason=XZ_DIARY_PHASE_UNAVAILABLE;break;
   case XZ_EXTRA_SYNC_DISCONTINUITY:reason=XZ_DIARY_DISCONTINUITY;break;
   case XZ_EXTRA_SYNC_LOOP_UNALIGNED:reason=XZ_DIARY_LOOP;break;
   case XZ_EXTRA_SYNC_ACQUIRING:reason=XZ_DIARY_ALIGNING;break;
   case XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED:reason=XZ_DIARY_EXTERNAL_MASTER;break;
   case XZ_EXTRA_SYNC_NO_SOURCE_BPM:reason=XZ_DIARY_NO_BPM;break;
   case XZ_EXTRA_SYNC_TEMPO_FOLLOW:reason=XZ_DIARY_TEMPO_ONLY;break;default:break;}
  if(state!=states[i]||reason!=reasons[i]){event(now,XZ_DIARY_SYNC,state,reason,0,i+3,0,0);states[i]=state;reasons[i]=reason;}
  unsigned transport=!d->loaded?XZ_DIARY_EMPTY:d->playing?XZ_DIARY_PLAYING:XZ_DIARY_PAUSED;
  if(transport!=transports[i]){event(now,XZ_DIARY_DECK,transport,XZ_DIARY_OBSERVED,0,i+3,0,0);transports[i]=transport;}
  if(d->sync.requested&&d->sync.locked&&d->sync.source==XZ_EXTRA_SYNC_SOURCE_LIVE)m.phase_ms[i+2]=d->sync.phase_error_us/1000;
 }
 for(unsigned i=0;i<4;i++){
  if(i>=2&&(!extra_ready||!decks.deck[i-2].loaded))continue;
  unsigned valid=__atomic_load_n(&audio_valid[i],__ATOMIC_ACQUIRE);
  if(valid&1)m.underrun_frames[i]=(int)__atomic_load_n(&audio_frames[i],__ATOMIC_RELAXED);
  if(valid&2)m.key_max_us[i]=(int)__atomic_load_n(&audio_key[i],__ATOMIC_RELAXED);
 }
 for(unsigned i=0;i<MOUNTS;i++)if(mounts[i].seen){
  if(mounts[i].latency>m.usb_read_latency_us)m.usb_read_latency_us=mounts[i].latency;
  m.dropped+=mounts[i].store.dropped;
 }
 if(emit){char line[XZ_DIARY_LINE_BYTES];enqueue(line,xz_diary_metrics_line(line,sizeof line,++sequence,now,&m));}
}
static int new_session(void){
 int fd=open("/dev/urandom",O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
 int ok=read(fd,session.id,sizeof session.id)==sizeof session.id;close(fd);if(!ok)return -1;
 time_t epoch=time(NULL);session.epoch=epoch>=1767225600&&((uint64_t)epoch)<4102444800u?(uint32_t)epoch:0;
 session.version=XZ_DIARY_VERSION;session.firmware_verified=1;began=micros(CLOCK_MONOTONIC);sequence=0;
 for(unsigned i=0;i<MOUNTS;i++){mounts[i].store.active=0;mounts[i].store.used=0;}
 return began?0:-1;
}
static void *worker(void *unused){
 (void)unused;
 if(setpriority(PRIO_PROCESS,0,19))return NULL;
#ifdef SYS_ioprio_set
 /* Linux IOPRIO_CLASS_IDLE, for this calling thread only. */
 xz_mods_diary_proof_v1.io_idle=syscall(SYS_ioprio_set,1,0,3u<<13)==0;
#endif
 if(new_session())return NULL;
 xz_mods_diary_proof_v1.started=1;
 unsigned last_metrics=0;int first=1;
 while(__atomic_load_n(&running,__ATOMIC_ACQUIRE)){
  uint64_t wall=micros(CLOCK_MONOTONIC),cpu=micros(CLOCK_THREAD_CPUTIME_ID);
  uint32_t now=(uint32_t)((wall-began)/1000u);
  if(now>=XZ_DIARY_SESSION_MS-60000u){if(new_session())break;now=0;first=1;last_metrics=0;}
  discover(now);for(unsigned i=0;i<MOUNTS;i++)if(mounts[i].seen)disk(&mounts[i],now);
  view(now);
  if(first)event(now,XZ_DIARY_RUNTIME,XZ_DIARY_STARTED,XZ_DIARY_NONE,0,0,0,0);
  int emit=first||now-last_metrics>=10000u;metrics(now,emit);if(emit)last_metrics=now;first=0;
  unsigned writes=0,syncs=0,dropped=0,disabled=0;
  for(unsigned i=0;i<MOUNTS;i++)if(mounts[i].seen){
   struct xz_diary_store *s=&mounts[i].store;xz_diary_store_flush(s,&session,(uint32_t)(wall/1000u));
   if(s->pause_reason!=mounts[i].last_reason){
    mounts[i].last_reason=s->pause_reason;
    event(now,XZ_DIARY_USB,s->disabled?XZ_DIARY_ERROR:s->pause_reason?XZ_DIARY_PAUSED:XZ_DIARY_READY,s->pause_reason,0,0,mounts[i].slot,0);
   }
   writes+=s->writes;syncs+=s->syncs;dropped+=s->dropped;disabled+=!!s->disabled;
  }
  xz_mods_diary_proof_v1.polls++;xz_mods_diary_proof_v1.writes=writes;xz_mods_diary_proof_v1.fsyncs=syncs;
  xz_mods_diary_proof_v1.dropped=dropped;xz_mods_diary_proof_v1.disabled=disabled;
  uint64_t used=micros(CLOCK_THREAD_CPUTIME_ID)-cpu,elapsed=micros(CLOCK_MONOTONIC)-wall;
  if(used>xz_mods_diary_proof_v1.worker_cpu_us_max)xz_mods_diary_proof_v1.worker_cpu_us_max=used>UINT_MAX?UINT_MAX:(unsigned)used;
  if(elapsed>xz_mods_diary_proof_v1.worker_wall_us_max)xz_mods_diary_proof_v1.worker_wall_us_max=elapsed>UINT_MAX?UINT_MAX:(unsigned)elapsed;
  struct timespec delay={1,0};nanosleep(&delay,NULL);
 }
 return NULL;
}
int xz_diary_start(void){
 if(!xz_mods_may_write())return 0;
 if(created)return 0;memset(mounts,0,sizeof mounts);memset(&session,0,sizeof session);have_previous=0;
 __atomic_store_n(&running,1,__ATOMIC_RELEASE);
 pthread_attr_t attr;
 if(pthread_attr_init(&attr)){__atomic_store_n(&running,0,__ATOMIC_RELEASE);return -1;}
 int error=pthread_attr_setstacksize(&attr,128u*1024u);
 if(!error)error=pthread_create(&thread,&attr,worker,NULL);
 pthread_attr_destroy(&attr);
 if(error){__atomic_store_n(&running,0,__ATOMIC_RELEASE);return -1;}
 created=1;return 0;
}
void xz_diary_stop(void){if(!created)return;__atomic_store_n(&running,0,__ATOMIC_RELEASE);pthread_join(thread,NULL);created=0;}
