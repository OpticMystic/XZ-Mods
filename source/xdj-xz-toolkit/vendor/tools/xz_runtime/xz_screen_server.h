/* SPDX-License-Identifier: MIT
 * Screen observation runs on its own worker, outside drawing/audio callbacks.
 * Protocol: XZSC + LE u32(1), then XZSF + LE u16(1,800,480,0),
 * LE u32(768000,sequence), followed by little-endian RGB565 pixels.
 */
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>

static int xz_screen_started;
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
static int xz_screen_mode(const struct fb_var_screeninfo *v, const struct fb_fix_screeninfo *f) {
    return v->xres == 800 && v->yres == 480 && v->bits_per_pixel == 16 &&
        v->red.offset == 11 && v->red.length == 5 && v->green.offset == 5 &&
        v->green.length == 6 && v->blue.offset == 0 && v->blue.length == 5 &&
        f->line_length >= 1600 && v->xoffset <= (f->line_length - 1600) / 2 &&
        ((uint64_t)v->yoffset + 480u) * f->line_length <= f->smem_len;
}
static void xz_screen_client(int client) {
    int fb = -1;
    unsigned char *map = MAP_FAILED, *pixels = NULL;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    unsigned char request[8], header[20] = {'X','Z','S','F',1,0,32,3,224,1,0,0,0,184,11,0,0,0,0,0};
    uint32_t sequence = 0;
    struct timeval timeout = {3,0};
    setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    fb = open("/dev/fb0",O_RDONLY);
    if (fb < 0 || ioctl(fb,FBIOGET_FSCREENINFO,&f) < 0 || ioctl(fb,FBIOGET_VSCREENINFO,&v) < 0 ||
        !xz_screen_mode(&v,&f) || f.smem_len > 16u*1024u*1024u) goto done;
    map = mmap(NULL,f.smem_len,PROT_READ,MAP_SHARED,fb,0);
    pixels = malloc(800u*480u*2u);
    if (map == MAP_FAILED || !pixels) goto done;
    while (read_full(client,request,sizeof(request))) {
        if (!memcmp(request,"XZSD\1\0\0\0",8)) {
            char status[16384];
            size_t (*snapshot)(char *,size_t)=dlsym(RTLD_DEFAULT,"xz_mods_stem_status_json_v1");
            size_t bytes=snapshot?snapshot(status,sizeof(status)):0;
            if (!bytes) {strcpy(status,"{\"busy\":true}");bytes=strlen(status);}
            unsigned char reply[8]={'X','Z','S','D',0,0,0,0};
            for(unsigned i=0;i<4;i++)reply[4+i]=(unsigned char)(bytes>>(8*i));
            if(!xz_screen_write(client,reply,sizeof(reply))||!xz_screen_write(client,status,bytes))break;
            usleep(500000);
            continue;
        }
        if (memcmp(request,"XZSC\1\0\0\0",8) || ioctl(fb,FBIOGET_VSCREENINFO,&v) < 0 || !xz_screen_mode(&v,&f)) break;
        for (unsigned y=0;y<480;y++)
            memcpy(pixels+y*1600u,map+(size_t)(y+v.yoffset)*f.line_length+v.xoffset*2u,1600);
        sequence++;
        for (unsigned i=0;i<4;i++) header[16+i]=(unsigned char)(sequence>>(8*i));
        if (!xz_screen_write(client,header,sizeof(header)) || !xz_screen_write(client,pixels,768000)) break;
        usleep(500000); /* One client, at most two captures per second. */
    }
done:
    free(pixels);
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
    log_line("XZ_SCREEN_LISTENING port=50010 read_only=1 max_fps=2");
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
