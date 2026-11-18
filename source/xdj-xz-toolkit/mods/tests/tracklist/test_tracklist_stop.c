/* SPDX-License-Identifier: MIT */
/* USB STOP and the recording tracklist: the real sampler and writer threads (tracklist_runtime.c, included to reach
   its statics), the real holders' board and its wait (usb_release.c, the part that runs on a PC), a fake recorder,
   fake DECK 3/4 and a folder standing in for the stick. Titles are made up.

   A  stop during a recording: the list (header, both tracks, footer naming the stop) and its CUE sheet are on the
      "stick" when the answer comes; one answer; the sampler acted at once, not at its next sample; the writer ran at
      nice 0 and best-effort I/O and went back to 19 and idle; the recorder going idle adds nothing; a stop of the
      other stick is not answered.
   B  the recording ended first and its list waits for a busy stick (the quiet-stick wait): the stop is asked for the
      queued list, the wait ends at once and the list and CUE are on the stick at the answer, inside the 1000 ms
      answer window (the wait alone would take 2000 ms).
   C  the stick comes back: the next recording gets its own list at its normal end; nothing is asked any more.
   D  the stick's folder is gone when the writer gets there: one answer all the same, the failure counted and logged.
   E  RECORDING TRACKLIST off: nothing is asked and nothing is written.
   The test stops at the end of the first phase that failed.

   Usage: test-tracklist-stop <folder> (made if missing; the test writes only below it). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/resource.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include "tracklist.h"
static int test_setpriority(int which,id_t who,int prio);
static int test_stick_stopped(struct xz_tl *t,const char *mount,uint32_t now);
static void test_config(struct xz_tl_config *c);
static FILE *test_fopen(const char *path,const char *mode);
#define setpriority test_setpriority
#define xz_tl_stick_stopped test_stick_stopped
#define fopen test_fopen
#define XZ_TL_TEST_CONFIG(c) test_config(c)
/* The sampler's wait. A stop must wake the sampler (xz_wake_set), not wait for its next sample up to 500 ms on. On a
   loaded machine timing alone cannot tell the two apart, so the test watches the wait itself: it asks only while the
   sampler is in its wait with time left in it, and records whether a watched wait ran out instead of being woken. */
#include "xz_wake.h"
static struct xz_wake *watched_wake;                 /* &sampler_wake, set in main once the runtime is in */
static int watched_in_wait;
static uint64_t watched_wait_until_us,watched_ran_out_us,ran_out_at_stop_us;
static uint64_t test_now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static void test_wake_wait(struct xz_wake *w,uint32_t ticket,unsigned ms){
 int watched=w==__atomic_load_n(&watched_wake,__ATOMIC_ACQUIRE);
 if(watched){__atomic_store_n(&watched_wait_until_us,test_now_us()+(uint64_t)ms*1000u,__ATOMIC_RELEASE);__atomic_store_n(&watched_in_wait,1,__ATOMIC_RELEASE);}
 xz_wake_wait(w,ticket,ms);
 if(watched){
  __atomic_store_n(&watched_in_wait,0,__ATOMIC_RELEASE);
  if(xz_wake_ticket(w)==ticket)__atomic_store_n(&watched_ran_out_us,test_now_us(),__ATOMIC_RELEASE);
 }
}
#define xz_wake_wait test_wake_wait
#include "tracklist_runtime.c"
#undef xz_wake_wait
#undef setpriority
#undef xz_tl_stick_stopped
#undef fopen
#ifndef XZ_USB_RELEASE_NO_RUNTIME
#define XZ_USB_RELEASE_NO_RUNTIME
#endif
#include "usb_release.h"
#include <dirent.h>
#include <stdio.h>

static int failures,checks;
#define CHECK(c) do{checks++;if(!(c)){failures++;fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);}}while(0)

