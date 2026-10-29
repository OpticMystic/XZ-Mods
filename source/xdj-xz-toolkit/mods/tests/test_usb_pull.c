/* Source-only USB recovery: actual runtime scanner, queue and wave admission. */
#define _GNU_SOURCE
#ifdef NDEBUG
#error USB recovery requires assertions
#endif
#include "../shell_media.h"
#include "../shell_library.h"
#include <assert.h>
#include <sys/time.h>
static struct xz_shmedia mounted[2];
static unsigned opens, fail_opens;
static uint32_t clock_ms=1000;
static int fixture_time(struct timeval *t,void *unused) {
 (void)unused;t->tv_sec=clock_ms/1000;t->tv_usec=(clock_ms%1000)*1000;return 0;
}
static int fixture_refresh(const struct xz_shmedia_paths *paths,int slot,
 struct xz_shmedia_guard *guard,struct xz_shmedia *out) {
 (void)paths;*out=mounted[slot];guard->bound=*out;return !!out->mount_id;
}
static int fixture_open(const char *path,size_t arena,struct xz_shlib **out) {
 opens++;if(fail_opens){fail_opens--;*out=NULL;return -1;}
 return xz_shlib_open(path,arena,out);
}
#define gettimeofday fixture_time
#define xz_shmedia_refresh fixture_refresh
#define xz_shlib_open fixture_open
#include "../shell_runtime.c"
#undef xz_shlib_open
#undef xz_shmedia_refresh
#undef gettimeofday
void xz_log(const char *message){(void)message;}
void xz_extra_order_release(struct xz_extra_order *order){assert(!order);}
int xz_native_four_mixer_order(void){return 0;}
int main(int argc,char **argv) {
 assert(argc==2);(void)&xz_shmedia_refresh;
 /* Keep identity and export bytes identical across a transient read failure. */
 for(int s=0;s<2;s++){
  mounted[s].mount_id=41+s;mounted[s].root_inode=100+s;mounted[s].attachment_inode=200+s;
  snprintf(mounted[s].database,sizeof mounted[s].database,"%s/PIONEER/rekordbox/export.pdb",argv[1]);
  snprintf(mounted[s].root,sizeof mounted[s].root,"%s",argv[1]);
  snprintf(mounted[s].source,sizeof mounted[s].source,"/dev/sd%c1",'a'+s);
  assert(!stat(mounted[s].database,&mounted[s].file));
 }
 scan_usb();assert(usb[0].lib&&usb[1].lib&&opens==2);
 uint32_t first=usb[0].generation,other=usb[1].generation;
 struct xz_shlib *other_lib=usb[1].lib;
 struct xz_shmedia original=mounted[0];memset(&mounted[0],0,sizeof mounted[0]);
 scan_usb();assert(!usb[0].lib&&!xz_extra_source_generation(1));
 assert(usb[1].lib==other_lib&&usb[1].generation==other);
 struct wave_request other_request={.track_id=101,.device_kind=3,.media=usb[1].media};char other_path[1024];
 assert(wave_expected_path(&other_request,other_path,sizeof other_path)==1);
 other_request.device_kind=1;assert(wave_expected_path(&other_request,other_path,sizeof other_path)<0);
 mounted[0]=original;mounted[0].mount_id++;fail_opens=1;clock_ms+=1000;
 scan_usb();assert(!usb[0].lib&&usb[0].status==USB_ERROR&&opens==3);
 uint32_t reinserted=usb[0].generation;assert(reinserted&&reinserted!=first);
 struct wave_request request={.track_id=101,.device_kind=2,.media=usb[0].media};
 char expected[1024];assert(wave_expected_path(&request,expected,sizeof expected)<0);
 /* Error caching must be bounded, even with identical export timestamps. */
 clock_ms+=1000;scan_usb();assert(opens==3); /* Backoff avoids a retry storm. */
 wave_job[0].request=request;wave_job[0].wanted=wave_job[0].done=9;
 clock_ms+=10000;scan_usb();
 assert(usb[0].lib&&usb[0].status==USB_READY&&"USB_LIBRARY_TRANSIENT_ERROR_STUCK");
 assert(opens==4&&usb[0].generation==reinserted);
 assert(wave_job[0].wanted!=wave_job[0].done&&"RECOVERED_LIBRARY_RESTARTS_EXPIRED_WAVE");
 assert(wave_expected_path(&request,expected,sizeof expected)==0);
 assert(usb[1].lib==other_lib&&usb[1].generation==other);
 puts("PASS reinserted USB retries unchanged export after transient open failure; waveform path recovers; other source retained");
 request.media.generation=first;assert(wave_expected_path(&request,expected,sizeof expected)<0);
 struct xz_shrt_command old={.kind=XZ_SHRT_CMD_LOAD,.deck=0,.track_id=101,.device_kind=2,.media_generation=first};
 queue_command(&old);assert(!queue.count);
 old.media_generation=reinserted;queue_command(&old);assert(queue.count==1);
 scan_usb();assert(opens==4); /* Healthy cached library is not reopened. */
 for(int s=0;s<2;s++){xz_shlib_free(usb[s].lib);usb[s].lib=NULL;analysis_drop(s);xz_shpl_free(&edits[s]);}
 puts("PASS old generation refused, current LOAD admitted and healthy library cache retained");
 return 0;
}
