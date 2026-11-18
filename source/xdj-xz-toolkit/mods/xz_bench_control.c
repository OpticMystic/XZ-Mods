#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "xz_bench_control.h"
#include "clock_compat.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include "xz_thread.h"
#include "xz_mutex.h"
#include <errno.h>
#include <time.h>
#include <stdio.h>

enum phase { EMPTY, READY, RUNNING, FINISHED };
static struct {
 pthread_mutex_t lock;pthread_t thread;int started,listener;
 unsigned stop,completion;enum phase phase;
 uint32_t mask;uint64_t deadline;
 struct xz_bench_request request,last_request;
 struct xz_bench_reply reply,last_reply;
 struct xz_bench_result completed;
 int cached;dev_t dev;ino_t ino;
} control={.lock=XZ_MUTEX_INITIALIZER,.listener=-1};
static uint64_t now_ms(void){struct timespec t;if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000u+(unsigned long)t.tv_nsec/1000000u;}
static int stopping(void){return __atomic_load_n(&control.stop,__ATOMIC_ACQUIRE)!=0;}
static int nonblock(int fd){
 int flags=fcntl(fd,F_GETFL,0),descriptor=fcntl(fd,F_GETFD,0);
 if(flags<0||descriptor<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK))return -1;
 return fcntl(fd,F_SETFD,descriptor|FD_CLOEXEC);
}
static int wait_fd(int fd,short events,uint64_t deadline){
 while(!stopping()){
  uint64_t now=now_ms();if(!now||now>=deadline)return 0;
  uint64_t left=deadline-now;struct pollfd p={fd,events,0};
  int n=poll(&p,1,(int)(left>25?25:left));
  if(n>0)return (p.revents&events)?1:-1;
  if(n<0&&errno!=EINTR)return -1;
 }return -1;
}
static int transfer(int fd,void *bytes,size_t count,int writing,uint64_t deadline){
 unsigned char *p=bytes;size_t at=0;
 while(at<count){
  if(wait_fd(fd,writing?POLLOUT:POLLIN,deadline)<=0)return -1;
  ssize_t n=writing?send(fd,p+at,count-at,MSG_NOSIGNAL):recv(fd,p+at,count-at,0);
  if(n>0)at+=(size_t)n;else if(!n)return -1;else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)return -1;
 }return 0;
}
static struct xz_bench_reply reply_for(uint32_t id,unsigned status,unsigned state){
 struct xz_bench_reply r={XZ_BENCH_VERSION,id,(uint32_t)getpid(),status,state,0,{0,0,0,0}};return r;
}
/* Called only while queue lock is held. A cancellation before RUNNING is final;
   an operation already RUNNING cannot be undone by disconnect/expiry/stop. */
