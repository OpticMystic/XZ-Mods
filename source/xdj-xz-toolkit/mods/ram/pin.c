/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "pin.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
extern int xz_ram_assets_reserve(uint64_t) __attribute__((weak));
extern void xz_ram_assets_publish(uint64_t) __attribute__((weak));
extern void xz_ram_assets_cancel(uint64_t) __attribute__((weak));
extern void xz_ram_assets_release(uint64_t) __attribute__((weak));
struct stage {int fd,backup,target,flags;off_t position;struct stat identity;struct xz_ram_file_pin pin;};
void xz_ram_file_unpin(struct xz_ram_file_pin *p){
    if(!p||!p->mapping)return;
    munlock(p->mapping,p->bytes);munmap(p->mapping,p->bytes);
    if(xz_ram_assets_release)xz_ram_assets_release(p->bytes);memset(p,0,sizeof(*p));
}
int xz_ram_files_pin(FILE *const files[],struct xz_ram_file_pin pins[],size_t count){
    if(!files||!pins||!count||count>32||!xz_ram_assets_reserve||!xz_ram_assets_publish||!xz_ram_assets_cancel||!xz_ram_assets_release)return 0;
    struct stage *s=calloc(count,sizeof(*s));if(!s)return 0;
    for(size_t i=0;i<count;i++)s[i].fd=s[i].backup=-1;
    uint64_t total=0;int reserved=0,ok=0;size_t committed=0;
    for(size_t i=0;i<count;i++){
        if(!files[i]||pins[i].mapping)goto done;
        s[i].target=fileno(files[i]);s[i].flags=fcntl(s[i].target,F_GETFD);s[i].position=lseek(s[i].target,0,SEEK_CUR);
        if(s[i].flags<0||s[i].position<0||fstat(s[i].target,&s[i].identity)||!S_ISREG(s[i].identity.st_mode)||s[i].identity.st_size<=0||
           (uint64_t)s[i].identity.st_size>512u*1024u*1024u-total)goto done;
        s[i].pin.bytes=(size_t)s[i].identity.st_size;total+=s[i].pin.bytes;
    }
    if(!xz_ram_assets_reserve(total))goto done;reserved=1;
    struct rlimit limit={RLIM_INFINITY,RLIM_INFINITY};(void)setrlimit(RLIMIT_MEMLOCK,&limit);
    for(size_t i=0;i<count;i++){
        char name[]="/dev/shm/xz-ram-stems-XXXXXX";s[i].fd=mkstemp(name);if(s[i].fd<0)goto done;unlink(name);
        if(fcntl(s[i].fd,F_SETFD,FD_CLOEXEC)||posix_fallocate(s[i].fd,0,(off_t)s[i].pin.bytes))goto done;
        void *map=mmap(NULL,s[i].pin.bytes,PROT_READ|PROT_WRITE,MAP_SHARED,s[i].fd,0);if(map==MAP_FAILED)goto done;
        s[i].pin.mapping=map;if(mlock(map,s[i].pin.bytes))goto done;
        for(size_t at=0;at<s[i].pin.bytes;){
            size_t n=s[i].pin.bytes-at;if(n>32768)n=32768;
            ssize_t got;do{got=pread(s[i].target,(unsigned char *)map+at,n,(off_t)at);}while(got<0&&errno==EINTR);
            if(got<=0)goto done;at+=(size_t)got;struct timespec delay={0,1000000};nanosleep(&delay,NULL);
        }
        struct stat after;if(fstat(s[i].target,&after)||after.st_dev!=s[i].identity.st_dev||after.st_ino!=s[i].identity.st_ino||
            after.st_size!=s[i].identity.st_size||after.st_mtim.tv_sec!=s[i].identity.st_mtim.tv_sec||after.st_mtim.tv_nsec!=s[i].identity.st_mtim.tv_nsec||
            after.st_ctim.tv_sec!=s[i].identity.st_ctim.tv_sec||after.st_ctim.tv_nsec!=s[i].identity.st_ctim.tv_nsec||
            lseek(s[i].fd,s[i].position,SEEK_SET)<0)goto done;
        s[i].backup=dup(s[i].target);if(s[i].backup<0)goto done;fcntl(s[i].backup,F_SETFD,FD_CLOEXEC);
    }
    for(size_t i=0;i<count;i++){
        if(dup2(s[i].fd,s[i].target)<0)goto done;committed++;
        if(fcntl(s[i].target,F_SETFD,s[i].flags|FD_CLOEXEC))goto done;
    }
    xz_ram_assets_publish(total);reserved=0;ok=1;
    for(size_t i=0;i<count;i++){pins[i]=s[i].pin;memset(&s[i].pin,0,sizeof(s[i].pin));}
done:
    if(!ok)for(size_t i=0;i<committed;i++){
        if(dup2(s[i].backup,s[i].target)<0||fcntl(s[i].target,F_SETFD,s[i].flags))ok=-1;
    }
    for(size_t i=0;i<count;i++){
        if(s[i].pin.mapping){munlock(s[i].pin.mapping,s[i].pin.bytes);munmap(s[i].pin.mapping,s[i].pin.bytes);}
        if(s[i].fd>=0)close(s[i].fd);if(s[i].backup>=0)close(s[i].backup);
    }
    if(reserved)xz_ram_assets_cancel(total);free(s);return ok;
}