/* ---- the player around the tracklist ---- */
static pthread_mutex_t fake=PTHREAD_MUTEX_INITIALIZER;
static struct xz_recorder_state fake_rec;static uint64_t rec_began_us;static unsigned rec_reads;
static struct xz_extra_decks fake_snap;
static char logs[32][320];static unsigned log_n;
void xz_log(const char *m){
 pthread_mutex_lock(&fake);if(log_n<32)snprintf(logs[log_n++],sizeof logs[0],"%s",m);pthread_mutex_unlock(&fake);
 printf("  log: %s\n",m);fflush(stdout);
}
int xz_read_memory(uint32_t a,void *out,size_t n){(void)a;(void)out;(void)n;return -1;}   /* no ON AIR, no DECK 1/2 */
int xz_recorder_state(struct xz_recorder_state *out){
 pthread_mutex_lock(&fake);
 *out=fake_rec;if(out->recording)out->elapsed_ms=(uint32_t)((micros(CLOCK_MONOTONIC)-rec_began_us)/1000u);
 rec_reads++;
 pthread_mutex_unlock(&fake);
 return out->known?0:-1;
}
int xz_extra_decks_snapshot(struct xz_extra_decks *out){pthread_mutex_lock(&fake);*out=fake_snap;pthread_mutex_unlock(&fake);return 1;}
static void test_config(struct xz_tl_config *c){c->heard_ms=1500u;c->heard_ms_no_air=1500u;c->tail_ms=500u;}

