/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "xz_kart_runtime.h"
#include "xz_kart_input.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_kart_host host;
static struct xz_kart_wire inert;
static pthread_t worker;
static int descriptor=-1,ready,stopping,dispatch_enabled;
static int heartbeat_cpu=-1,heartbeat_verified,heartbeat_start_gate;
static uint32_t next_epoch;
static uint32_t launcher_pid;
static uint32_t launcher_epoch;
static unsigned long long launcher_start;
static int (*display_probe)(void);
void xz_kart_runtime_display_probe(int (*probe)(void)) {
 pthread_mutex_lock(&mutex);display_probe=probe;pthread_mutex_unlock(&mutex);
}
static uint32_t milliseconds(void) {
 struct timespec t;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;
 return (uint32_t)((uint64_t)t.tv_sec*1000+(uint32_t)t.tv_nsec/1000000);
}
static int heartbeat_attributes(pthread_attr_t *attributes) {
 const char *v=getenv("XZ_KART_DIAGNOSTIC_HEARTBEAT_CPU");
 heartbeat_cpu=-1;
 if(!v)return 0;
 if(v[0]<'0' || v[0]>'3' || v[1]) {
  fputs("Invalid XZ_KART_DIAGNOSTIC_HEARTBEAT_CPU (expected 0..3)\n",stderr);return -1;
 }
 int cpu=v[0]-'0',result=pthread_attr_init(attributes);
 if(result){errno=result;perror("kart heartbeat attributes");return -1;}
 cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(cpu,&mask);
 result=pthread_attr_setaffinity_np(attributes,sizeof(mask),&mask);
 if(result){pthread_attr_destroy(attributes);errno=result;perror("kart heartbeat affinity");return -1;}
 heartbeat_cpu=cpu;return 1;
}
static int process_info(uint32_t pid,unsigned *parent,unsigned *group,char *state,unsigned long long *start) {
 char path[64],line[2048];snprintf(path,sizeof(path),"/proc/%u/stat",pid);
 FILE *f=fopen(path,"r");if(!f)return errno==ENOENT || errno==ESRCH?1:-1;
 errno=0;char *read=fgets(line,sizeof(line),f);int read_error=errno;fclose(f);
 if(!read)return read_error==ENOENT || read_error==ESRCH?1:-1;
 char *p=strrchr(line,')');if(!p)return -1;p++;while(*p==' ')p++;
 if(!*p)return -1;
 *state=*p++;
 for(unsigned field=4;field<=22;field++) {
  char *end;errno=0;unsigned long long value=strtoull(p,&end,10);
  if(errno || end==p)return -1;
  if(field==4)*parent=(unsigned)value;
  if(field==5)*group=(unsigned)value;
  if(field==22)*start=value;
  p=end;
 }
 return 0;
}
const char *xz_kart_runtime_sdk_test_root(void) {
 const char *root=getenv("XZ_KART_SDK_TEST_ROOT"),*prefix="/dev/shm/ool-sdk-source-";
 if(!root || strncmp(root,prefix,strlen(prefix)) || strlen(root)>=96)return NULL;
 const char *tail=root+strlen(prefix);if(!*tail)return NULL;
 for(const char *p=tail;*p;p++)if(!((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='-'))return NULL;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return NULL;
 struct stat st;struct statfs fs;
 int valid=!fstat(fd,&st) && st.st_uid==geteuid() && (st.st_mode&0777)==0700
  && !fstatfs(fd,&fs) && (unsigned long)fs.f_type==0x01021994UL;
 close(fd);return valid?root:NULL;
}
static int launched_child(uint32_t pid,uint32_t parent) {
 if(pid<=1 || pid>INT_MAX || !parent || getpgid((pid_t)pid)!=(pid_t)parent)return 0;
 char path[64],line[1024];snprintf(path,sizeof(path),"/proc/%u/stat",pid);
 FILE *f=fopen(path,"r");if(!f)return 0;
 char *read=fgets(line,sizeof(line),f);fclose(f);if(!read)return 0;
 char *end=strrchr(line,')');
 if(!end)return 0;
 char *cursor=end+1;while(*cursor==' ')cursor++;
 if(!*cursor)return 0;
 cursor++; /* process state, followed by the decimal parent PID */
 errno=0;unsigned long ppid=strtoul(cursor,&end,10);
 return !errno && end!=cursor && ppid==parent;
}
static void *heartbeat(void *unused) {
 (void)unused;
 if(heartbeat_cpu>=0) {
  cpu_set_t actual,wanted;CPU_ZERO(&actual);CPU_ZERO(&wanted);CPU_SET(heartbeat_cpu,&wanted);
  int verified=!pthread_getaffinity_np(pthread_self(),sizeof(actual),&actual) && CPU_EQUAL(&actual,&wanted);
  fprintf(stderr,"OOL_KART_HEARTBEAT_AFFINITY tid=%ld cpu=%d verified=%d\n",(long)syscall(SYS_gettid),heartbeat_cpu,verified);
  __atomic_store_n(&heartbeat_verified,verified?1:-1,__ATOMIC_RELEASE);
  if(!verified)return NULL;
  while(!__atomic_load_n(&heartbeat_start_gate,__ATOMIC_ACQUIRE)) {
   struct timespec pause={0,1000000};nanosleep(&pause,NULL);
  }
  if(__atomic_load_n(&heartbeat_start_gate,__ATOMIC_ACQUIRE)<0)return NULL;
 }
 while(!__atomic_load_n(&stopping,__ATOMIC_ACQUIRE)) {
  pthread_mutex_lock(&mutex);
  uint32_t previous_owner=0,client_age=0,host_age=0;
  if(heartbeat_cpu>=0) {
   previous_owner=__atomic_load_n(&host.wire->owner,__ATOMIC_ACQUIRE);
   uint32_t now=milliseconds();client_age=now-__atomic_load_n(&host.wire->client_ms,__ATOMIC_ACQUIRE);
   host_age=now-__atomic_load_n(&host.wire->host_ms,__ATOMIC_ACQUIRE);
  }
  const char *revocation=NULL;
  xz_kart_host_display(&host,display_probe && display_probe());
  if(previous_owner && !__atomic_load_n(&host.wire->owner,__ATOMIC_ACQUIRE))revocation="display_lost";
  uint32_t request=__atomic_load_n(&host.wire->requested_owner,__ATOMIC_ACQUIRE);
  uint32_t epoch=__atomic_load_n(&host.wire->epoch,__ATOMIC_ACQUIRE);
  if(request && __atomic_load_n(&host.wire->requested_epoch,__ATOMIC_ACQUIRE)==epoch
   && launched_child(request,launcher_pid))
   (void)xz_kart_host_adopt(&host,launcher_pid,request,epoch,milliseconds());
  uint32_t pid=__atomic_load_n(&host.wire->owner,__ATOMIC_ACQUIRE);
  if(pid && kill((pid_t)pid,0)<0 && errno==ESRCH){xz_kart_host_revoke(&host);revocation="process_exited";}
  else {
   xz_kart_host_tick(&host,milliseconds());
   if(heartbeat_cpu>=0 && pid && !__atomic_load_n(&host.wire->owner,__ATOMIC_ACQUIRE))revocation="client_stale";
  }
  pthread_mutex_unlock(&mutex);
  if(heartbeat_cpu>=0 && revocation)fprintf(stderr,"OOL_KART_REVOKED owner=%u reason=%s client_age_ms=%u host_age_ms=%u lease_ms=%u\n",previous_owner,revocation,client_age,host_age,XZ_KART_LEASE_MS);
  struct timespec delay={0,50000000};nanosleep(&delay,NULL);
 }
 return NULL;
}
int xz_kart_runtime_start(void) {
 pthread_mutex_lock(&mutex);
 if(ready){pthread_mutex_unlock(&mutex);return 0;}
 pthread_attr_t attributes;
 int custom=heartbeat_attributes(&attributes);
 if(custom<0){pthread_mutex_unlock(&mutex);return -1;}
 int fd=open(XZ_KART_INPUT_PATH,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 struct stat st;
 if(fd<0 || flock(fd,LOCK_EX|LOCK_NB) || fstat(fd,&st) || !S_ISREG(st.st_mode)
  || st.st_uid!=geteuid() || (st.st_size && st.st_size!=(off_t)sizeof(inert))
  || ftruncate(fd,sizeof(inert))) {
  if(fd>=0)close(fd);
  if(custom)pthread_attr_destroy(&attributes);
  pthread_mutex_unlock(&mutex);return -1;
 }
 void *memory=mmap(NULL,sizeof(inert),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 if(memory==MAP_FAILED){close(fd);if(custom)pthread_attr_destroy(&attributes);pthread_mutex_unlock(&mutex);return -1;}
 descriptor=fd;xz_kart_host_init(&host,memory);next_epoch=milliseconds();
 xz_kart_host_display(&host,0);
 __atomic_store_n(&stopping,0,__ATOMIC_RELEASE);
 __atomic_store_n(&heartbeat_verified,0,__ATOMIC_RELEASE);
 __atomic_store_n(&heartbeat_start_gate,0,__ATOMIC_RELEASE);
 int create_result=pthread_create(&worker,custom?&attributes:NULL,heartbeat,NULL);
 if(custom)pthread_attr_destroy(&attributes);
 if(create_result) {
  errno=create_result;perror("kart heartbeat creation");
  munmap(memory,sizeof(inert));close(fd);descriptor=-1;host.wire=NULL;
  pthread_mutex_unlock(&mutex);return -1;
 }
 if(custom) {
  uint32_t started=milliseconds();unsigned attempts=0;
  while(!__atomic_load_n(&heartbeat_verified,__ATOMIC_ACQUIRE) && attempts++<1000 && (uint32_t)(milliseconds()-started)<1000) {
   struct timespec pause={0,1000000};nanosleep(&pause,NULL);
  }
  if(__atomic_load_n(&heartbeat_verified,__ATOMIC_ACQUIRE)!=1) {
   __atomic_store_n(&heartbeat_start_gate,-1,__ATOMIC_RELEASE);
   pthread_join(worker,NULL);
   munmap(memory,sizeof(inert));close(fd);descriptor=-1;host.wire=NULL;
   fputs("kart heartbeat affinity verification failed\n",stderr);
   pthread_mutex_unlock(&mutex);return -1;
  }
  __atomic_store_n(&heartbeat_start_gate,1,__ATOMIC_RELEASE);
 }
 ready=1;__atomic_store_n(&dispatch_enabled,1,__ATOMIC_RELEASE);pthread_mutex_unlock(&mutex);return 0;
}
void xz_kart_runtime_stop(void) {
 pthread_mutex_lock(&mutex);
 if(!ready){pthread_mutex_unlock(&mutex);return;}
 if(launcher_pid){xz_kart_host_revoke(&host);pthread_mutex_unlock(&mutex);return;}
 ready=0;__atomic_store_n(&stopping,1,__ATOMIC_RELEASE);pthread_mutex_unlock(&mutex);
 pthread_join(worker,NULL);
 pthread_mutex_lock(&mutex);
 uint32_t owned[2]={host.owned[0],host.owned[1]};
 xz_kart_host_revoke(&host);munmap(host.wire,sizeof(inert));close(descriptor);descriptor=-1;
 xz_kart_host_init(&host,&inert);host.owned[0]=owned[0];host.owned[1]=owned[1];
 if(!owned[0] && !owned[1])__atomic_store_n(&dispatch_enabled,0,__ATOMIC_RELEASE);
 pthread_mutex_unlock(&mutex);
}
int xz_kart_runtime_physical(void *self,const void *event) {
 if(!__atomic_load_n(&dispatch_enabled,__ATOMIC_ACQUIRE))return 0;
 pthread_mutex_lock(&mutex);
 if(host.wire)xz_kart_host_display(&host,display_probe && display_probe());
 int result=host.wire?xz_kart_host_player_event(&host,self,0x27,event,24,milliseconds()):0;
 if(!ready && !host.owned[0] && !host.owned[1])__atomic_store_n(&dispatch_enabled,0,__ATOMIC_RELEASE);
 pthread_mutex_unlock(&mutex);return result;
}
int xz_kart_runtime_prepare(const char *id,unsigned kind,struct xz_kart_launch *launch) {
 memset(launch,0,sizeof(*launch));
 if(kind!=1 || strcmp(id,"org.vjtools.out-of-line"))return 0;
 snprintf(launch->display_env,sizeof(launch->display_env),"XZ_KART_SDK_DISPLAY=1");
 const char *mask=getenv("XZ_KART_CONTROL_DECK_MASK");
 if(mask && strcmp(mask,"1") && strcmp(mask,"2") && strcmp(mask,"3"))return -1;
 pthread_mutex_lock(&mutex);
 if(!ready || launcher_pid || __atomic_load_n(&host.wire->owner,__ATOMIC_ACQUIRE)) {pthread_mutex_unlock(&mutex);return -1;}
 if(!++next_epoch)++next_epoch;
 launch->epoch=next_epoch;launch->mask=mask?(unsigned)(mask[0]-'0'):1;
 pthread_mutex_unlock(&mutex);
 snprintf(launch->epoch_env,sizeof(launch->epoch_env),"XZ_KART_INPUT_EPOCH=%u",launch->epoch);
 snprintf(launch->mask_env,sizeof(launch->mask_env),"XZ_KART_INPUT_DECK_MASK=%u",launch->mask);
 return 1;
}
int xz_kart_runtime_attach(uint32_t pid,const struct xz_kart_launch *launch) {
 if(!launch->epoch)return 0;
 unsigned parent,group;char state;unsigned long long start=0;
 if(process_info(pid,&parent,&group,&state,&start)!=0 || !start)return -1;
 pthread_mutex_lock(&mutex);
 int result=ready?xz_kart_host_grant(&host,pid,launch->epoch,launch->mask,milliseconds()):-1;
 if(!result){
  launcher_pid=pid;launcher_epoch=launch->epoch;launcher_start=start;
 }
 pthread_mutex_unlock(&mutex);return result;
}
void xz_kart_runtime_release(uint32_t pid) {
 pthread_mutex_lock(&mutex);
 if(host.wire && launcher_pid==pid)xz_kart_host_revoke(&host);
 pthread_mutex_unlock(&mutex);
}
int xz_kart_runtime_signal_target(int pid) {
 pthread_mutex_lock(&mutex);int result=launcher_pid==(uint32_t)pid?-pid:pid;
 pthread_mutex_unlock(&mutex);return result;
}
void xz_kart_runtime_forget(uint32_t pid) {
 uint32_t epoch=0;
 pthread_mutex_lock(&mutex);
 if(host.wire && launcher_pid==pid){xz_kart_host_revoke(&host);launcher_pid=0;epoch=launcher_epoch;launcher_epoch=0;}
 pthread_mutex_unlock(&mutex);
 if(epoch)xz_kart_runtime_cleanup_profile(pid,epoch);
}
static int group_alive(uint32_t pid,unsigned long long expected_start) {
#ifdef XZ_KART_LIFECYCLE_TEST
 if(getenv("OOL_TEST_GROUP_UNKNOWN"))return -1;
#endif
 unsigned parent,group;char state;unsigned long long start;
 int leader=process_info(pid,&parent,&group,&state,&start);
 if(leader<0)return -1;
 if(!leader && expected_start && start!=expected_start)return 0;
 DIR *dir=opendir("/proc");if(!dir)return -1;
 struct dirent *e;unsigned seen=0;int found=0;
 for(;;) {
  errno=0;e=readdir(dir);
  if(!e){if(errno)found=-1;break;}
  if(++seen>4096){found=-1;break;}
  char *end;unsigned long value=strtoul(e->d_name,&end,10);
  if(!*e->d_name || *end || !value || value>INT_MAX)continue;
  int result=process_info((uint32_t)value,&parent,&group,&state,&start);
  if(result<0){found=-1;break;}
  if(!result && group==pid && state!='Z' && state!='X'){found=1;break;}
 }
 closedir(dir);return found;
}
int xz_kart_runtime_group_pending(void) {
 pthread_mutex_lock(&mutex);int result=launcher_pid!=0;pthread_mutex_unlock(&mutex);return result;
}
int xz_kart_runtime_drain(uint32_t pid) {
 pthread_mutex_lock(&mutex);
 int ours=launcher_pid==pid;unsigned long long start=launcher_start;
 pthread_mutex_unlock(&mutex);
 if(!ours)return 0;
 xz_kart_runtime_release(pid);
 for(unsigned phase=0;phase<2;phase++) {
  int alive=group_alive(pid,start);if(alive==0)return 0;if(alive<0)return -1;
  (void)kill(-(pid_t)pid,phase?SIGKILL:SIGTERM);
  unsigned deadline=milliseconds()+(phase?1000u:1500u);
  do {
   struct timespec delay={0,25000000};nanosleep(&delay,NULL);
   alive=group_alive(pid,start);if(alive==0)return 0;if(alive<0)return -1;
  } while((int32_t)(milliseconds()-deadline)<0);
 }
 return -1;
}
static int clear_directory(int parent,const char *name,unsigned depth,unsigned *budget) {
 if(!depth || !*budget)return -1;
 int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0)return errno==ENOENT?0:-1;
 struct stat st;
 if(fstat(fd,&st) || st.st_uid!=geteuid()){close(fd);return -1;}
 DIR *dir=fdopendir(fd);if(!dir){close(fd);return -1;}
 struct dirent *entry;int result=0;
 while((entry=readdir(dir))) {
  if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
  if(!*budget){result=-1;break;}
  (*budget)--;
  if(fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)){result=-1;break;}
  if(S_ISDIR(st.st_mode))result=clear_directory(fd,entry->d_name,depth-1,budget);
  else result=unlinkat(fd,entry->d_name,0);
  if(result)break;
 }
 closedir(dir);return result?result:unlinkat(parent,name,AT_REMOVEDIR);
}
void xz_kart_runtime_cleanup_profile(uint32_t pid,uint32_t epoch) {
 if(!pid || !epoch)return;
 char name[80];snprintf(name,sizeof(name),"ool-profile-%u-%u",pid,epoch);
 int root=open("/dev/shm",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)return;
 unsigned budget=256;(void)clear_directory(root,name,5,&budget);close(root);
}
