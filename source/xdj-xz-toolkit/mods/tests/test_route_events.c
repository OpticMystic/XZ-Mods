/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual connector, fake filesystem/helper identity, and a transport that never
   opens a device. Run in a private temporary working directory. */
#define _GNU_SOURCE
#ifdef NDEBUG
#error Route event acceptance requires active assertions
#endif
#define XZ_EXTRA_SHM_DIR "shm"
#define XZ_EXTRA_PROC_DIR "proc"
#define USB_MAJOR 1
#define XZ_SYNC_OBSERVATION 0
#include "../extra_decks.c"
#include <assert.h>
#include <stdarg.h>

static unsigned opens,log_fallback;
static int no_inotify,miss_events,fail_watch,fail_pipe_flags;
int __real_inotify_init(void);
int __wrap_inotify_init(void) {
 if(no_inotify){errno=ENOSYS;return -1;}
 return __real_inotify_init();
}
int __real_inotify_add_watch(int,const char *,uint32_t);
int __wrap_inotify_add_watch(int fd,const char *path,uint32_t mask) {
 if(fail_watch){errno=EACCES;return -1;}
 return __real_inotify_add_watch(fd,miss_events?"quiet":path,mask);
}
int __real_fcntl(int,int,...);
int __wrap_fcntl(int fd,int command,...) {
 if(command==F_GETFL||command==F_GETFD)return __real_fcntl(fd,command);
 assert(command==F_SETFL||command==F_SETFD);
 va_list ap;va_start(ap,command);int flags=va_arg(ap,int);va_end(ap);
#ifndef ROUTE_EVENT_BASELINE
 if(fail_pipe_flags&&fd==route_events.wake[1]&&command==F_SETFL){errno=EPERM;return -1;}
#endif
 return __real_fcntl(fd,command,flags);
}
void xz_log(const char *s) {
 if(strstr(s,"polling every")&&(strstr(s,"Function not implemented")||strstr(s,"Permission denied")||strstr(s,"Operation not permitted")))
  __atomic_add_fetch(&log_fallback,1,__ATOMIC_RELEASE);
}
struct xz_usb_transport { int dummy; };
xz_usb_result xz_usb_transport_open(const char *path,xz_usb_transport **out) {
 (void)out;assert(!strncmp(path,"shm/",4));
 __atomic_add_fetch(&opens,1,__ATOMIC_RELEASE);return XZ_USB_BUSY;
}
xz_usb_result xz_usb_transport_close(xz_usb_transport **p){*p=NULL;return XZ_USB_OK;}
xz_usb_result xz_usb_transport_position(xz_usb_transport *p,xz_usb_position *o){(void)p;memset(o,0,sizeof *o);return XZ_USB_OK;}
xz_usb_result xz_usb_transport_submit(xz_usb_transport *p,xz_usb_deck d,uint32_t g,const float *s,uint32_t n,uint64_t a){(void)p;(void)d;(void)g;(void)s;(void)n;(void)a;return XZ_USB_OK;}
xz_usb_result xz_usb_transport_flush(xz_usb_transport *p,xz_usb_deck d,uint32_t *g){(void)p;(void)d;(void)g;return XZ_USB_OK;}
xz_usb_result xz_usb_transport_flush_and_submit(xz_usb_transport *p,xz_usb_deck d,uint32_t g,const float *s,uint32_t n,uint64_t a,uint32_t *o){(void)p;(void)d;(void)g;(void)s;(void)n;(void)a;(void)o;return XZ_USB_OK;}
struct xz_xp_players { int dummy; };
static struct xz_xp_players fake_players;
struct xz_xp_players *xz_xp_create(const struct xz_xp_env *e){(void)e;return &fake_players;}
int xz_xp_start(struct xz_xp_players *p){(void)p;return 0;}
void xz_xp_stop(struct xz_xp_players *p){(void)p;}
void xz_xp_destroy(struct xz_xp_players *p){(void)p;}
int xz_xp_read_held(struct xz_xp_players *p,struct xz_extra_decks *o,struct xz_extra_analysis_hold *h){(void)p;(void)h;memset(o,0,sizeof *o);return 1;}
void xz_xp_release_analysis(struct xz_extra_decks *o,struct xz_extra_analysis_hold *h){(void)o;(void)h;}
void xz_xp_notice(struct xz_xp_players *p,const char *s){(void)p;(void)s;}
void xz_xp_set_native_sync_clock(struct xz_xp_players *p,int(*f)(struct xz_sync_native_snapshot *)){(void)p;(void)f;}
void xz_xp_set_cue_store(struct xz_xp_players *p,xz_cue_read_fn r,xz_cue_write_fn w){(void)p;(void)r;(void)w;}
int xz_cues_worker_start(int(*r)(int,uint32_t,uint64_t,struct xz_extra_track *),void(*n)(const char *),const char *a,const char *b){(void)r;(void)n;(void)a;(void)b;return 0;}
void xz_cues_worker_stop(void){}
int xz_cues_restore(const struct xz_extra_track *t,uint64_t g,int32_t d,struct xz_saved_cues *o){(void)t;(void)g;(void)d;(void)o;return -1;}
int xz_cues_enqueue(int a,const struct xz_extra_track *t,uint64_t g,int32_t d,const struct xz_saved_cues *s,const struct xz_cue_edit *e){(void)a;(void)t;(void)g;(void)d;(void)s;(void)e;return 0;}

