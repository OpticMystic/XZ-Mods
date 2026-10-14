/* SPDX-License-Identifier: MIT
 * Screen observation runs on its own worker, outside drawing/audio callbacks.
 * Protocol: XZSC + LE u32(1), then XZSF + LE u16(1,800,480,0),
 * LE u32(768000,sequence), followed by little-endian RGB565 pixels.
 * Version 2 replies have a 32-byte header: codec at 10, encoded size at 12,
 * sequence at 16, capture timestamp us at 20, copy us at 24, encode us at 28.
 * Codecs: 0 raw, 1 RLE16 keyframe, 2 RLE16 XOR delta, 3 unchanged frame.
 * A v2 request starts continuous delivery. XZAK + LE sequence acknowledges a
 * frame; no more than two frames are in flight. XZSD snapshots arrive at 1 Hz.
 */
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <netinet/tcp.h>
#include "xz_screen_codec.h"

static int xz_screen_started;
static uint32_t xz_screen_time_us(void) {
    /* Use the old Linux syscall ABI, not the unavailable GLIBC_2.17 symbol. */
#ifdef SYS_clock_gettime
    struct {long seconds,nanoseconds;} stamp;
    if (syscall(SYS_clock_gettime,1,&stamp)==0)
        return (uint32_t)((uint64_t)stamp.seconds*1000000u+(unsigned long)stamp.nanoseconds/1000u);
#endif
    struct timeval fallback;gettimeofday(&fallback,NULL);
    return (uint32_t)((uint64_t)fallback.tv_sec*1000000u+(unsigned long)fallback.tv_usec);
}
static void xz_screen_put32(unsigned char *out,uint32_t value) {
    for(unsigned i=0;i<4;i++)out[i]=(unsigned char)(value>>(8*i));
}
static int xz_screen_write(int fd, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += n; size -= (size_t)n;
    }
    return 1;
}
static int xz_screen_status(int client,char *status,size_t *bytes,uint32_t *at){
    uint32_t now=xz_screen_time_us();
    if(!*bytes || now-*at>=250000u){
        size_t (*snapshot)(char *,size_t)=dlsym(RTLD_DEFAULT,"xz_mods_stem_status_json_v1");
        *bytes=snapshot?snapshot(status,16384):0;
        if(!*bytes || *bytes>16384){strcpy(status,"{\"busy\":true}");*bytes=strlen(status);}
        *at=now;
    }
    unsigned char reply[8]={'X','Z','S','D',0,0,0,0};xz_screen_put32(reply+4,(uint32_t)*bytes);
    return xz_screen_write(client,reply,sizeof(reply))&&xz_screen_write(client,status,*bytes);
}
static int xz_screen_mode(const struct fb_var_screeninfo *v, const struct fb_fix_screeninfo *f) {
    return v->xres == 800 && v->yres == 480 && v->bits_per_pixel == 16 &&
        v->red.offset == 11 && v->red.length == 5 && v->green.offset == 5 &&
        v->green.length == 6 && v->blue.offset == 0 && v->blue.length == 5 &&
        f->line_length >= 1600 && v->xoffset <= (f->line_length - 1600) / 2 &&
        ((uint64_t)v->yoffset + 480u) * f->line_length <= f->smem_len;
}
static void xz_screen_client(int client) {
    int fb = -1;
    unsigned char *map = MAP_FAILED, *pixels = NULL, *previous = NULL, *encoded = NULL;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    unsigned char request[8], header[32] = {'X','Z','S','F',1,0,32,3,224,1,0,0,0,184,11,0,0,0,0,0};
    uint32_t sequence = 0, previous_sequence = 0, status_at = 0, next_frame_at = 0, acknowledged = 0;
    unsigned paced_version = 0;
    int previous_valid = 0, one = 1, streaming = 0;
    char status[16384];size_t status_bytes=0;
    struct timeval timeout = {3,0};
    setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
    setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    fb = open("/dev/fb0",O_RDONLY);
    if (fb < 0 || ioctl(fb,FBIOGET_FSCREENINFO,&f) < 0 || ioctl(fb,FBIOGET_VSCREENINFO,&v) < 0 ||
        !xz_screen_mode(&v,&f) || f.smem_len > 16u*1024u*1024u) goto done;
    map = mmap(NULL,f.smem_len,PROT_READ,MAP_SHARED,fb,0);
    pixels = malloc(800u*480u*2u);
    if (map == MAP_FAILED || !pixels) goto done;
    for (;;) {
        if(streaming){
            while(sequence-acknowledged>=2u){
                if(!read_full(client,request,sizeof(request))||memcmp(request,"XZAK",4))goto done;
                uint32_t ack=(uint32_t)request[4]|(uint32_t)request[5]<<8|(uint32_t)request[6]<<16|(uint32_t)request[7]<<24;
                if(!((uint32_t)(ack-acknowledged)) || ack-acknowledged>sequence-acknowledged)goto done;
                acknowledged=ack;
            }
            memcpy(request,"XZSC\2\0\0\0",8);
        }else if(!read_full(client,request,sizeof(request)))break;
        if (!memcmp(request,"XZSD\1\0\0\0",8)) {
            if(!xz_screen_status(client,status,&status_bytes,&status_at))break;
            continue;
        }
        int fast=!memcmp(request,"XZSC\2\0\0\0",8);
        if (!fast && memcmp(request,"XZSC\1\0\0\0",8)) break;
        if(fast&&!streaming){if(sequence)break;streaming=1;}
        if(fast && !previous){previous=malloc(768000);encoded=malloc(768000);if(!previous||!encoded)break;}
        uint32_t interval=fast?33333u:500000u, now=xz_screen_time_us();
        unsigned version=fast?2u:1u;
        if(paced_version!=version){next_frame_at=now;paced_version=version;}
        int32_t delay=(int32_t)(next_frame_at-now);
        /* Keep an absolute cadence instead of accumulating sleep overshoot.
         * If a slot is missed, sample a fresh frame; never queue old pictures. */
        if(delay>(int32_t)interval || delay<-(int32_t)interval)next_frame_at=now;
        while((delay=(int32_t)(next_frame_at-now))>0 && delay<=(int32_t)interval){usleep((useconds_t)delay);now=xz_screen_time_us();}
        uint32_t started=now;next_frame_at+=interval;
        if (ioctl(fb,FBIOGET_VSCREENINFO,&v) < 0 || !xz_screen_mode(&v,&f)) break;
        for (unsigned y=0;y<480;y++)
            memcpy(pixels+y*1600u,map+(size_t)(y+v.yoffset)*f.line_length+v.xoffset*2u,1600);
        uint32_t copied=xz_screen_time_us();
        sequence++;
        size_t size=768000;unsigned codec=0;const unsigned char *payload=pixels;
        if(fast){
            int delta=previous_valid && previous_sequence==sequence-1;
            if(delta && !memcmp(pixels,previous,768000)){size=0;codec=3;}
            else {
                size_t packed=xz_screen_rle16(encoded,768000,pixels,delta?previous:NULL,800u*480u);
                if(packed){size=packed;codec=delta?2:1;payload=encoded;}
            }
        }
        uint32_t encoded_at=xz_screen_time_us();
        header[4]=(unsigned char)(fast?2:1);xz_screen_put16(header+10,(uint16_t)codec);
        xz_screen_put32(header+12,(uint32_t)size);xz_screen_put32(header+16,sequence);
        xz_screen_put32(header+20,started);xz_screen_put32(header+24,copied-started);
        xz_screen_put32(header+28,encoded_at-copied);
        if (!xz_screen_write(client,header,fast?32:20) || !xz_screen_write(client,payload,size)) break;
        if(fast){unsigned char *swap=previous;previous=pixels;pixels=swap;previous_sequence=sequence;previous_valid=1;}
        if(fast&&(!status_bytes || xz_screen_time_us()-status_at>=1000000u)){
            if(!xz_screen_status(client,status,&status_bytes,&status_at))break;
        }
    }
done:
    free(pixels);
    free(previous);free(encoded);
    if (map != MAP_FAILED) munmap(map,f.smem_len);
    if (fb >= 0) close(fb);
}
static void *xz_screen_listener(void *unused) {
    int server = socket(AF_INET,SOCK_STREAM,0), one = 1;
    struct sockaddr_in address;
    (void)unused;
    struct sched_param priority={0};
    pthread_setschedparam(pthread_self(),SCHED_OTHER,&priority);
    setpriority(PRIO_PROCESS,0,10);
    if (server < 0) return NULL;
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_ANY); address.sin_port=htons(50010);
    if (bind(server,(struct sockaddr *)&address,sizeof(address)) < 0 || listen(server,2) < 0) {close(server);return NULL;}
    log_line("XZ_SCREEN_LISTENING port=50010 read_only=1 v1_fps=2 v2_target_fps=30");
    for (;;) {
        int client = accept(server,NULL,NULL);
        if (client < 0) {if (errno == EINTR) continue;break;}
        xz_screen_client(client); close(client);
    }
    close(server); return NULL;
}
static void xz_screen_start(void) {
    pthread_t thread;
    if (xz_screen_started) return;
    if (pthread_create(&thread,NULL,xz_screen_listener,NULL) == 0) {
        xz_screen_started=1; pthread_detach(thread);
    } else log_line("ERROR screen listener thread failed");
}