/* The nice last asked for the writer thread, through the runtime's own calls. */
static int writer_nice=-100;
static int test_setpriority(int which,id_t who,int prio){
 long target=who?(long)who:(long)syscall(SYS_gettid);
 if(target==__atomic_load_n(&writer_tid,__ATOMIC_ACQUIRE))__atomic_store_n(&writer_nice,prio,__ATOMIC_RELEASE);
 return setpriority(which,who,prio);
}
static uint64_t stopped_us;   /* when the sampler ended the list */
static int test_stick_stopped(struct xz_tl *t,const char *mount,uint32_t now){
 /* The sampler thread, ending the list: when its wait last ran out without a wake (later waits do not count). */
 __atomic_store_n(&ran_out_at_stop_us,__atomic_load_n(&watched_ran_out_us,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
 __atomic_store_n(&stopped_us,micros(CLOCK_MONOTONIC),__ATOMIC_RELEASE);
 return xz_tl_stick_stopped(t,mount,now);
}
/* The stick's block statistics, which the writer reads to wait for a quiet stick: requests in flight while stick_busy
   is set, none otherwise. Never the host's own disks: their activity (or, under qemu, a device number that lands on
   one of them) would make the normal path wait up to 2 s at random. */
static int stick_busy;static unsigned busy_polls;static char busy_stat[600],idle_stat[600];
static FILE *test_fopen(const char *path,const char *mode){
 if(strncmp(path,"/sys/dev/block/",15))return fopen(path,mode);
 if(!__atomic_load_n(&stick_busy,__ATOMIC_ACQUIRE))return fopen(idle_stat,mode);
 __atomic_add_fetch(&busy_polls,1u,__ATOMIC_ACQ_REL);return fopen(busy_stat,mode);
}
static int io_class_now(long tid){
#ifdef SYS_ioprio_get
 long v=syscall(SYS_ioprio_get,1,(int)tid);
 return v<0?-1:(int)((unsigned long)v>>13);
#else
 (void)tid;return -1;
#endif
}

/* ---- the holders' board, as usb_release.c keeps it in the player ---- */
static struct xz_ur_board board;static pthread_mutex_t board_lock=PTHREAD_MUTEX_INITIALIZER;static int holders_added;
int xz_usb_release_holder(const char *name,xz_ur_ask ask,void *ctx){
 pthread_mutex_lock(&board_lock);int id=xz_ur_board_add(&board,name,ask,ctx);holders_added++;pthread_mutex_unlock(&board_lock);
 return id;
}
static char *slurp(const char *path){
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 char *s=NULL;size_t n=0,cap=0;int c;
 while((c=fgetc(f))!=EOF){if(n+2>cap){cap=cap?cap*2:4096;s=realloc(s,cap);}s[n++]=(char)c;}
 fclose(f);if(!s)s=calloc(1,1);else s[n]=0;return s;
}
/* What the stick held when the answer came, on the writer's thread. */
static struct { int count, slot, nice, io; uint32_t stop; uint64_t at_us; char *txt, *cue; } seen;
static char want_txt[700],want_cue[700];
void xz_usb_release_answer(int holder,int slot,uint32_t stop){
 char txt_path[700],cue_path[700];
 pthread_mutex_lock(&fake);snprintf(txt_path,sizeof txt_path,"%s",want_txt);snprintf(cue_path,sizeof cue_path,"%s",want_cue);pthread_mutex_unlock(&fake);
 char *txt=slurp(txt_path),*cue=slurp(cue_path);
 int io=io_class_now(0),nice=__atomic_load_n(&writer_nice,__ATOMIC_ACQUIRE);
 pthread_mutex_lock(&fake);
 seen.count++;seen.slot=slot;seen.stop=stop;seen.at_us=micros(CLOCK_MONOTONIC);seen.nice=nice;seen.io=io;
 free(seen.txt);free(seen.cue);seen.txt=txt;seen.cue=cue;
 pthread_mutex_unlock(&fake);
 xz_ur_board_answer(&board,holder,slot,stop);
}
static uint32_t ask(int slot,unsigned *asked,uint64_t *at_us){
 pthread_mutex_lock(&board_lock);
 *at_us=micros(CLOCK_MONOTONIC);
 uint32_t stop=xz_ur_board_ask(&board,slot,(uint32_t)(*at_us/1000u));
 *asked=stop_holder>=0?board.slot[slot-1].asked[stop_holder]:0;
 pthread_mutex_unlock(&board_lock);
 return stop;
}
static char settle_line[400];
static uint32_t env_now(void *c){(void)c;return (uint32_t)(micros(CLOCK_MONOTONIC)/1000u);}
static void sleep_ms(unsigned ms){struct timespec d={(time_t)(ms/1000u),(long)(ms%1000u)*1000000L};while(nanosleep(&d,&d)&&errno==EINTR){}}
static void env_sleep(void *c,unsigned ms){(void)c;sleep_ms(ms);}
static void env_log(void *c,const char *l){(void)c;snprintf(settle_line,sizeof settle_line,"%s",l);}
/* The unmount's wait for the answers (usb_release.c): the holders that did not answer within 1000 ms. */
static int settle(int slot){
 struct xz_ur_env e;memset(&e,0,sizeof e);e.now_ms=env_now;e.sleep_ms=env_sleep;e.log=env_log;
 settle_line[0]=0;int left=xz_ur_board_settle(&board,&e,slot);
 if(settle_line[0])printf("  board: %s\n",settle_line);
 return left;
}

/* ---- the stick and the set ---- */
static char root[400],stick[440],dir1[520];
static void path_in(char *out,size_t cap,const char *name){snprintf(out,cap,"%s/%s",dir1,name);}
static int exists(const char *name){char p[700];path_in(p,sizeof p,name);return !access(p,F_OK);}
static void touch(const char *name){char p[700];path_in(p,sizeof p,name);FILE *f=fopen(p,"wb");if(f){fputs("RIFF",f);fclose(f);}}
static void empty_dir(void){
 DIR *d=opendir(dir1);if(!d)return;struct dirent *e;char p[1100];
 while((e=readdir(d)))if(e->d_name[0]!='.'){snprintf(p,sizeof p,"%s/%s",dir1,e->d_name);unlink(p);}
 closedir(d);
}
static void expect(const char *base){
 pthread_mutex_lock(&fake);
 snprintf(want_txt,sizeof want_txt,"%s/%s.TXT",dir1,base);snprintf(want_cue,sizeof want_cue,"%s/%s.CUE",dir1,base);
 pthread_mutex_unlock(&fake);
}
static void start_recording(uint32_t generation,const char *name){
 touch("RECTMP.WAV");                                 /* stock records under this name until the file closes */
 pthread_mutex_lock(&fake);
 fake_rec.known=1;fake_rec.recording=1;fake_rec.generation=generation;fake_rec.elapsed_ms=0;
 snprintf(fake_rec.file,sizeof fake_rec.file,"%s/%s",dir1,name);rec_began_us=micros(CLOCK_MONOTONIC);
 pthread_mutex_unlock(&fake);
}
static void end_recording(const char *name){
 pthread_mutex_lock(&fake);fake_rec.recording=0;fake_rec.elapsed_ms=0;fake_rec.file[0]=0;pthread_mutex_unlock(&fake);
 char from[700],to[700];path_in(from,sizeof from,"RECTMP.WAV");path_in(to,sizeof to,name);(void)rename(from,to);
}
static void deck(int i,uint32_t id,const char *artist,const char *title){
 pthread_mutex_lock(&fake);
 struct xz_extra_deck *x=&fake_snap.deck[i];memset(x,0,sizeof *x);
 x->loaded=1;x->source=1;x->track_id=id;x->jog_content_epoch=id;x->playing=1;
 x->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_ARTIST)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING);
 snprintf(x->title,sizeof x->title,"%s",title);snprintf(x->artist,sizeof x->artist,"%s",artist);
 pthread_mutex_unlock(&fake);
}
/* Returns after the sampler read the recorder and went into its wait for the next sample with at least 300 ms of it
   left: a stop asked now can only end that wait by waking it. */