static void wait_ms(unsigned ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000L};nanosleep(&t,NULL);}
static void put(const char *path,const char *s,size_t n) {
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(s,1,n,f)==n);assert(!fclose(f));
}
static unsigned count(void){return __atomic_load_n(&opens,__ATOMIC_ACQUIRE);}
static uint32_t revision(void){return xz_extra_decks_route_status().revision;}
static void until_open(unsigned before,unsigned limit) {
 uint32_t began=now_ms();
 while(count()==before&&(uint32_t)(now_ms()-began)<limit)wait_ms(1);
 assert(count()>before);
}
static void start(void) {
 unsigned before=revision();assert(!xz_extra_decks_start());
 uint32_t began=now_ms();while(revision()==before&&(uint32_t)(now_ms()-began)<500u)wait_ms(1);
 assert(revision()!=before);wait_ms(30); /* initial discovery completed, wait is armed */
#ifndef ROUTE_EVENT_BASELINE
 if(!no_inotify&&!fail_watch&&!fail_pipe_flags) {
  assert(route_events.fd>=0);
  int fds[]={route_events.fd,route_events.wake[0],route_events.wake[1]};
  for(unsigned i=0;i<sizeof fds/sizeof *fds;i++) {
   assert((fcntl(fds[i],F_GETFL)&O_NONBLOCK)!=0);
   assert((fcntl(fds[i],F_GETFD)&FD_CLOEXEC)!=0);
  }
 }
#endif
}
static void stop(void) {
#ifndef ROUTE_EVENT_BASELINE
 int fd=route_events.fd,rd=route_events.wake[0],wr=route_events.wake[1];
#endif
 uint32_t began=now_ms();xz_extra_decks_stop();
 unsigned elapsed=now_ms()-began;assert(elapsed<50u);
#ifndef ROUTE_EVENT_BASELINE
 if(fd>=0){errno=0;assert(fcntl(fd,F_GETFD)<0&&errno==EBADF);}
 if(rd>=0){errno=0;assert(fcntl(rd,F_GETFD)<0&&errno==EBADF);}
 if(wr>=0){errno=0;assert(fcntl(wr,F_GETFD)<0&&errno==EBADF);}
#endif
 printf("shutdown_ms=%u\n",elapsed);
}
static void make_node(const char *path){assert(!symlink("/dev/null",path));}
static void node_check(const char *path,const char *label) {
 start();unsigned before=count();uint32_t began=now_ms();make_node(path);
 until_open(before,1600);unsigned elapsed=now_ms()-began;
 printf("%s_ms=%u\n",label,elapsed);fflush(stdout);
#ifndef ROUTE_EVENT_BASELINE
 assert(elapsed<50u);
#endif
 /* Deleting the candidate must immediately clear the busy-node observation. */
 assert(!unlink(path));began=now_ms();
 while(xz_extra_decks_route_status().state!=XZ_EXTRA_ROUTE_NOT_DETECTED&&(uint32_t)(now_ms()-began)<1600u)wait_ms(1);
 assert(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_NOT_DETECTED);
#ifndef ROUTE_EVENT_BASELINE
 assert((uint32_t)(now_ms()-began)<50u);
#endif
 stop();
}
static void helper_status(const char *reason,int atomic_replace) {
 char text[512];snprintf(text,sizeof text,"{\"schema\":\"xz-bridge-status/1\",\"state\":\"waiting\",\"reason\":\"%s\",\"terminal_reason\":null,\"retryable\":true,\"attempt\":1,\"route_ready\":false}\n",reason);
 const char *path=atomic_replace?"shm/xz-four-deck/status.tmp":"shm/xz-four-deck/status.json";
 put(path,text,strlen(text));
 if(atomic_replace)assert(!rename(path,"shm/xz-four-deck/status.json"));
}
static void status_check(void) {
 assert(!mkdir("proc",0700));assert(!mkdir("proc/42",0700));
 const char cmd[]="/dev/shm/xz-auto-fixture/xz-auto-bridge\0--run\0" "123\0";
 put("proc/42/cmdline",cmd,sizeof cmd-1);assert(!symlink(cmd,"proc/42/exe"));
 put("proc/stat","btime 1000\n",11);
 char text[512]="42 (xz-auto-bridge) S";
 for(int field=4;field<22;field++)strcat(text," 0");strcat(text," 100 0\n");
 put("proc/42/stat",text,strlen(text));put("shm/xz-four-deck/bridge.pid","42\n",3);
 helper_status("WAIT_LOOPBACK",0);start();
 assert(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_NOT_DETECTED);
 for(int replace=0;replace<2;replace++) {
  uint32_t began=now_ms();helper_status("WAIT_NATIVE_PAUSED",replace);
  while(xz_extra_decks_route_status().state!=XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP&&(uint32_t)(now_ms()-began)<1600u)wait_ms(1);
  unsigned elapsed=now_ms()-began;assert(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP);
  printf("status_%s_ms=%u\n",replace?"rename":"rewrite",elapsed);fflush(stdout);
#ifndef ROUTE_EVENT_BASELINE
  assert(elapsed<50u);
#endif
  helper_status("WAIT_LOOPBACK",0);
  began=now_ms();while(xz_extra_decks_route_status().state!=XZ_EXTRA_ROUTE_NOT_DETECTED&&(uint32_t)(now_ms()-began)<1600u)wait_ms(1);
  assert(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_NOT_DETECTED);
 }
 stop();assert(!unlink("shm/xz-four-deck/status.json"));assert(!unlink("shm/xz-four-deck/bridge.pid"));
}
int main(void) {
 assert(!mkdir("shm",0700));assert(!mkdir("quiet",0700));
 assert(!mkdir("shm/xz-four-deck",0700));
 node_check("shm/xz-four-deck/usb-audio","node");status_check();
 assert(!rmdir("shm/xz-four-deck"));start();unsigned before=count();
 uint32_t began=now_ms();assert(!mkdir("shm/xz-four-deck",0700));make_node("shm/xz-four-deck/usb-audio");until_open(before,1600);
 printf("late_directory_ms=%u\n",now_ms()-began);
#ifndef ROUTE_EVENT_BASELINE
 assert((uint32_t)(now_ms()-began)<50u);
#endif
 stop();assert(!unlink("shm/xz-four-deck/usb-audio"));
 assert(!mkdir("shm/xz-poc-fixture",0700));node_check("shm/xz-poc-fixture/audio-usb","fallback_node");
 setenv("XZ_MODS_EXTRA_USB_NODE","shm/custom-node",1);node_check("shm/custom-node","override_node");
 /* Override must not silently discover a default node. */
 make_node("shm/xz-four-deck/usb-audio");start();before=count();wait_ms(100);assert(count()==before);stop();
 assert(!unlink("shm/xz-four-deck/usb-audio"));unsetenv("XZ_MODS_EXTRA_USB_NODE");
#ifndef ROUTE_EVENT_BASELINE
 /* A directory event precedes the node by a separate connector iteration. */
 assert(!rmdir("shm/xz-four-deck"));start();unsigned rev=revision();
 assert(!mkdir("shm/xz-four-deck",0700));began=now_ms();
 while(revision()==rev&&(uint32_t)(now_ms()-began)<50u)wait_ms(1);
 assert(revision()!=rev);wait_ms(30);before=count();began=now_ms();
 make_node("shm/xz-four-deck/usb-audio");until_open(before,100);
 assert((uint32_t)(now_ms()-began)<50u);printf("late_directory_then_node_ms=%u\n",now_ms()-began);
 stop();assert(!unlink("shm/xz-four-deck/usb-audio"));
 start();rev=revision();assert(!mkdir("shm/xz-poc-late",0700));began=now_ms();
 while(revision()==rev&&(uint32_t)(now_ms()-began)<50u)wait_ms(1);
 assert(revision()!=rev);wait_ms(30);before=count();began=now_ms();
 make_node("shm/xz-poc-late/audio-usb");until_open(before,100);
 assert((uint32_t)(now_ms()-began)<50u);printf("late_fallback_then_node_ms=%u\n",now_ms()-began);
 stop();assert(!unlink("shm/xz-poc-late/audio-usb"));
 /* The same RESCAN called by the four-deck card must interrupt the event wait. */
 start();rev=revision();began=now_ms();xz_extra_decks_rescan();
 assert(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_SCANNING);
 while(revision()==rev&&(uint32_t)(now_ms()-began)<50u)wait_ms(1);
 assert(revision()!=rev);printf("rescan_ms=%u\n",now_ms()-began);
 wait_ms(100);assert(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_SCANNING);
 while(xz_extra_decks_route_status().state==XZ_EXTRA_ROUTE_SCANNING&&(uint32_t)(now_ms()-began)<750u)wait_ms(1);
 assert((uint32_t)(now_ms()-began)>=600u&&xz_extra_decks_route_status().state!=XZ_EXTRA_ROUTE_SCANNING);stop();
 /* Drop every useful event while leaving inotify healthy. */
 miss_events=1;start();before=count();began=now_ms();make_node("shm/xz-four-deck/usb-audio");until_open(before,ROUTE_EVENT_SAFETY_MS+300);
 assert((uint32_t)(now_ms()-began)>=ROUTE_EVENT_SAFETY_MS-100u);
 printf("missed_event_ms=%u safety_ms=%u\n",now_ms()-began,(unsigned)ROUTE_EVENT_SAFETY_MS);
 stop();miss_events=0;assert(!unlink("shm/xz-four-deck/usb-audio"));
 no_inotify=1;start();before=count();began=now_ms();make_node("shm/xz-four-deck/usb-audio");until_open(before,1300);
 assert((uint32_t)(now_ms()-began)>=800u);assert(__atomic_load_n(&log_fallback,__ATOMIC_ACQUIRE)>0);
 printf("inotify_unavailable_ms=%u\n",now_ms()-began);stop();no_inotify=0;assert(!unlink("shm/xz-four-deck/usb-audio"));
 fail_watch=1;start();assert(route_events.fd<0);rev=revision();began=now_ms();xz_extra_decks_rescan();
 while(revision()==rev&&(uint32_t)(now_ms()-began)<50u)wait_ms(1);
 assert(revision()!=rev);assert(__atomic_load_n(&log_fallback,__ATOMIC_ACQUIRE)>=2u);
 printf("watch_failure_rescan_ms=%u\n",now_ms()-began);stop();fail_watch=0;
 fail_pipe_flags=1;start();assert(route_events.fd==-1&&route_events.wake[0]==-1&&route_events.wake[1]==-1);
 rev=revision();began=now_ms();xz_extra_decks_rescan();
 while(revision()==rev&&(uint32_t)(now_ms()-began)<50u)wait_ms(1);
 assert(revision()!=rev);assert(__atomic_load_n(&log_fallback,__ATOMIC_ACQUIRE)>=3u);
 printf("pipe_flags_failure_rescan_ms=%u\n",now_ms()-began);stop();fail_pipe_flags=0;
 start();before=revision();began=now_ms();
 for(int i=0;i<1000;i++){put("shm/xz-four-deck/storm","x",1);assert(!unlink("shm/xz-four-deck/storm"));}
 wait_ms(80);unsigned checks=revision()-before;assert(checks<100u);
 before=revision();wait_ms(100);assert(revision()==before);
 printf("storm_1000_checks=%u elapsed_ms=%u\n",checks,now_ms()-began);stop();
#endif
 puts("PASS route events");return 0;
}
