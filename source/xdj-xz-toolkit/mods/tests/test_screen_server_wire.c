#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#define BYTES 768000u
#define FAKE_FB 10001
static unsigned char framebuffer[BYTES];
static unsigned status_calls;
static int animated_fixture;
static unsigned fixture_tick;
static unsigned capture_calls;
static int read_full(int fd,void *buffer,size_t size){
    unsigned char *data=buffer;
    while(size){ssize_t count=recv(fd,data,size,0);if(count<0&&errno==EINTR)continue;if(count<=0)return 0;data+=count;size-=(size_t)count;}return 1;
}
static void log_line(const char *line){(void)line;}
static int fake_open(const char *path,int flags){assert(!strcmp(path,"/dev/fb0")&&flags==O_RDONLY);return FAKE_FB;}
static int fake_close(int fd){return fd==FAKE_FB?0:close(fd);}
static int fake_ioctl(int fd,unsigned long request,void *out){
    assert(fd==FAKE_FB);
    if(request==FBIOGET_FSCREENINFO){struct fb_fix_screeninfo value={.line_length=1600,.smem_len=BYTES};memcpy(out,&value,sizeof(value));return 0;}
    assert(request==FBIOGET_VSCREENINFO);
    capture_calls++;
    if(!animated_fixture&&capture_calls==4){framebuffer[24690]=0xe0;framebuffer[24691]=0x07;}
    if(animated_fixture){
        static const unsigned short colors[]={0xf800,0x07e0,0x001f,0xffff,0xffe0,0x07ff,0xf81f,0};
        fixture_tick++;
        for(size_t y=0;y<480;y++)for(size_t x=0;x<800;x++){
            unsigned short color=colors[((x+fixture_tick*7)/100)%8];
            if(y>220&&y<260)color=(unsigned short)(fixture_tick&65535);
            framebuffer[(y*800+x)*2]=(unsigned char)color;framebuffer[(y*800+x)*2+1]=(unsigned char)(color>>8);
        }
    }
    struct fb_var_screeninfo value={.xres=800,.yres=480,.bits_per_pixel=16,.red={.offset=11,.length=5},.green={.offset=5,.length=6},.blue={.offset=0,.length=5}};
    memcpy(out,&value,sizeof(value));return 0;
}
static void *fake_mmap(void *address,size_t length,int protection,int flags,int fd,off_t offset){
    assert(!address&&length==BYTES&&protection==PROT_READ&&flags==MAP_SHARED&&fd==FAKE_FB&&!offset);return framebuffer;
}
static int fake_munmap(void *address,size_t length){assert(address==framebuffer&&length==BYTES);return 0;}
size_t xz_mods_stem_status_json_v1(char *out,size_t capacity){
    const char *value="{\"test\":true}";size_t size=strlen(value);assert(capacity>size);memcpy(out,value,size+1);status_calls++;return size;
}
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#include "../../vendor/tools/xz_runtime/xz_screen_server.h"
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
static void *client(void *arg){int fd=(int)(intptr_t)arg;xz_screen_client(fd);close(fd);return NULL;}
static uint32_t number(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned short word(const unsigned char *p){return (unsigned short)(p[0]|(unsigned short)p[1]<<8);}
static void decode(const unsigned char *payload,size_t size,unsigned char *out,int delta){
    size_t offset=0,pixel=0;
    while(offset<size){assert(size-offset>=2);unsigned command=word(payload+offset);offset+=2;size_t count=(command&32767)+1;
        assert(count<=BYTES/2-pixel);size_t bytes=(command&32768)?2:count*2;assert(bytes<=size-offset);
        for(size_t i=0;i<count;i++){unsigned short color=word(payload+offset+((command&32768)?0:i*2));if(delta)color^=word(out+(pixel+i)*2);out[(pixel+i)*2]=(unsigned char)color;out[(pixel+i)*2+1]=(unsigned char)(color>>8);}
        pixel+=count;offset+=bytes;
    }
    assert(pixel==BYTES/2);
}
int main(int argc,char **argv){
    (void)&xz_screen_start;
    if(argc==2){
        animated_fixture=1;int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
        struct sockaddr_in address={.sin_family=AF_INET,.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)},.sin_port=0};
        assert(!bind(listener,(struct sockaddr*)&address,sizeof(address))&&!listen(listener,1));
        socklen_t length=sizeof(address);assert(!getsockname(listener,(struct sockaddr*)&address,&length));
        FILE *receipt=fopen(argv[1],"w");assert(receipt);fprintf(receipt,"%u\n",ntohs(address.sin_port));fclose(receipt);
        int connection=accept(listener,NULL,NULL);assert(connection>=0);xz_screen_client(connection);close(connection);close(listener);return 0;
    }
    for(size_t i=0;i<BYTES;i+=2){framebuffer[i]=0;framebuffer[i+1]=0xf8;}
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));pthread_t worker;assert(!pthread_create(&worker,NULL,client,(void*)(intptr_t)pair[0]));
    struct timeval timeout={2,0};setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    unsigned char header[32],*payload=malloc(BYTES),*pixels=calloc(1,BYTES);assert(payload&&pixels);
    assert(send(pair[1],"XZSC\1\0\0\0",8,0)==8);assert(read_full(pair[1],header,20));
    assert(word(header+4)==1&&number(header+12)==BYTES&&number(header+16)==1);
    assert(read_full(pair[1],payload,BYTES)&&!memcmp(payload,framebuffer,BYTES));
    for(unsigned i=0;i<2;i++){
        assert(send(pair[1],"XZSD\1\0\0\0",8,0)==8);assert(read_full(pair[1],header,8));assert(!memcmp(header,"XZSD",4));size_t size=number(header+4);assert(size==13&&read_full(pair[1],payload,size));assert(!memcmp(payload,"{\"test\":true}",size));
    }
    assert(send(pair[1],"XZSC\x63\0\0\0",8,0)==8);assert(recv(pair[1],header,1,0)==0);pthread_join(worker,NULL);close(pair[1]);assert(status_calls==1);
    capture_calls=0;status_calls=0;
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(!pthread_create(&worker,NULL,client,(void*)(intptr_t)pair[0]));
    setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    assert(send(pair[1],"XZSC\2\0\0\0",8,0)==8);
    unsigned frames=0,statuses=0;
    while(frames<3){
        assert(read_full(pair[1],header,4));
        if(!memcmp(header,"XZSD",4)){
            assert(read_full(pair[1],header+4,4));size_t size=number(header+4);assert(size==13&&read_full(pair[1],payload,size));assert(!memcmp(payload,"{\"test\":true}",size));statuses++;continue;
        }
        assert(!memcmp(header,"XZSF",4)&&read_full(pair[1],header+4,28));frames++;
        assert(word(header+4)==2&&number(header+16)==frames);unsigned codec=word(header+10);size_t size=number(header+12);
        assert(size<=BYTES&&read_full(pair[1],payload,size));
        if(frames==1)assert(codec==1&&size==48);
        if(frames==2)assert(codec==3&&size==0);
        if(frames==3)assert(codec==2);
        if(codec!=3)decode(payload,size,pixels,codec==2);
        assert(!memcmp(pixels,framebuffer,BYTES));
        if(frames==2){
            struct timeval short_timeout={0,100000};setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&short_timeout,sizeof(short_timeout));
            assert(recv(pair[1],header,1,0)==-1&&(errno==EAGAIN||errno==EWOULDBLOCK));
            setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            assert(send(pair[1],"XZAK\2\0\0\0",8,0)==8);
        }
    }
    assert(statuses==1);close(pair[1]);pthread_join(worker,NULL);assert(status_calls==1);capture_calls=0;status_calls=0;
    for(size_t i=0;i<BYTES/2;i++){uint16_t value=(uint16_t)i;framebuffer[i*2]=(unsigned char)value;framebuffer[i*2+1]=(unsigned char)(value>>8);}
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(!pthread_create(&worker,NULL,client,(void*)(intptr_t)pair[0]));
    setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    assert(send(pair[1],"XZSC\3\0\0\0",8,0)==8);frames=0;statuses=0;
    while(frames<3){
        assert(read_full(pair[1],header,4));
        if(!memcmp(header,"XZSD",4)){assert(read_full(pair[1],header+4,4));size_t count=number(header+4);assert(count==13&&read_full(pair[1],payload,count));statuses++;continue;}
        assert(!memcmp(header,"XZSF",4)&&read_full(pair[1],header+4,28));frames++;
        assert(word(header+4)==3&&word(header+6)==800&&word(header+8)==480&&word(header+10)==4);
        assert(number(header+12)==BYTES/2&&number(header+16)==frames&&read_full(pair[1],payload,BYTES/2));
        for(size_t i=0;i<BYTES/2;i++){
            uint16_t color=word(framebuffer+i*2);
            unsigned r=(color>>11)&31,g=(color>>5)&63,b=color&31;
            assert(payload[i]==((r/4)<<5|(g/8)<<2|b/8));
        }
        if(frames==2){
            struct timeval short_timeout={0,100000};setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&short_timeout,sizeof(short_timeout));
            assert(recv(pair[1],header,1,0)==-1&&(errno==EAGAIN||errno==EWOULDBLOCK));
            setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            assert(send(pair[1],"XZAK\2\0\0\0",8,0)==8);
        }
    }
    assert(statuses==1);close(pair[1]);pthread_join(worker,NULL);free(payload);free(pixels);
    puts("PASS real screen handler: legacy, lossless v2, raw RGB332 v3, full frames, two-frame credit, cached diagnostics and teardown");return 0;
}
