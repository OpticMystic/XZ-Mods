/* SPDX-License-Identifier: GPL-2.0-only */
/* Private connector wait. All watch IO belongs to the connector; the wake pipe
   is written under route_wake_lock and closed only after the connector joins. */
#ifndef XZ_EXTRA_ROUTE_EVENTS_H
#define XZ_EXTRA_ROUTE_EVENTS_H
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>

#ifndef XZ_EXTRA_SHM_DIR
#define XZ_EXTRA_SHM_DIR "/dev/shm"
#endif
#ifndef XZ_EXTRA_PROC_DIR
#define XZ_EXTRA_PROC_DIR "/proc"
#endif
#ifndef ROUTE_EVENT_SAFETY_MS
#define ROUTE_EVENT_SAFETY_MS 5000
#endif
#define ROUTE_EVENT_MASK (IN_CREATE|IN_MOVED_TO|IN_CLOSE_WRITE|IN_DELETE|IN_ATTRIB|IN_DELETE_SELF|IN_MOVE_SELF)
static struct { int fd,parent,wake[2]; } route_events={-1,-1,{-1,-1}};

static int route_fd_flags(int fd) {
 int flags=fcntl(fd,F_GETFL);
 if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return -1;
 flags=fcntl(fd,F_GETFD);
 return flags<0?-1:fcntl(fd,F_SETFD,flags|FD_CLOEXEC);
}
static void route_events_fallback(const char *operation) {
 int error=errno;char text[256];
 if(route_events.fd>=0)close(route_events.fd);
 route_events.fd=-1;route_events.parent=-1;
 snprintf(text,sizeof text,"DECK 3/4 route: %s: %s; polling every %u ms",operation,strerror(error),(unsigned)POLL_DOWN_MS);
 xz_log(text);
}
static void route_events_close(void) {
 if(route_events.fd>=0)close(route_events.fd);
 for(int i=0;i<2;i++)if(route_events.wake[i]>=0)close(route_events.wake[i]);
 route_events.fd=route_events.parent=route_events.wake[0]=route_events.wake[1]=-1;
}
static void route_events_init(void) {
 route_events.fd=inotify_init(); /* inotify_init1 requires GLIBC_2.9. */
 if(route_events.fd<0){route_events_fallback("inotify_init unavailable");return;}
 if(route_fd_flags(route_events.fd)<0){route_events_fallback("inotify fcntl failed");return;}
 if(pipe(route_events.wake)<0){route_events_fallback("wake pipe failed");return;}
 if(route_fd_flags(route_events.wake[0])<0||route_fd_flags(route_events.wake[1])<0)
  route_events_fallback("wake pipe fcntl failed");
}
static void route_signal_locked(void) {
 pthread_cond_signal(&route_wake_cond);
 if(route_events.wake[1]>=0) {
  char byte=1;ssize_t n;
  do{n=write(route_events.wake[1],&byte,1);}while(n<0&&errno==EINTR);
  /* EAGAIN means the pipe already contains a wake. */
 }
}
static int route_watch(const char *path) {
 int wd=inotify_add_watch(route_events.fd,path,ROUTE_EVENT_MASK);
 if(wd<0&&errno!=ENOENT&&errno!=ENOTDIR)route_events_fallback("inotify_add_watch failed");
 return wd;
}
static void route_events_refresh(void) {
 if(route_events.fd<0)return;
 route_events.parent=route_watch(XZ_EXTRA_SHM_DIR);
 if(route_events.parent<0) {
  if(route_events.fd>=0)route_events_fallback("shared-memory watch unavailable");
  return;
 }
 route_watch(XZ_EXTRA_SHM_DIR "/xz-four-deck");
 if(route_events.fd<0)return;
 const char *forced=getenv("XZ_MODS_EXTRA_USB_NODE");
 if(forced&&*forced) {
  char path[PATH_MAX];
  if(snprintf(path,sizeof path,"%s",forced)<(int)sizeof path) {
   char *slash=strrchr(path,'/');
   if(slash){if(slash==path)slash[1]=0;else *slash=0;}
   else strcpy(path,".");
   route_watch(path);
  }
 }
 if(route_events.fd<0)return;
 DIR *dir=opendir(XZ_EXTRA_SHM_DIR);
 if(!dir)return;
 for(struct dirent *e;route_events.fd>=0&&(e=readdir(dir));)if(!strncmp(e->d_name,"xz-poc-",7)) {
  char path[PATH_MAX];
  if(snprintf(path,sizeof path,XZ_EXTRA_SHM_DIR "/%s",e->d_name)<(int)sizeof path)route_watch(path);
 }
 closedir(dir);
}
static void route_wake_drain(void) {
 char bytes[256];
 /* Bound draining even if a producer continually signals. */
 for(int i=0;i<16;i++)if(read(route_events.wake[0],bytes,sizeof bytes)<=0)break;
}
static int route_events_drain(void) {
 union { struct inotify_event align;char bytes[4096]; } buffer;
 int relevant=0;
 for(int i=0;i<16;i++) {
  ssize_t n=read(route_events.fd,buffer.bytes,sizeof buffer.bytes);
  if(n<0) {
   if(errno==EINTR)continue;
   if(errno!=EAGAIN)route_events_fallback("inotify read failed");
   break;
  }
  if(!n)break;
  for(size_t off=0;off<(size_t)n;) {
   const struct inotify_event *e=(const void *)(buffer.bytes+off);
   /* Parent traffic unrelated to our directories must not cause discovery. */
   const char *forced=getenv("XZ_MODS_EXTRA_USB_NODE");
   const char *name=forced?strrchr(forced,'/'):NULL;name=name?name+1:forced;
   if(e->wd!=route_events.parent||!e->len||!strcmp(e->name,"xz-four-deck")||!strncmp(e->name,"xz-poc-",7)||
      (name&&!strcmp(e->name,name)))relevant=1;
   off+=sizeof *e+e->len;
  }
 }
 return relevant;
}
static void route_events_wait(void) {
 if(route_events.fd<0){rest(POLL_DOWN_MS);return;}
 unsigned ms=ROUTE_EVENT_SAFETY_MS;
 pthread_mutex_lock(&route_wake_lock);
 unsigned scan=__atomic_load_n(&route_scan,__ATOMIC_ACQUIRE);
 if(scan==2) {
  uint32_t elapsed=now_ms()-route_scan_at;
  if(elapsed<600u&&ms>600u-elapsed)ms=600u-elapsed;
 }
 int skip=scan==1||__atomic_load_n(&route.quit,__ATOMIC_ACQUIRE);
 pthread_mutex_unlock(&route_wake_lock);
 if(skip)return;
 uint32_t began=now_ms();
 for(;;) {
  struct pollfd fds[2]={{route_events.fd,POLLIN,0},{route_events.wake[0],POLLIN,0}};
  uint32_t elapsed=now_ms()-began;
  if(elapsed>=ms)return;
  int n=poll(fds,2,(int)(ms-elapsed));
  if(n<0&&errno==EINTR)continue;
  if(n<0){route_events_fallback("event poll failed");return;}
  if(!n)return;
  if(fds[1].revents){route_wake_drain();return;}
  if(fds[0].revents&(POLLERR|POLLHUP|POLLNVAL)) {
   errno=EIO;route_events_fallback("inotify poll failed");return;
  }
  if(fds[0].revents&POLLIN) {
   /* Fixed debounce, never extended by more events. Wake/stop interrupts it. */
   struct pollfd wake={route_events.wake[0],POLLIN,0};
   poll(&wake,1,5);
   int changed=route_events_drain();
   if(wake.revents)route_wake_drain();
   if(changed||wake.revents||route_events.fd<0)return;
  }
 }
}
#endif
