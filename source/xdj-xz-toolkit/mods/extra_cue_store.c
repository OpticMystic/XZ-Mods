/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "writes.h"
#include "extra_cue_store.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CUE_BYTES (16u+24u*16u+4u)
#define CUE_JOBS 16u
static uint32_t get(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static uint32_t checksum(const unsigned char *p,size_t n){uint32_t v=2166136261u;while(n--)v=(v^*p++)*16777619u;return v;}
static int point_valid(const struct xz_extra_cue *c,int32_t duration){
 return (c->set==0||c->set==1)&&(!c->set||(c->ms>=0&&c->ms<=duration&&c->rgb<=0xffffffu&&
  (!c->end_ms||(c->end_ms>c->ms&&c->end_ms<=duration))));
}
static int valid(const struct xz_saved_cues *s,int32_t duration){
 if(duration<=0||s->memory_count>XZ_CUE_MEMORY)return 0;
 for(unsigned i=0;i<8;i++)if(!point_valid(&s->hot[i],duration))return 0;
 for(unsigned i=0;i<s->memory_count;i++)if(!s->memory[i].set||!point_valid(&s->memory[i],duration)||
  (i&&s->memory[i].ms<=s->memory[i-1].ms))return 0;
 return 1;
}
static int filename(char *out,size_t size,uint32_t id){return snprintf(out,size,"xzmods-cues-%u.bin",id)>=(int)size?-1:0;}
int xz_cues_read_at(int dir,uint32_t id,int32_t duration,struct xz_saved_cues *out){
 unsigned char bytes[CUE_BYTES];char name[64];struct stat st;
 if(!out||filename(name,sizeof name,id))return -1;
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return errno==ENOENT?0:-1;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==CUE_BYTES;
 size_t done=0;while(ok&&done<sizeof bytes){ssize_t n=read(fd,bytes+done,sizeof bytes-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(size_t)n;}
 close(fd);if(!ok||get(bytes)!=0x31554358u||get(bytes+4)!=id||get(bytes+8)!=(uint32_t)duration||
   get(bytes+400)!=checksum(bytes,400))return -1;
 struct xz_saved_cues s={0};s.memory_count=get(bytes+12);
 for(unsigned i=0;i<24;i++){
  const unsigned char *p=bytes+16+i*16;struct xz_extra_cue *c=i<8?&s.hot[i]:&s.memory[i-8];
  c->set=(int)get(p);c->ms=(int32_t)get(p+4);c->end_ms=(int32_t)get(p+8);c->rgb=get(p+12);
 }
 if(!valid(&s,duration))return -1;
 *out=s;return 1;
}
int xz_cues_write_at(int dir,uint32_t id,int32_t duration,const struct xz_saved_cues *s){
 if(!s||!valid(s,duration))return -1;
 unsigned char bytes[CUE_BYTES]={0};put(bytes,0x31554358u);put(bytes+4,id);put(bytes+8,(uint32_t)duration);put(bytes+12,s->memory_count);
 for(unsigned i=0;i<24;i++){
  const struct xz_extra_cue *c=i<8?&s->hot[i]:&s->memory[i-8];unsigned char *p=bytes+16+i*16;
  if(!c->set||(i>=8&&i-8>=s->memory_count))continue;
  put(p,1);put(p+4,(uint32_t)c->ms);put(p+8,(uint32_t)c->end_ms);put(p+12,c->rgb);
 }
 put(bytes+400,checksum(bytes,400));
 char name[64],temp[96];if(filename(name,sizeof name,id))return -1;
 static unsigned sequence;unsigned seq=__atomic_add_fetch(&sequence,1,__ATOMIC_RELAXED);
 snprintf(temp,sizeof temp,".xzmods-cues-%u-%u-%u.tmp",id,(unsigned)getpid(),seq);
 int fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;
 int ok=1;size_t done=0;
 while(done<sizeof bytes){ssize_t n=write(fd,bytes+done,sizeof bytes-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(size_t)n;}
 if(ok&&fsync(fd))ok=0;if(close(fd))ok=0;
 if(ok&&renameat(dir,temp,dir,name))ok=0;
 if(!ok)unlinkat(dir,temp,0);else (void)fsync(dir);
 return ok?0:-1;
}
int xz_cues_apply(struct xz_saved_cues *s,const struct xz_cue_edit *e){
 if(!s||!e||s->memory_count>XZ_CUE_MEMORY)return 0;
 if(e->kind==XZ_CUE_EDIT_HOT){if(e->pad>=8)return 0;s->hot[e->pad]=e->point;return 1;}
 unsigned at=0;while(at<s->memory_count&&s->memory[at].ms<e->point.ms)at++;
 int present=at<s->memory_count&&s->memory[at].ms==e->point.ms;
 if(e->kind==XZ_CUE_EDIT_DELETE){
  if(!present)return 1;
  memmove(s->memory+at,s->memory+at+1,(s->memory_count-at-1)*sizeof *s->memory);s->memory_count--;
  memset(&s->memory[s->memory_count],0,sizeof *s->memory);return 1;
 }
 if(e->kind!=XZ_CUE_EDIT_MEMORY)return 0;
 if(!present){if(s->memory_count==XZ_CUE_MEMORY)return 0;memmove(s->memory+at+1,s->memory+at,(s->memory_count-at)*sizeof *s->memory);s->memory_count++;}
 s->memory[at]=e->point;return 1;
}

struct cue_job {int deck;struct xz_extra_track track;uint64_t generation;int32_t duration;struct xz_saved_cues base;struct xz_cue_edit edit;};
static struct {
 pthread_mutex_t lock;pthread_cond_t ready;pthread_t thread;int started,quit;
 struct cue_job queue[CUE_JOBS];unsigned head,count;
 int (*resolve)(int,uint32_t,uint64_t,struct xz_extra_track *);void (*notice)(const char *);
 char root[2][PATH_MAX];
} writer={.lock=PTHREAD_MUTEX_INITIALIZER,.ready=PTHREAD_COND_INITIALIZER};
/* Resolve from an allowed removable-media root, then open each directory without
   following links. The held descriptor pins all subsequent file operations. */
static int directory(const struct xz_extra_track *t){
 if(t->source<1||t->source>2||!t->anlz_dir[0])return -1;
 char root[PATH_MAX],path[PATH_MAX];
 if(!realpath(writer.root[t->source-1],root)||!realpath(t->anlz_dir,path))return -1;
 size_t length=strlen(root);if(strncmp(path,root,length)||path[length]!='/')return -1;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 char *save=NULL;for(char *part=strtok_r(path+length+1,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  if(!strcmp(part,".")||!strcmp(part,"..")){close(fd);return -1;}
  int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);fd=next;if(fd<0)return -1;
 }
 return fd;
}
static int current(const struct cue_job *j){
 struct xz_extra_track now={0};return writer.resolve&&j->generation&&
  !writer.resolve(j->track.source,j->track.track_id,j->generation,&now)&&
  !strcmp(now.path,j->track.path)&&!strcmp(now.anlz_dir,j->track.anlz_dir);
}
static void *save_worker(void *unused){
 (void)unused;
 for(;;){
  pthread_mutex_lock(&writer.lock);
  while(!writer.count&&!writer.quit)pthread_cond_wait(&writer.ready,&writer.lock);
  if(!writer.count&&writer.quit){pthread_mutex_unlock(&writer.lock);return NULL;}
  struct cue_job j=writer.queue[writer.head];writer.head=(writer.head+1)%CUE_JOBS;writer.count--;pthread_mutex_unlock(&writer.lock);
  int ok=0;
  if(current(&j)){
   int dir=directory(&j.track);
   if(dir>=0){
    struct xz_saved_cues s=j.base;int read=xz_cues_read_at(dir,j.track.track_id,j.duration,&s);
    if(read>=0&&current(&j)&&xz_cues_apply(&s,&j.edit))ok=!xz_cues_write_at(dir,j.track.track_id,j.duration,&s);
    close(dir);
   }
  }
  if(writer.notice)writer.notice(ok?"CUES SAVED TO USB":"CUE SAVE FAILED / SESSION CUES KEPT");
 }
}
int xz_cues_worker_start(int (*resolve)(int,uint32_t,uint64_t,struct xz_extra_track *),void (*notice)(const char *),const char *a,const char *b){
 if(writer.started)return 0;
 if(!a||!b||strlen(a)>=PATH_MAX||strlen(b)>=PATH_MAX)return -1;
 strcpy(writer.root[0],a);strcpy(writer.root[1],b);writer.resolve=resolve;writer.notice=notice;writer.quit=0;writer.head=writer.count=0;
 if(pthread_create(&writer.thread,NULL,save_worker,NULL))return -1;
 writer.started=1;return 0;
}
void xz_cues_worker_stop(void){
 if(!writer.started)return;
 pthread_mutex_lock(&writer.lock);writer.quit=1;pthread_cond_signal(&writer.ready);pthread_mutex_unlock(&writer.lock);
 pthread_join(writer.thread,NULL);writer.started=0;
}
int xz_cues_restore(const struct xz_extra_track *t,uint64_t generation,int32_t duration,struct xz_saved_cues *out){
 if(!writer.started||!generation)return 0;
 struct cue_job j={.track=*t,.generation=generation};if(!current(&j))return -1;
 int dir=directory(t);if(dir<0)return 0;int result=xz_cues_read_at(dir,t->track_id,duration,out);close(dir);return result;
}
int xz_cues_enqueue(int deck,const struct xz_extra_track *t,uint64_t generation,int32_t duration,const struct xz_saved_cues *s,const struct xz_cue_edit *e){
 if(!writer.started||!generation||!t||!s||!e||!xz_mods_may_write())return 0;
 pthread_mutex_lock(&writer.lock);
 int ok=!writer.quit&&writer.count<CUE_JOBS;
 if(ok){writer.queue[(writer.head+writer.count)%CUE_JOBS]=(struct cue_job){deck,*t,generation,duration,*s,*e};writer.count++;pthread_cond_signal(&writer.ready);}
 pthread_mutex_unlock(&writer.lock);return ok;
}
