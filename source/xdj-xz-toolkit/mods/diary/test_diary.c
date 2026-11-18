/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "store.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct fault {int ro,busy,gone,short_write,fail_sync,pull;uint64_t free;unsigned writes,syncs;char root[1024],moved[1024];};
static int space(void *p,int fd,uint64_t *free,int *ro){(void)fd;struct fault *f=p;*free=f->free;*ro=f->ro;return 0;}
static int guard(void *p,const struct xz_diary_store *s){(void)s;struct fault *f=p;return f->gone?-1:f->busy?0:1;}
static ssize_t writing(void *p,int fd,const void *bytes,size_t n){
 struct fault *f=p;f->writes++;assert(n<=XZ_DIARY_BATCH_BYTES);
 if(f->pull){assert(!rename(f->root,f->moved));assert(!mkdir(f->root,0700));}
 if(f->short_write<0){errno=ENOSPC;return -1;}
 return write(fd,bytes,f->short_write?n/2:n);
}
static int syncing(void *p,int fd){struct fault *f=p;f->syncs++;return f->fail_sync?-1:fsync(fd);}
static void init(struct xz_diary_store *s,struct fault *f,const char *root){
 assert(!xz_diary_store_init(s,root,1));memset(f,0,sizeof *f);f->free=UINT64_C(1024)*1024*1024;
 snprintf(f->root,sizeof f->root,"%s",root);snprintf(f->moved,sizeof f->moved,"%s-pulled",root);
 s->context=f;s->space=space;s->guard=guard;s->write_once=writing;s->sync_once=syncing;
}
static struct xz_diary_session session(void){
 struct xz_diary_session s={.have_settings=1,.modern=1,.deck_view=4,.firmware_verified=1,.slots=3,.epoch=1793145600,.version="0.5.0-preview.22.7"};
 for(unsigned i=0;i<16;i++)s.id[i]=(unsigned char)(i+1);
 for(unsigned i=0;i<32;i++)s.settings_hash[i]=(unsigned char)i;
 return s;
}
static unsigned sequence;
static void queue(struct xz_diary_store *s,unsigned ms){
 struct xz_diary_metrics m;xz_diary_metrics_unknown(&m);m.cpu_percent=34;m.free_ram_kib=85432;
 m.underrun_frames[2]=256;m.underrun_frames[3]=0;m.phase_ms[2]=-3;m.phase_ms[3]=7;m.key_max_us[2]=732;m.key_max_us[3]=920;m.usb_read_latency_us=3100;
 char line[XZ_DIARY_LINE_BYTES];int n=xz_diary_metrics_line(line,sizeof line,++sequence,ms,&m);assert(n>0);xz_diary_store_queue(s,line,(size_t)n);
}
static int exists(const char *root,const char *name){char path[1300];snprintf(path,sizeof path,"%s/%s",root,name);struct stat st;return !lstat(path,&st);}
static void root_at(char out[1024],const char *base,const char *name){snprintf(out,1024,"%s/%s",base,name);assert(!mkdir(out,0700));}
static unsigned long long nanos(void){struct timespec t;assert(!clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t));return (unsigned long long)t.tv_sec*1000000000u+t.tv_nsec;}
int main(int argc,char **argv){
 assert(argc==2);assert(!mkdir(argv[1],0700)||errno==EEXIST);char tmp[]="/tmp/xz-diary-XXXXXX";char *base=mkdtemp(tmp);assert(base);
 struct xz_diary_session h=session();struct xz_diary_store s;struct fault f;char root[1024];
 root_at(root,base,"readonly");init(&s,&f,root);f.ro=1;queue(&s,0);assert(xz_diary_store_flush(&s,&h,0)==0);assert(!exists(root,"VJ.Tools"));
 root_at(root,base,"full");init(&s,&f,root);f.free=XZ_DIARY_MIN_FREE;queue(&s,0);assert(!xz_diary_store_flush(&s,&h,0));assert(!exists(root,"VJ.Tools"));
 root_at(root,base,"busy");init(&s,&f,root);f.busy=1;for(unsigned i=0;i<10000;i++)queue(&s,i);
 assert(s.used<=XZ_DIARY_BATCH_BYTES&&s.dropped>0);assert(memmem(s.pending,s.used,"\"monotonic_ms\":9999",19));
 assert(!xz_diary_store_flush(&s,&h,0));assert(!exists(root,"VJ.Tools"));
 f.busy=0;assert(xz_diary_store_flush(&s,&h,30000)==1);unsigned calls=f.writes;queue(&s,30001);assert(!xz_diary_store_flush(&s,&h,30001));assert(calls==f.writes);
 root_at(root,base,"symlink");init(&s,&f,root);char linked[1200];snprintf(linked,sizeof linked,"%s/VJ.Tools",root);assert(!symlink(base,linked));queue(&s,0);
 assert(xz_diary_store_flush(&s,&h,0)==-1);assert(!exists(base,"XZ-Mods-diary"));
 root_at(root,base,"removed");init(&s,&f,root);queue(&s,0);f.gone=1;assert(xz_diary_store_flush(&s,&h,0)==-1);assert(!exists(root,"VJ.Tools"));
 root_at(root,base,"pulled");init(&s,&f,root);queue(&s,0);f.pull=1;assert(xz_diary_store_flush(&s,&h,0)==-1);assert(f.writes==1&&f.syncs==0);assert(!exists(root,"VJ.Tools"));
 for(unsigned kind=0;kind<3;kind++){
  char name[32];snprintf(name,sizeof name,"fault%u",kind);root_at(root,base,name);init(&s,&f,root);queue(&s,0);
  f.short_write=kind==0?1:kind==1?-1:0;f.fail_sync=kind==2;
  assert(xz_diary_store_flush(&s,&h,0)==-1);unsigned count=f.writes;
  assert(xz_diary_store_flush(&s,&h,60000)==-1&&f.writes==count);
 }
 /* Actual complete output from two independent simulated sticks. */
 for(unsigned usb=0;usb<2;usb++){
  char name[32];snprintf(name,sizeof name,"usb%u",usb+1);root_at(root,argv[1],name);init(&s,&f,root);
  struct xz_diary_event e={XZ_DIARY_SYNC,XZ_DIARY_FOLLOWING,XZ_DIARY_CLOCK_LOST,0,3,0,0};char event_line[XZ_DIARY_LINE_BYTES];
  int event_bytes=xz_diary_event_line(event_line,sizeof event_line,++sequence,0,&e);assert(event_bytes>0);xz_diary_store_queue(&s,event_line,(size_t)event_bytes);
  queue(&s,0);assert(xz_diary_store_flush(&s,&h,0)==1);
  for(unsigned t=10000;t<=120000;t+=10000){queue(&s,t);xz_diary_store_flush(&s,&h,t);}
  assert(s.writes==5&&s.syncs==6);printf("two-stick usb%u bytes=%llu writes=%u fsyncs=%u coverage_ms=120000\n",usb+1,(unsigned long long)s.written,s.writes,s.syncs);
 }
 root_at(root,base,"late-full");init(&s,&f,root);queue(&s,0);assert(xz_diary_store_flush(&s,&h,0)==1);
 unsigned before_bytes=s.bytes;f.free=0;queue(&s,30000);assert(!xz_diary_store_flush(&s,&h,30000));assert(s.bytes==before_bytes&&s.writes==1);
 /* Rotation must remove the lowest generation, even after a clock rollback. */
 root_at(root,base,"ring");init(&s,&f,root);
 unsigned long long ring_start=nanos();
 for(unsigned batch=0;batch<700;batch++){
  while(s.used<XZ_DIARY_BATCH_BYTES-XZ_DIARY_LINE_BYTES)queue(&s,batch*30000);
  assert(xz_diary_store_flush(&s,&h,batch*30000)==1);
 }
 double ring_cpu_us=(double)(nanos()-ring_start)/700/1000;
 assert(ring_cpu_us<5000);assert(s.writes==700&&s.syncs<=s.writes+10);
 assert(s.generation>8);unsigned sum=0;char path[1300];struct stat st;
 for(unsigned i=0;i<8;i++){snprintf(path,sizeof path,"%s/VJ.Tools/XZ-Mods-diary/segment-%02u.jsonl",root,i);assert(!stat(path,&st));assert(st.st_size<=XZ_DIARY_SEGMENT_BYTES);sum+=(unsigned)st.st_size;}
 assert(sum<=XZ_DIARY_SEGMENTS*XZ_DIARY_SEGMENT_BYTES);printf("ring bytes=%u cap=%u generations=%u writes=%u cpu_us_per_batch=%.3f\n",sum,XZ_DIARY_SEGMENTS*XZ_DIARY_SEGMENT_BYTES,s.generation,s.writes,ring_cpu_us);
 /* Preserve the exact tail of the most recent session, including a torn line. */
 snprintf(path,sizeof path,"%s/VJ.Tools/XZ-Mods-diary/segment-%02u.jsonl",root,s.segment);
 int fd=open(path,O_WRONLY|O_APPEND);assert(fd>=0);assert(write(fd,"{\"crash_tail\":",14)==14);close(fd);assert(!stat(path,&st));off_t old_size=st.st_size;ino_t old_inode=st.st_ino;
 init(&s,&f,root);h.id[0]++;queue(&s,0);assert(xz_diary_store_flush(&s,&h,0)==1);
 assert(!stat(path,&st)&&st.st_size==old_size&&st.st_ino==old_inode);
 fd=open(path,O_RDONLY);assert(fd>=0);char tail[15]={0};assert(pread(fd,tail,14,st.st_size-14)==14);close(fd);assert(!strcmp(tail,"{\"crash_tail\":"));
 /* No free-text input can enter any JSON output. */
 const char *canary="TITLE /music/private.wav SERIAL-SECRET 192.168.1.8";char line[XZ_DIARY_LINE_BYTES];
 h.version=canary;assert(xz_diary_header(line,sizeof line,&h,20)>0);assert(!strstr(line,"TITLE")&&!strstr(line,"SERIAL")&&!strstr(line,"/music"));
 assert(xz_diary_notice_code(canary)==XZ_DIARY_NOTICE_OTHER);assert(xz_diary_error_code(canary)==0);
 struct xz_diary_event e={.component=999};assert(xz_diary_event_line(line,sizeof line,1,0,&e)==-1);
 struct xz_diary_metrics m;xz_diary_metrics_unknown(&m);m.cpu_percent=10000;m.phase_ms[0]=INT32_MAX;m.key_max_us[3]=INT32_MAX;
 assert(xz_diary_metrics_line(line,sizeof line,1,0,&m)>0);assert(strstr(line,"\"cpu_percent\":null"));assert(!strstr(line,"2147483647"));
 unsigned long long start=nanos();unsigned loops=100000;for(unsigned i=0;i<loops;i++)assert(xz_diary_metrics_line(line,sizeof line,i,i,&m)>0);
 double cost=(double)(nanos()-start)/loops/1000.0;printf("format cost_us=%.3f loops=%u sizeof_store=%zu batch_cap=%u\n",cost,loops,sizeof s,XZ_DIARY_BATCH_BYTES);assert(cost<100.0);
 puts("PASS diary storage, faults, rotation, crash tail, sanitizer and cost bounds");return 0;
}