static void after_sample(void){
 pthread_mutex_lock(&fake);unsigned n=rec_reads;pthread_mutex_unlock(&fake);
 int sampled=0;
 for(int i=0;i<8000;i++){
  if(!sampled){pthread_mutex_lock(&fake);sampled=rec_reads!=n;pthread_mutex_unlock(&fake);}
  if(sampled&&__atomic_load_n(&watched_in_wait,__ATOMIC_ACQUIRE)&&
     __atomic_load_n(&watched_wait_until_us,__ATOMIC_ACQUIRE)>test_now_us()+300000u)return;
  struct timespec d={0,500000L};nanosleep(&d,NULL);
 }
}
static int answers(void){pthread_mutex_lock(&fake);int n=seen.count;pthread_mutex_unlock(&fake);return n;}
static unsigned logs_with(const char *text){
 unsigned n=0;pthread_mutex_lock(&fake);for(unsigned i=0;i<log_n;i++)n+=strstr(logs[i],text)!=NULL;pthread_mutex_unlock(&fake);return n;
}
static int ends_with(const char *s,const char *tail){size_t a=strlen(s),b=strlen(tail);return a>=b&&!strcmp(s+a-b,tail);}

static void phase_stop(void){
 printf("A: USB STOP while recording REC001.WAV on USB 1\n");
 deck(0,11,"Test Artist A","Synthetic One");
 start_recording(1,"REC001.WAV");
 sleep_ms(2600);                                      /* DECK 3 heard past 1.5 s: listed */
 deck(1,12,"Test Artist B","Synthetic Two");
 sleep_ms(1400);                                      /* DECK 4 heard 0.5 to 1 s: in by the 0.5 s tail rule */
 expect("REC001");
 unsigned asked;uint64_t at;
 CHECK(ask(2,&asked,&at)&&asked==0);                  /* the other stick: nothing to do there */
 CHECK(settle(2)==0&&answers()==0);
 after_sample();
 uint32_t stop=ask(1,&asked,&at);
 CHECK(stop&&asked==1);
 CHECK(settle(1)==0);
 pthread_mutex_lock(&fake);
 int count=seen.count,slot=seen.slot,nice=seen.nice,io=seen.io;uint32_t got=seen.stop;uint64_t answered=seen.at_us;
 char *txt=seen.txt?strdup(seen.txt):NULL,*cue=seen.cue?strdup(seen.cue):NULL;
 pthread_mutex_unlock(&fake);
 uint64_t acted=__atomic_load_n(&stopped_us,__ATOMIC_ACQUIRE);
 printf("  the sampler ended the list %.1f ms after the ask, made just after a sample; the answer came %.1f ms after the ask\n",
        acted>=at?(double)(acted-at)/1000.0:-1.0,answered>=at?(double)(answered-at)/1000.0:-1.0);
 CHECK(count==1&&slot==1&&got==stop);
 CHECK(acted>=at&&acted-at<100000u);                  /* woken at once, not at the next sample 500 ms on */
 /* The same, whatever the load: the wait the stop was asked in ended by the stop's wake, not by running out. */
 CHECK(__atomic_load_n(&ran_out_at_stop_us,__ATOMIC_ACQUIRE)<at);
 CHECK(answered>=at&&answered-at<(uint64_t)XZ_UR_ANSWER_MS*1000u);
 CHECK(txt&&!strncmp(txt,"REC001.WAV tracklist (XZ Mods)\r\n",32));
 CHECK(txt&&strstr(txt,"  Test Artist A - Synthetic One")&&strstr(txt,"  Test Artist B - Synthetic Two"));
 CHECK(txt&&strstr(txt,"\r\nThe recording ended at about 00:00:0")&&ends_with(txt," because its USB stick was stopped (USB STOP).\r\n"));
 CHECK(cue&&strstr(cue,"FILE \"REC001.WAV\" WAVE")&&strstr(cue,"TRACK 02 AUDIO")&&!strstr(cue,"TRACK 03"));
 CHECK(nice==0);                                      /* asked for nice 0 while writing */
 if(io>=0)CHECK(io==2);else printf("  (ioprio_get is not available here: the I/O class is not checked)\n");
 if(txt)printf("  REC001.TXT at the answer:\n%s",txt);
 /* Stock ends the recording; the sampler sees the recorder idle: no second list, no second footer. */
 end_recording("REC001.WAV");
 sleep_ms(1600);
 char *later=slurp(want_txt);
 CHECK(later&&txt&&!strcmp(later,txt));
 CHECK(answers()==1);
 CHECK(logs_with("USB_RELEASE: tracklist, USB 1: the list was written before the unmount")==1);
 CHECK(__atomic_load_n(&writer_nice,__ATOMIC_ACQUIRE)==19);   /* back to the usual priority */
 int after=io_class_now(__atomic_load_n(&writer_tid,__ATOMIC_ACQUIRE));
 if(after>=0)CHECK(after==3);
 free(later);free(txt);free(cue);
}
static void phase_queued(void){
 printf("B: REC005.WAV ended first; its list waits for a busy stick when USB STOP comes\n");
 start_recording(5,"REC005.WAV");sleep_ms(2600);
 expect("REC005");
 __atomic_store_n(&stick_busy,1,__ATOMIC_RELEASE);
 unsigned polls=__atomic_load_n(&busy_polls,__ATOMIC_ACQUIRE);
 end_recording("REC005.WAV");
 for(int i=0;i<3000&&__atomic_load_n(&busy_polls,__ATOMIC_ACQUIRE)==polls;i++)sleep_ms(1);
 CHECK(__atomic_load_n(&busy_polls,__ATOMIC_ACQUIRE)!=polls);   /* the writer waits for the stick to be quiet */
 CHECK(!exists("REC005.TXT"));
 unsigned asked;uint64_t at;
 uint32_t stop=ask(1,&asked,&at);
 CHECK(stop&&asked==1);                               /* no open list, but one queued on this stick */
 CHECK(settle(1)==0);
 pthread_mutex_lock(&fake);
 int count=seen.count;uint64_t answered=seen.at_us;
 char *txt=seen.txt?strdup(seen.txt):NULL;int cue=seen.cue!=NULL;
 pthread_mutex_unlock(&fake);
 __atomic_store_n(&stick_busy,0,__ATOMIC_RELEASE);
 printf("  the answer came %.1f ms after the ask (the quiet-stick wait would have taken up to 2000 ms)\n",
        answered>=at?(double)(answered-at)/1000.0:-1.0);
 CHECK(count==2&&answered>=at&&answered-at<(uint64_t)XZ_UR_ANSWER_MS*1000u);   /* never the 2 s quiet-stick wait */
 CHECK(txt&&!strncmp(txt,"REC005.WAV tracklist (XZ Mods)\r\n",32)&&strstr(txt,"\r\nEnd of recording at 00:00:0")&&!strstr(txt,"USB STOP"));
 CHECK(cue);
 sleep_ms(600);
 CHECK(answers()==2&&logs_with("USB_RELEASE: tracklist, USB 1: the list was written before the unmount")==2);
 free(txt);
}
static void phase_reinserted(void){
 printf("C: the stick back, REC002.WAV ends with the record key\n");
 start_recording(2,"REC002.WAV");sleep_ms(2600);end_recording("REC002.WAV");
 /* The normal path: the next sample, then up to 2 s of waiting for a quiet stick, then the writes. */
 for(int i=0;i<100&&!exists("REC002.CUE");i++)sleep_ms(50);
 sleep_ms(300);
 char p[700];path_in(p,sizeof p,"REC002.TXT");char *txt=slurp(p);
 CHECK(txt&&!strncmp(txt,"REC002.WAV tracklist (XZ Mods)\r\n",32)&&strstr(txt,"\r\nEnd of recording at 00:00:0")&&!strstr(txt,"USB STOP"));
 CHECK(exists("REC002.CUE"));
 unsigned asked;uint64_t at;
 CHECK(ask(1,&asked,&at)&&asked==0&&settle(1)==0);
 CHECK(answers()==2);
 free(txt);
}
static void phase_failed(void){
 printf("D: the stick's folder is gone when the writer gets there\n");
 start_recording(3,"REC003.WAV");sleep_ms(2600);
 expect("REC003");
 empty_dir();CHECK(!rmdir(dir1));
 after_sample();
 unsigned asked;uint64_t at;
 uint32_t stop=ask(1,&asked,&at);
 CHECK(stop&&asked==1&&settle(1)==0);
 pthread_mutex_lock(&fake);int count=seen.count;uint32_t got=seen.stop;int txt=seen.txt!=NULL;pthread_mutex_unlock(&fake);
 CHECK(count==3&&got==stop&&!txt);
 end_recording("REC003.WAV");sleep_ms(1600);
 CHECK(answers()==3);
 CHECK(logs_with("USB_RELEASE: tracklist, USB 1: the list could not be written (errno 2,")==1);
 CHECK(!mkdir(dir1,0755));
}
static void phase_off(void){
 printf("E: RECORDING TRACKLIST off\n");
 start_recording(4,"REC004.WAV");sleep_ms(2600);
 xz_tracklist_set_enabled(0);sleep_ms(1100);
 unsigned asked;uint64_t at;
 CHECK(ask(1,&asked,&at)&&asked==0&&settle(1)==0);
 sleep_ms(700);
 CHECK(!exists("REC004.TXT")&&!exists("REC004.CUE")&&answers()==3);
}

