/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#ifdef NDEBUG
#error Route status acceptance requires active assertions
#endif
#include "../extra_decks.c"
#define main existing_connector_main
#include "../test_extra_decks.c"
#undef main
#include <utime.h>

static _Thread_local int reader_no_io;
int __real_open(const char *,int,...);
int __wrap_open(const char *p,int f,...) { assert(!reader_no_io);return __real_open(p,f); }
ssize_t __real_read(int,void *,size_t);
ssize_t __wrap_read(int fd,void *p,size_t n) { assert(!reader_no_io);return __real_read(fd,p,n); }
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m) { assert(!reader_no_io);return __real_fopen(p,m); }
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s) { assert(!reader_no_io);return __real_stat(p,s); }
int __real_pthread_mutex_lock(pthread_mutex_t *);
int __wrap_pthread_mutex_lock(pthread_mutex_t *p) { assert(!reader_no_io);return __real_pthread_mutex_lock(p); }

static struct xz_extra_route_status read_status(void) {
 reader_no_io=1;struct xz_extra_route_status s=xz_extra_decks_route_status();reader_no_io=0;return s;
}
static void put(const char *path,const void *s,size_t n) {
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(s,1,n,f)==n);assert(!fclose(f));
}
static void stamp(const char *path,time_t sec,long ns) {
 struct timespec t[2]={{sec,ns},{sec,ns}};assert(!utimensat(AT_FDCWD,path,t,0));
}
static const char ready_json[]="{\"schema\":\"xz-bridge-status/1\",\"state\":\"ready\",\"reason\":\"READY_CH3_CH4\",\"terminal_reason\":null,\"retryable\":false,\"attempt\":1,\"route_ready\":true}\n";
static void process_stat(char state,unsigned long long born) {
 char text[1024];size_t n=(size_t)snprintf(text,sizeof text,"42 (xz-auto-bridge) %c",state);
 for(int field=4;field<22;field++)n+=(size_t)snprintf(text+n,sizeof text-n," 0");
 snprintf(text+n,sizeof text-n," %llu 0\n",born);put("proc/42/stat",text,strlen(text));
}
static void stale_checks(void) {
 assert(!mkdir("bridge",0700));assert(!mkdir("proc",0700));assert(!mkdir("proc/42",0700));
 put("bridge/bridge.pid","42\n",3);stamp("bridge/bridge.pid",2000,100);
 put("bridge/status.json",ready_json,sizeof ready_json-1);stamp("bridge/status.json",2000,200);
 const char command[]="/dev/shm/xz-auto-4bb9db00a2ab/xz-auto-bridge\0--run\0" "123\0";
 put("proc/42/cmdline",command,sizeof command-1);
 assert(!symlink(command,"proc/42/exe"));
 put("proc/stat","cpu 0\nbtime 1000\n",17);
 process_stat('S',(unsigned long long)sysconf(_SC_CLK_TCK)*100u);
 struct route_bridge b=route_bridge_read("bridge","proc");assert(b.valid);
 assert(route_observed(&b,0,0,0,0,XZ_USB_OK).state==XZ_EXTRA_ROUTE_STARTING);
 stamp("bridge/status.json",2000,99);assert(!route_bridge_read("bridge","proc").valid);
 stamp("bridge/status.json",2000,200);
 process_stat('Z',100);assert(!route_bridge_read("bridge","proc").valid);
 process_stat('S',(unsigned long long)sysconf(_SC_CLK_TCK)*1100u);assert(!route_bridge_read("bridge","proc").valid);
 process_stat('S',100);
 put("proc/42/cmdline","/bin/other\0--run\0" "123\0",21);assert(!route_bridge_read("bridge","proc").valid);
 put("proc/42/cmdline",command,sizeof command-1);
 put("bridge/bridge.pid","43\n",3);assert(!route_bridge_read("bridge","proc").valid);
 put("bridge/bridge.pid","42\n",3);stamp("bridge/bridge.pid",2000,100);
 assert(route_bridge_read("bridge","proc").valid);
 assert(!unlink("proc/42/exe"));assert(!symlink("/bin/other","proc/42/exe"));
 assert(!route_bridge_read("bridge","proc").valid);
 assert(!unlink("bridge/bridge.pid"));assert(!route_bridge_read("bridge","proc").valid);
 puts("PASS stale PID, identity, zombie, process start, missing PID and nanosecond file age");
}
static void mapping_checks(void) {
 #include "route_reason_checks.h"
 for(size_t i=0;i<sizeof route_reasons/sizeof *route_reasons;i++) {
  const struct route_reason *r=&route_reasons[i];assert(strlen(r->sentence)<=80);
  assert(!strstr(r->sentence,"TERMINATED")&&!strstr(r->sentence,"WAIT_")&&!strstr(r->sentence,"XZ"));
 }
 struct route_bridge b={0};
 assert(route_observed(&b,0,0,0,0,XZ_USB_OK).state==XZ_EXTRA_ROUTE_NOT_DETECTED);
 assert(route_observed(&b,0,0,1,0,XZ_USB_OK).state==XZ_EXTRA_ROUTE_LOST);
 assert(route_observed(&b,1,0,0,0,XZ_USB_OK).state==XZ_EXTRA_ROUTE_STARTING);
 assert(route_observed(&b,0,0,0,3,XZ_USB_OK).state==XZ_EXTRA_ROUTE_FAILED);
 assert(route_observed(&b,1,0,0,0,XZ_USB_BUSY).fail_part==XZ_EXTRA_ROUTE_PART_SOCKET);
 assert(route_observed(&b,1,0,0,0,XZ_USB_WRONG_DEVICE).fail_part==XZ_EXTRA_ROUTE_PART_CABLE);
 assert(route_bridge_parse(ready_json,&b));strcpy(b.terminal,"TERMINATED_SIGNAL");
 assert(route_observed(&b,1,0,1,0,XZ_USB_OK).state==XZ_EXTRA_ROUTE_FAILED);
 assert(route_observed(&b,1,1,1,3,XZ_USB_OK).state==XZ_EXTRA_ROUTE_READY);
 assert(route_reason_map("SOME_NEW_PRIVATE_CODE",0).state==XZ_EXTRA_ROUTE_STARTING);
 assert(route_reason_map("SOME_NEW_PRIVATE_CODE",1).state==XZ_EXTRA_ROUTE_FAILED);
 assert(!strstr(route_reason_map("SOME_NEW_PRIVATE_CODE",1).reason,"PRIVATE"));
 assert(!route_bridge_parse("{}",&b));assert(!route_bridge_parse("{\"schema\":\"xz-bridge-status/2\"}",&b));
 char truncated[sizeof ready_json];memcpy(truncated,ready_json,sizeof ready_json);truncated[sizeof ready_json-4]=0;
 assert(!route_bridge_parse(truncated,&b));
 puts("PASS complete bridge reason coverage, unknown reasons, transport truth and LOST distinction");
}
static unsigned wait_entered,wait_done;
static void *fake_connector_wait(void *unused) {
 (void)unused;__atomic_store_n(&wait_entered,1,__ATOMIC_RELEASE);rest(1000);
 __atomic_store_n(&wait_done,1,__ATOMIC_RELEASE);return NULL;
}
static void rescan_checks(void) {
 assert(read_status().state==XZ_EXTRA_ROUTE_NO_ENGINE);
 xz_extra_decks_rescan();assert(!route_scan);
 control_update(CONTROL_AVAILABLE,CONTROL_AVAILABLE);
 pthread_t thread;assert(!pthread_create(&thread,NULL,fake_connector_wait,NULL));
 while(!__atomic_load_n(&wait_entered,__ATOMIC_ACQUIRE))wait_ms(1);
 wait_ms(30);uint32_t began=now_ms();xz_extra_decks_rescan();uint32_t first=route_scan_at;
 assert(read_status().state==XZ_EXTRA_ROUTE_SCANNING);
 for(int i=0;i<50&&!__atomic_load_n(&wait_done,__ATOMIC_ACQUIRE);i++)wait_ms(2);
 assert(__atomic_load_n(&wait_done,__ATOMIC_ACQUIRE));assert((uint32_t)(now_ms()-began)<150u);
 assert(!pthread_join(thread,NULL));
 for(int i=0;i<10;i++)xz_extra_decks_rescan();assert(route_scan_at==first&&route_scan==1);
 route_scan_finish(first+599u,0);assert(route_scan==1); /* request arrived after attempt began */
 route_scan_finish(first+599u,1);assert(route_scan==2);
 xz_extra_decks_rescan();assert(route_scan_at==first&&route_scan==2);
 route_scan_finish(first+600u,2);assert(!route_scan);
 route_scan_at=UINT32_MAX-100u;route_scan=1;
 route_scan_finish(498u,1);assert(route_scan==2);route_scan_finish(499u,2);assert(!route_scan);
 puts("PASS RESCAN wakes 1000 ms wait in <150 ms; idempotent, 600 ms minimum and clock wrap");
}
static unsigned publish_done;
static void *publisher(void *unused) {
 (void)unused;
 for(unsigned i=1;i<30000;i++) {
  struct xz_extra_route_status s=route_message(XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_NONE,"Starting.");
  snprintf(s.reason,sizeof s.reason,"Check %u.",i);s.fail_part=(enum xz_extra_route_part)(i%5u);
  route_status_publish(s,i,0);
 }
 __atomic_store_n(&publish_done,1,__ATOMIC_RELEASE);return NULL;
}
static void publication_checks(void) {
 struct xz_extra_route_status s=route_message(XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"Waiting.");
 route_status_publish(s,1,0);uint32_t before=read_status().revision;assert(!xz_extra_decks_route_up_since(before));
 s.state=XZ_EXTRA_ROUTE_READY;route_status_publish(s,2,1);
 uint32_t ready=read_status().ready_revision;assert(xz_extra_decks_route_up_since(before));
 assert(read_status().state==XZ_EXTRA_ROUTE_LOST); /* control bit already dropped, cache not updated yet */
 assert(!xz_extra_decks_route_up_since(ready));route_status_publish(s,3,1);assert(read_status().ready_revision==ready);
 s.state=XZ_EXTRA_ROUTE_LOST;route_status_publish(s,4,0);assert(xz_extra_decks_route_up_since(before));
 route_status_publish(s,5,1);assert(xz_extra_decks_route_up_since(ready));
 route_last.revision=UINT32_MAX;route_status_publish(s,6,0);route_status_publish(s,7,1);
 assert(xz_extra_decks_route_up_since(UINT32_MAX));
 route_status_publish(route_message(XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_NONE,"Check 0."),0,0);
 pthread_t thread;assert(!pthread_create(&thread,NULL,publisher,NULL));unsigned reads=0;
 do {
  s=read_status();unsigned n;assert(sscanf(s.reason,"Check %u.",&n)==1);
  assert(n==s.checked_at_ms&&s.fail_part==(enum xz_extra_route_part)(n%5u));reads++;
 }while(!__atomic_load_n(&publish_done,__ATOMIC_ACQUIRE));
 assert(!pthread_join(thread,NULL));printf("PASS %u consistent concurrent copies; readers perform no IO or mutex calls\n",reads);
}
int main(void) {
 mapping_checks();stale_checks();rescan_checks();publication_checks();
 puts("PASS route status");return 0;
}
