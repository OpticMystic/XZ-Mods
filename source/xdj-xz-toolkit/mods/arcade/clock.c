/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "clock.h"
#include <string.h>
int xz_arcade_decode_beat(const void *data,size_t n,struct xz_arcade_beat *out){
    const unsigned char *p=data;
    if(!p||!out||n!=96||memcmp(p,"Qspt1WmJOL",10)||p[10]!=0x28||p[31]!=1||p[32]||p[34]||p[35]!=0x3c)return 0;
    unsigned deck=p[33],bar=p[92],pitch=(unsigned)p[85]<<16|(unsigned)p[86]<<8|p[87],bpm=(unsigned)p[90]<<8|p[91];
    if((deck!=1&&deck!=2&&deck!=17&&deck!=18)||p[95]!=deck||bar<1||bar>4||!pitch||pitch>0x200000||!bpm||bpm==65535)return 0;
    float value=(float)bpm*(float)pitch/(100.f*1048576.f);
    if(value<40||value>240)return 0;
    *out=(struct xz_arcade_beat){.bpm=value,.deck=(deck&15)-1,.bar=bar};return 1;
}
#ifndef XZ_ARCADE_CLOCK_PORTABLE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
static struct xz_arcade_beat mailbox[2];
static uint32_t mailbox_lock[2];
static ssize_t (*stock_sendto)(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
static ssize_t (*stock_recvfrom)(int,void *,size_t,int,struct sockaddr *,socklen_t *);
static pthread_once_t resolve_once=PTHREAD_ONCE_INIT;
static void resolve(void){stock_sendto=dlsym(RTLD_NEXT,"sendto");stock_recvfrom=dlsym(RTLD_NEXT,"recvfrom");}
static void observe(const void *p,size_t n){
    struct xz_arcade_beat beat;
    if(!xz_arcade_decode_beat(p,n,&beat))return;
    struct timespec t;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return;
    beat.at=(uint32_t)((uint64_t)t.tv_sec*1000+(uint32_t)t.tv_nsec/1000000);
    uint32_t zero=0;
    if(!__atomic_compare_exchange_n(&mailbox_lock[beat.deck],&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
    beat.sequence=mailbox[beat.deck].sequence+1;mailbox[beat.deck]=beat;
    __atomic_store_n(&mailbox_lock[beat.deck],0,__ATOMIC_RELEASE);
}
int xz_arcade_clock_read(unsigned deck,struct xz_arcade_beat *out){
    if(deck>1||!out)return 0;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&mailbox_lock[deck],&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    *out=mailbox[deck];__atomic_store_n(&mailbox_lock[deck],0,__ATOMIC_RELEASE);return out->sequence!=0;
}
/* Observe existing stock traffic, with identical arguments, return and errno.
 * No socket is created and no extra packet is transmitted by the game. */
__attribute__((visibility("default"))) ssize_t sendto(int fd,const void *p,size_t n,int flags,const struct sockaddr *a,socklen_t len){
    int before=errno;pthread_once(&resolve_once,resolve);errno=before;
    ssize_t result=stock_sendto?stock_sendto(fd,p,n,flags,a,len):syscall(SYS_sendto,fd,p,n,flags,a,len);
    int saved=errno;if(result==96&&n==96)observe(p,n);errno=saved;return result;
}
__attribute__((visibility("default"))) ssize_t recvfrom(int fd,void *p,size_t n,int flags,struct sockaddr *a,socklen_t *len){
    int before=errno;pthread_once(&resolve_once,resolve);errno=before;
    ssize_t result=stock_recvfrom?stock_recvfrom(fd,p,n,flags,a,len):syscall(SYS_recvfrom,fd,p,n,flags,a,len);
    int saved=errno;if(result==96&&n>=96)observe(p,96);errno=saved;return result;
}
#endif
