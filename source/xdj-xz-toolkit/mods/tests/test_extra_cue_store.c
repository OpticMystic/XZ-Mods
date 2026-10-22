#define _GNU_SOURCE
#include "extra_cue_store.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static struct xz_extra_track track;static unsigned notices;static int generation_ok=1;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t gate_ready=PTHREAD_COND_INITIALIZER;
static int blocked,entered,released;
static int resolve(int source,uint32_t id,uint64_t generation,struct xz_extra_track *out){
 pthread_mutex_lock(&gate);if(blocked){entered=1;pthread_cond_signal(&gate_ready);while(!released)pthread_cond_wait(&gate_ready,&gate);}pthread_mutex_unlock(&gate);
 if(source!=1||id!=7||generation!=42||!generation_ok)return -1;*out=track;return 0;
}
static void notice(const char *s){assert(s);__atomic_add_fetch(&notices,1,__ATOMIC_RELEASE);}
int main(int argc,char **argv){
 assert(argc==2);char folder[480];assert(snprintf(folder,sizeof folder,"%s/anlz",argv[1])<(int)sizeof folder);assert(!mkdir(folder,0700));
 int dir=open(folder,O_RDONLY|O_DIRECTORY);assert(dir>=0);
 struct xz_saved_cues a={0},b={0};a.hot[0]=(struct xz_extra_cue){1,100,0xff0000,0};a.memory_count=1;a.memory[0]=(struct xz_extra_cue){1,500,0,1500};
 a.hot[7]=(struct xz_extra_cue){1,10000,0xff8000,0}; /* An end marker is a legal point, but cannot start a loop. */
 assert(!xz_cues_read_at(dir,7,10000,&b));assert(!xz_cues_write_at(dir,7,10000,&a));assert(xz_cues_read_at(dir,7,10000,&b)==1&&!memcmp(&a,&b,sizeof a));
 assert(xz_cues_read_at(dir,7,10001,&b)==-1);assert(!memcmp(&a,&b,sizeof a));
 struct xz_saved_cues bad=a;bad.memory[0].end_ms=10001;assert(xz_cues_write_at(dir,7,10000,&bad)==-1);assert(xz_cues_read_at(dir,7,10000,&b)==1&&!memcmp(&a,&b,sizeof a));
 int fd=openat(dir,"xzmods-cues-7.bin",O_WRONLY);assert(fd>=0);unsigned char corrupt=0xff;assert(pwrite(fd,&corrupt,1,26)==1);close(fd);
 assert(xz_cues_read_at(dir,7,10000,&b)==-1&&!memcmp(&a,&b,sizeof a));assert(!xz_cues_write_at(dir,7,10000,&a));
 assert(!symlinkat("xzmods-cues-7.bin",dir,"xzmods-cues-8.bin"));assert(xz_cues_read_at(dir,8,10000,&b)==-1);
 assert(!xz_cues_write_at(dir,8,10000,&a));assert(xz_cues_read_at(dir,7,10000,&b)==1&&!memcmp(&a,&b,sizeof a));
 track.source=1;track.track_id=7;snprintf(track.anlz_dir,sizeof track.anlz_dir,"%s",folder);snprintf(track.path,sizeof track.path,"%s/audio.wav",argv[1]);
 assert(!xz_cues_worker_start(resolve,notice,argv[1],argv[1]));
 struct xz_cue_edit h={.kind=XZ_CUE_EDIT_HOT,.pad=1,.point={1,250,0x00ff00,0}};
 struct xz_cue_edit m={.kind=XZ_CUE_EDIT_MEMORY,.point={1,800,0,1200}};
 assert(xz_cues_enqueue(0,&track,42,10000,&a,&h));assert(xz_cues_enqueue(1,&track,42,10000,&a,&m));
 xz_cues_worker_stop();assert(notices==2);assert(xz_cues_read_at(dir,7,10000,&b)==1);
 assert(b.hot[0].set&&b.hot[1].set&&b.memory_count==2&&b.memory[1].ms==800);
 assert(!xz_cues_worker_start(resolve,notice,argv[1],argv[1]));generation_ok=0;
 h.pad=2;assert(xz_cues_enqueue(0,&track,42,10000,&a,&h));xz_cues_worker_stop();
 assert(xz_cues_read_at(dir,7,10000,&b)==1&&!b.hot[2].set);generation_ok=1;
 assert(!xz_cues_worker_start(resolve,notice,argv[1],argv[1]));assert(!setenv("XZ_MODS_SETTINGS_READONLY","1",1));
 assert(!xz_cues_enqueue(0,&track,42,10000,&a,&h));xz_cues_worker_stop();assert(!unsetenv("XZ_MODS_SETTINGS_READONLY"));
 /* A blocked filesystem worker cannot block the enqueue path or consume more than the fixed queue. */
 unsigned before=notices;blocked=1;entered=released=0;
 assert(!xz_cues_worker_start(resolve,notice,argv[1],argv[1]));assert(xz_cues_enqueue(0,&track,42,10000,&a,&h));
 pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&gate_ready,&gate);pthread_mutex_unlock(&gate);
 for(unsigned i=0;i<16;i++)assert(xz_cues_enqueue((int)(i%2),&track,42,10000,&a,&h));
 assert(!xz_cues_enqueue(0,&track,42,10000,&a,&h));
 pthread_mutex_lock(&gate);released=1;pthread_cond_broadcast(&gate_ready);pthread_mutex_unlock(&gate);
 xz_cues_worker_stop();assert(notices-before==17);blocked=0;
 /* Even a resolver-approved path cannot escape the selected USB root through a directory link. */
 char outside[500];assert(snprintf(outside,sizeof outside,"%s/../outside",argv[1])<(int)sizeof outside);assert(!mkdir(outside,0700));
 int root=open(argv[1],O_RDONLY|O_DIRECTORY);assert(root>=0);assert(!symlinkat("../outside",root,"anlz-link"));close(root);
 assert(snprintf(track.anlz_dir,sizeof track.anlz_dir,"%s/anlz-link",argv[1])<(int)sizeof track.anlz_dir);
 assert(!xz_cues_worker_start(resolve,notice,argv[1],argv[1]));assert(xz_cues_enqueue(0,&track,42,10000,&a,&h));xz_cues_worker_stop();
 int escaped=open(outside,O_RDONLY|O_DIRECTORY);assert(escaped>=0);assert(openat(escaped,"xzmods-cues-7.bin",O_RDONLY)<0);close(escaped);
 close(dir);puts("PASS cue sidecar: roundtrip, identity, bounds, corrupt-data preservation, symlink refusal, atomic replacement, concurrent deck edits, generation and read-only fences");return 0;
}
