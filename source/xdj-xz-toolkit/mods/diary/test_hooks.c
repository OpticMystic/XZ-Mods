/* SPDX-License-Identifier: MIT */
#include "runtime.c"
#include <assert.h>

/* No mount discovery or runtime thread is run by this fixture. */
unsigned long xz_settings_mount_id(const char *path){(void)path;return 7;}
int xz_settings_format(const struct xz_settings *s,char *out,size_t n){(void)s;return snprintf(out,n,"XZ_MODS_SETTINGS 1\ninterface=1\n");}
int main(void){
 assert(!setenv("XZ_MODS_SETTINGS_READONLY","1",1));assert(!xz_diary_start());
 assert(!created&&!running&&!xz_mods_diary_proof_v1.polls);
 xz_diary_busy_begin(3);xz_diary_busy_begin(1);assert(busy[0]==2&&busy[1]==1);
 struct mount m={.slot=1,.disk_safe=1};m.store.mount_id=7;running=1;
 assert(!safe(&m,&m.store));xz_diary_busy_end(1);assert(!safe(&m,&m.store));xz_diary_busy_end(3);assert(safe(&m,&m.store)==1);
 m.disk_safe=0;assert(!safe(&m,&m.store));m.store.mount_id=8;assert(safe(&m,&m.store)==-1);
 xz_diary_notice(XZ_DIARY_UI,XZ_DIARY_NOTICE_KEY_BUSY);xz_diary_notice(XZ_DIARY_UI,XZ_DIARY_NOTICE_KEY_BUSY);
 assert(notices[XZ_DIARY_UI]==1u<<XZ_DIARY_NOTICE_KEY_BUSY);
 xz_diary_notice(1000,1000);xz_diary_audio(2,UINT64_MAX,UINT_MAX,1);
 assert(audio_frames[2]==INT_MAX&&audio_key[2]==1000000&&audio_valid[2]==3);
 uint64_t start=micros(CLOCK_THREAD_CPUTIME_ID);unsigned count=1000000;
 for(unsigned i=0;i<count;i++){xz_diary_busy_begin(3);xz_diary_busy_end(3);}
 uint64_t cost=micros(CLOCK_THREAD_CPUTIME_ID)-start;
 assert(!busy[0]&&!busy[1]);assert(cost<count);
 printf("PASS readonly start, nested activity, mount identity, strict notices; busy_pair_ns=%.2f\n",(double)cost*1000/count);
 printf("worker static_bytes=%zu stack_bytes=131072 mount_slots=%u\n",sizeof mounts+sizeof session+sizeof previous,(unsigned)MOUNTS);
 return 0;
}