int main(int argc,char **argv){
 if(argc<2){fprintf(stderr,"usage: %s <folder>\n",argv[0]);return 2;}
 __atomic_store_n(&watched_wake,&sampler_wake,__ATOMIC_RELEASE);
 setvbuf(stdout,NULL,_IOLBF,0);
 snprintf(root,sizeof root,"%s",argv[1]);mkdir(root,0755);
 char media[420];snprintf(media,sizeof media,"%s/media",root);mkdir(media,0755);
 snprintf(stick,sizeof stick,"%s/usb",media);
 char slot[460];snprintf(slot,sizeof slot,"%s1",stick);mkdir(slot,0755);
 snprintf(slot,sizeof slot,"%s2",stick);mkdir(slot,0755);
 snprintf(dir1,sizeof dir1,"%s1/PIONEER REC",stick);mkdir(dir1,0755);empty_dir();
 stick_mount=stick;
 unsetenv("XZ_MODS_TRACKLIST");unsetenv("XZ_MODS_OBSERVER");unsetenv("XZ_MODS_SETTINGS_READONLY");
 pthread_mutex_lock(&fake);fake_rec.known=1;pthread_mutex_unlock(&fake);
 xz_tracklist_set_enabled(1);
 CHECK(xz_tracklist_start()==0&&created&&holders_added==1&&stop_holder==0);
 snprintf(busy_stat,sizeof busy_stat,"%s/busy-stat",root);
 {FILE *f=fopen(busy_stat,"w");if(f){fputs("   12 0 96 4 0 0 0 0 3 40 44\n",f);fclose(f);}}   /* field 9: 3 in flight */
 snprintf(idle_stat,sizeof idle_stat,"%s/idle-stat",root);
 {FILE *f=fopen(idle_stat,"w");if(f){fputs("   12 0 96 4 0 0 0 0 0 40 44\n",f);fclose(f);}}
 void (*const phases[])(void)={phase_stop,phase_queued,phase_reinserted,phase_failed,phase_off};
 for(unsigned i=0;i<sizeof phases/sizeof phases[0];i++){
  phases[i]();
  if(failures){printf("FAIL in phase %c: %d checks, %d failures\n",'A'+(int)i,checks,failures);fflush(stdout);_exit(1);}
 }
 xz_tracklist_stop();
 char json[4096];CHECK(xz_tracklist_status_json(json,sizeof json)>0);
 printf("  status: %s\n",json);
 /* Eight operations in all: REC001 and REC005 TXT+CUE at the stops, REC002 TXT+CUE at its end, REC003 TXT failed and
    CUE skipped. REC005's TXT was already on its way when the stop came; its CUE went out on the stop's path. */
 CHECK(strstr(json,"\"ops\":8,")&&strstr(json,"\"failures\":1,")&&strstr(json,"\"skipped\":1,")&&strstr(json,"\"last_errno\":2,"));
 /* Only the three files written outside a stop were synced (file and folder each): a stop's write leaves that to
    the unmount. */
 CHECK(strstr(json,"\"writes\":6,")&&strstr(json,"\"syncs\":6,"));
 CHECK(strstr(json,"\"stop_asks\":3,")&&strstr(json,"\"stop_lists\":2,")&&strstr(json,"\"stop_answers\":3,")&&
       strstr(json,"\"stop_files\":3,")&&strstr(json,"\"stop_failures\":2,"));
 free(seen.txt);free(seen.cue);
 printf("%s: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
