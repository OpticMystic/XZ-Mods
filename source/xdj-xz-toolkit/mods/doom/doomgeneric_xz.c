/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "vendor/doomgeneric.h"
#include "xz_controls.h"
#include "native_bridge.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct __attribute__((packed)) frame_header {
    char magic[4]; uint8_t version,deck; uint16_t flags;
    uint32_t sequence; uint16_t width,height,x,y; uint32_t length; float playhead;
};
struct __attribute__((packed)) touch_event {
    char magic[4]; uint8_t version,type; uint16_t flags;
    uint32_t sequence; uint16_t x,y; int32_t value;
};
_Static_assert(sizeof(struct frame_header)==28,"VJFS frame ABI");
_Static_assert(sizeof(struct touch_event)==20,"VJTE touch ABI");
static struct xz_doom_controls controls;
static uint16_t frame[800*480];
static unsigned frames,headless,seconds=900;
static uint64_t started;
static int display=-1;
static volatile sig_atomic_t stopped;
static const char *snapshot;
static struct touch_event incoming;
static size_t incoming_bytes;
static struct xz_game_input *physical;
static uint32_t owner;

static uint64_t now_ms(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void stop_signal(int sig) { (void)sig; stopped=1; }
static void cleanup(void) {
    if(physical) {
        uint32_t expected=owner;
        __atomic_compare_exchange_n(&physical->owner,&expected,0,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
        munmap(physical,sizeof(*physical)); physical=NULL;
    }
    if(display>=0) { shutdown(display,SHUT_RDWR); close(display); display=-1; }
    fprintf(stdout,"XZ_DOOM_EXIT frames=%u elapsed_ms=%llu display=%s\n",frames,
            (unsigned long long)(now_ms()-started),headless?"headless":"xz-loopback");
    fflush(stdout);
}
static void write_snapshot(void) {
    if(!snapshot) return;
    int fd=open(snapshot,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0) { perror("snapshot"); exit(2); }
    const unsigned char *bytes=(const unsigned char *)frame;
    size_t total=0;
    while(total<sizeof(frame)) {
        ssize_t n=write(fd,bytes+total,sizeof(frame)-total);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) { perror("snapshot write"); close(fd); exit(2); }
        total+=(size_t)n;
    }
    if(close(fd)) { perror("snapshot close"); exit(2); }
    snapshot=NULL;
}
static void send_all(const void *data,size_t count) {
    const unsigned char *p=data;
    while(count) {
        ssize_t n=send(display,p,count,MSG_NOSIGNAL);
        if(n<0 && errno==EINTR && !stopped) continue;
        if(n<=0) { perror("XZ display disconnected"); exit(2); }
        count-=(size_t)n; p+=n;
    }
}
void DG_Init(void) {
    started=now_ms();
    signal(SIGINT,stop_signal); signal(SIGTERM,stop_signal);
    if(atexit(cleanup)) exit(2);
    if(headless) return;
    int fd=open(XZ_GAME_INPUT_PATH,O_RDWR|O_NOFOLLOW);
    if(fd>=0) {
        struct stat info;
        if(fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_size!=sizeof(*physical)) {
            close(fd); fprintf(stderr,"Invalid native input bridge\n"); exit(2);
        }
        physical=mmap(NULL,sizeof(*physical),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); close(fd);
        if(physical==MAP_FAILED) { physical=NULL; perror("Native input mmap"); exit(2); }
        if(physical->magic!=XZ_GAME_INPUT_MAGIC || physical->version!=1) { fprintf(stderr,"Unknown native input protocol\n"); exit(2); }
        uint32_t previous=__atomic_load_n(&physical->owner,__ATOMIC_ACQUIRE);
        if(previous && kill((pid_t)previous,0)==-1 && errno==ESRCH)
            __atomic_compare_exchange_n(&physical->owner,&previous,0,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED);
        owner=(uint32_t)getpid(); uint32_t empty=0;
        if(!__atomic_compare_exchange_n(&physical->owner,&empty,owner,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {
            fprintf(stderr,"Doom already owns the physical controls\n"); exit(2);
        }
        for(unsigned i=0;i<2;i++) {
            __atomic_store_n(&physical->buttons[i],0,__ATOMIC_RELAXED);
            __atomic_store_n(&physical->jog_time[i],0,__ATOMIC_RELAXED);
        }
        __atomic_store_n(&physical->heartbeat,(uint32_t)now_ms(),__ATOMIC_RELEASE);
        printf("XZ_DOOM_INPUT native bridge claimed pid=%u\n",owner);
    } else { fprintf(stderr,"XZ_DOOM_INPUT native bridge absent; touchscreen only\n"); }
    /* This never connects to the laptop. XZ Mods owns the actual screen/input. */
    struct sockaddr_in address={0};
    address.sin_family=AF_INET; address.sin_port=htons(50005);
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    display=socket(AF_INET,SOCK_STREAM,0);
    struct timeval timeout={2,0};
    if(display<0 || setsockopt(display,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) ||
       connect(display,(struct sockaddr *)&address,sizeof(address))) {
        perror("XZ Mods display receiver"); exit(2);
    }
}
void DG_DrawFrame(void) {
    if(physical) __atomic_store_n(&physical->heartbeat,(uint32_t)now_ms(),__ATOMIC_RELEASE);
    uint64_t elapsed=now_ms()-started;
    if(stopped || controls.quit || (seconds && elapsed>=(uint64_t)seconds*1000)) exit(0);
    xz_doom_compose(frame,DG_ScreenBuffer,&controls);
    /* Capture after startup wipes so the evidence contains a rendered scene. */
    if(elapsed>=3000) write_snapshot();
    if(!headless) {
        struct frame_header h={"VJFS",1,0,5,frames,800,480,0,0,sizeof(frame),0};
        send_all(&h,sizeof(h)); send_all(frame,sizeof(frame));
    }
    frames++;
    if(frames==1 || frames%175==0) {
        printf("XZ_DOOM_FRAME count=%u elapsed_ms=%llu\n",frames,(unsigned long long)elapsed); fflush(stdout);
    }
}
void DG_SleepMs(uint32_t ms) {
    if(physical) __atomic_store_n(&physical->heartbeat,(uint32_t)now_ms(),__ATOMIC_RELEASE);
    struct timespec t={ms/1000,(long)(ms%1000)*1000000};
    while(nanosleep(&t,&t) && errno==EINTR && !stopped) {}
    if(stopped || (seconds && now_ms()-started>=(uint64_t)seconds*1000)) exit(0);
}
uint32_t DG_GetTicksMs(void) { return (uint32_t)(now_ms()-started); }
int DG_GetKey(int *pressed,unsigned char *key) {
    if(physical) {
        uint32_t now=(uint32_t)now_ms(),buttons=0; int turn=0;
        __atomic_store_n(&physical->heartbeat,now,__ATOMIC_RELEASE);
        for(unsigned i=0;i<2;i++) {
            buttons|=__atomic_load_n(&physical->buttons[i],__ATOMIC_ACQUIRE);
            uint32_t at=__atomic_load_n(&physical->jog_time[i],__ATOMIC_ACQUIRE);
            if((uint32_t)(now-at)<100) turn+=(int32_t)__atomic_load_n(&physical->jog[i],__ATOMIC_RELAXED);
        }
        xz_doom_physical(&controls,buttons,turn);
    }
    if(display>=0) for(;;) {
        ssize_t n=recv(display,(unsigned char *)&incoming+incoming_bytes,
                       sizeof(incoming)-incoming_bytes,MSG_DONTWAIT);
        if(n<0 && errno==EINTR) continue;
        if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
        if(n<=0) { fprintf(stderr,"XZ touch connection closed\n"); exit(2); }
        incoming_bytes+=(size_t)n;
        if(incoming_bytes==sizeof(incoming)) {
            if(memcmp(incoming.magic,"VJTE",4) || incoming.version!=1 ||
               (incoming.type!=1 && incoming.type!=2)) { fprintf(stderr,"Invalid XZ touch packet\n"); exit(2); }
            xz_doom_touch(&controls,incoming.type==1,incoming.x,incoming.y);
            printf("XZ_DOOM_TOUCH type=%u x=%u y=%u\n",incoming.type,incoming.x,incoming.y); fflush(stdout);
            incoming_bytes=0;
        }
    }
    return xz_doom_key(&controls,pressed,key);
}
void DG_SetWindowTitle(const char *title) { printf("XZ_DOOM_TITLE %s\n",title); }
int main(int argc,char **argv) {
    /* Adapter options are removed before the Doom parser sees its arguments. */
    int out=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"-xz-headless")) headless=1;
        else if(!strcmp(argv[i],"-xz-seconds") && i+1<argc) {
            char *end; unsigned long value=strtoul(argv[++i],&end,10);
            if(*end || value>86400) { fprintf(stderr,"Invalid time limit\n"); return 2; }
            seconds=(unsigned)value;
        } else if(!strcmp(argv[i],"-xz-snapshot") && i+1<argc) snapshot=argv[++i];
        else argv[out++]=argv[i];
    }
    argc=out; argv[out]=NULL;
    doomgeneric_Create(argc,argv);
    while(1) doomgeneric_Tick();
}
