#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
static int read_full(int fd,void *data,size_t size){(void)fd;(void)data;(void)size;return 0;}
static void log_line(const char *text){(void)text;}
#include "../../vendor/tools/xz_runtime/xz_screen_server.h"
int main(void){
    (void)&xz_screen_start;
    struct fb_var_screeninfo v={.xres=800,.yres=480,.bits_per_pixel=16,
        .red={.offset=11,.length=5},.green={.offset=5,.length=6},.blue={.offset=0,.length=5}};
    struct fb_fix_screeninfo f={.line_length=1600,.smem_len=768000};
    assert(xz_screen_mode(&v,&f));
    v.yoffset=480;assert(!xz_screen_mode(&v,&f));f.smem_len=1536000;assert(xz_screen_mode(&v,&f));
    v.yoffset=UINT32_MAX;assert(!xz_screen_mode(&v,&f));
    v.yoffset=0;v.xoffset=1;assert(!xz_screen_mode(&v,&f));
    f.line_length=1664;assert(xz_screen_mode(&v,&f));
    v.xoffset=33;assert(!xz_screen_mode(&v,&f));
    v.xoffset=0;v.bits_per_pixel=32;assert(!xz_screen_mode(&v,&f));
    v.bits_per_pixel=16;v.red.offset=0;assert(!xz_screen_mode(&v,&f));
    puts("PASS RGB565 framebuffer geometry, padded rows, page offsets and overflow rejection");
    return 0;
}