static void finish_cancel(unsigned reason){
 control.reply=reply_for(control.request.request_id,reason,XZ_BENCH_CANCELLED);control.phase=FINISHED;
}
static void cache_reply(void){control.last_request=control.request;control.last_reply=control.reply;control.cached=1;}
static int peer_gone(int fd){
 unsigned char b;ssize_t n=recv(fd,&b,1,MSG_PEEK|MSG_DONTWAIT);
 return n>0?2:n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR);
}
static void serve(int fd){
 unsigned char wire[40];struct xz_bench_request req;
 uint64_t deadline=now_ms()+XZ_BENCH_TIMEOUT_MS;
 if(transfer(fd,wire,36,0,deadline))return;
 xz_bench_decode(&req,wire,9);
 struct xz_bench_reply reply=reply_for(req.request_id,XZ_BENCH_OK,XZ_BENCH_NOT_STARTED);
 unsigned char extra;ssize_t trailing=recv(fd,&extra,1,MSG_PEEK|MSG_DONTWAIT);
 if(trailing>0)reply.status=XZ_BENCH_MALFORMED;
 else if(req.version!=XZ_BENCH_VERSION||!req.request_id)reply.status=XZ_BENCH_BAD_VERSION;
 else if(req.expected_pid!=(uint32_t)getpid())reply.status=XZ_BENCH_WRONG_PID;
 else if(req.op<1||req.op>31||!(control.mask&(1u<<req.op))||req.deck>4)reply.status=XZ_BENCH_BAD_OP;
 else {
  pthread_mutex_lock(&control.lock);
  if(control.cached&&req.request_id==control.last_request.request_id){
   if(memcmp(&req,&control.last_request,sizeof req))reply.status=XZ_BENCH_CONFLICT;else reply=control.last_reply;
   pthread_mutex_unlock(&control.lock);
  }else if(control.cached&&req.request_id<control.last_request.request_id){reply.status=XZ_BENCH_STALE;pthread_mutex_unlock(&control.lock);}
  else {
   control.request=req;control.deadline=deadline;control.phase=READY;
   /* EOF already observed must become final before the owner can claim. */
   if(trailing==0)finish_cancel(XZ_BENCH_DISCONNECTED);
   __atomic_store_n(&control.completion,0,__ATOMIC_RELEASE);
   pthread_mutex_unlock(&control.lock);
   for(;;){
    int gone=peer_gone(fd);uint64_t now=now_ms();
    pthread_mutex_lock(&control.lock);
    if(control.phase==READY&&(gone||stopping()||!now||now>=deadline))finish_cancel(stopping()?XZ_BENCH_STOPPED:gone==2?XZ_BENCH_MALFORMED:gone?XZ_BENCH_DISCONNECTED:XZ_BENCH_EXPIRED);
    if(control.phase==RUNNING&&__atomic_load_n(&control.completion,__ATOMIC_ACQUIRE)){
     control.reply=reply_for(req.request_id,XZ_BENCH_OK,XZ_BENCH_EXECUTED);
     control.reply.operation_status=control.completed.status;memcpy(control.reply.args,control.completed.args,sizeof control.reply.args);control.phase=FINISHED;
    }
    if(control.phase==FINISHED){reply=control.reply;cache_reply();control.phase=EMPTY;pthread_mutex_unlock(&control.lock);break;}
    pthread_mutex_unlock(&control.lock);
    /* Waiting only on worker: in-flight bounded callback must finish even if the
       peer is gone or stop was requested. No forced callback/thread cancellation. */
    struct pollfd p={fd,POLLIN,0};if(!gone&&!stopping())poll(&p,1,5);else {struct timespec t={0,1000000};nanosleep(&t,NULL);}
   }
  }
 }
 xz_bench_encode(wire,&reply,10);(void)transfer(fd,wire,40,1,now_ms()+250u);
}
static void *worker(void *unused){
 (void)unused;
 while(!stopping()){
  if(wait_fd(control.listener,POLLIN,now_ms()+100u)<=0)continue;
  int fd=accept(control.listener,NULL,NULL);if(fd<0)continue;
  if(!nonblock(fd))serve(fd);
  close(fd);
 }return NULL;
}
static void cleanup_path(void){
 struct stat st;
 if(!lstat(XZ_BENCH_SOCKET_PATH,&st)&&S_ISSOCK(st.st_mode)&&st.st_uid==geteuid()&&st.st_dev==control.dev&&st.st_ino==control.ino)(void)unlink(XZ_BENCH_SOCKET_PATH);
}
int xz_bench_control_start(int enabled,uint32_t op_mask){
 if(!enabled)return 0;
 if(control.started){errno=EALREADY;return -1;}
 struct stat st;if(!lstat(XZ_BENCH_SOCKET_PATH,&st)){errno=EEXIST;return -1;}if(errno!=ENOENT)return -1;
 struct sockaddr_un addr;memset(&addr,0,sizeof addr);addr.sun_family=AF_UNIX;
 if(strlen(XZ_BENCH_SOCKET_PATH)>=sizeof addr.sun_path){errno=ENAMETOOLONG;return -1;}
 memcpy(addr.sun_path,XZ_BENCH_SOCKET_PATH,strlen(XZ_BENCH_SOCKET_PATH)+1);
 int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return -1;
 if(nonblock(fd)||bind(fd,(struct sockaddr *)&addr,sizeof addr)){int e=errno;close(fd);errno=e;return -1;}
 if(lstat(XZ_BENCH_SOCKET_PATH,&st)||!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid()){close(fd);return -1;}
 control.dev=st.st_dev;control.ino=st.st_ino;
 if(chmod(XZ_BENCH_SOCKET_PATH,0600)||listen(fd,1)){int e=errno;close(fd);cleanup_path();errno=e;return -1;}
 control.listener=fd;control.mask=op_mask;control.phase=EMPTY;
 __atomic_store_n(&control.stop,0,__ATOMIC_RELEASE);__atomic_store_n(&control.completion,0,__ATOMIC_RELEASE);
 int error=XZ_WORKER_CREATE(&control.thread,worker,NULL);
 if(error){close(fd);control.listener=-1;cleanup_path();errno=error;return -1;}
 control.started=1;return 1;
}
void xz_bench_control_stop(void){
 if(!control.started)return;
 __atomic_store_n(&control.stop,1,__ATOMIC_RELEASE);
 /* Lifecycle is serialized and no tick is active here. Never join with queue locked. */
 pthread_join(control.thread,NULL);close(control.listener);control.listener=-1;cleanup_path();control.started=0;
}
int xz_bench_control_tick(xz_bench_callback callback,void *opaque){
 if(!callback||stopping()||pthread_mutex_trylock(&control.lock))return 0;
 if(control.phase!=READY){pthread_mutex_unlock(&control.lock);return 0;}
 uint64_t now=now_ms();
 if(stopping()||!now||now>=control.deadline){finish_cancel(stopping()?XZ_BENCH_STOPPED:XZ_BENCH_EXPIRED);pthread_mutex_unlock(&control.lock);return 0;}
 struct xz_bench_request req=control.request;control.phase=RUNNING;
 pthread_mutex_unlock(&control.lock);
 struct xz_bench_result result={0,{0,0,0,0}};callback(&req,&result,opaque);
 control.completed=result;__atomic_store_n(&control.completion,1,__ATOMIC_RELEASE);return 1;
}
