/* SPDX-License-Identifier: GPL-2.0-or-later
 * On-device frames and controls use the existing process-neutral game bridge. */
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
extern "C" {
#include "xz_controls.h"
#include "native_bridge.h"
}

namespace XzStream {
struct __attribute__((packed)) Frame {
    char magic[4]; uint8_t version,deck; uint16_t flags;
    uint32_t sequence; uint16_t width,height,x,y; uint32_t length; float playhead;
};
struct __attribute__((packed)) Touch {
    char magic[4]; uint8_t version,type; uint16_t flags;
    uint32_t sequence; uint16_t x,y; int32_t value;
};
static_assert(sizeof(Frame)==28 && sizeof(Touch)==20,"XZ game transport ABI");
static int socket_fd=-1;
static xz_game_input *input;
static xz_doom_controls controls;
static Touch touch;
static size_t touch_bytes;
static uint32_t owner,sequence;
static uint16_t output[800*480];
static uint32_t pixels[320*200];
static uint32_t now(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint32_t(uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000);}
static bool enabled(){const char *v=getenv("XZ_GZ_DISPLAY");return v&&v[0]=='1';}
static void close(){
    if(input){uint32_t expected=owner;__atomic_compare_exchange_n(&input->owner,&expected,0,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);munmap(input,sizeof(*input));input=nullptr;}
    if(socket_fd>=0){shutdown(socket_fd,SHUT_RDWR);::close(socket_fd);socket_fd=-1;}
}
static void open(){
    if(!enabled())return;
    int fd=::open(XZ_GAME_INPUT_PATH,O_RDWR|O_NOFOLLOW);struct stat st;
    if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size!=sizeof(*input))I_FatalError("XZ game input bridge unavailable");
    input=static_cast<xz_game_input *>(mmap(nullptr,sizeof(*input),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));::close(fd);
    if(input==MAP_FAILED){input=nullptr;I_FatalError("XZ game input mapping failed");}
    if(input->magic!=XZ_GAME_INPUT_MAGIC || input->version!=1)I_FatalError("Unknown XZ game input protocol");
    uint32_t previous=__atomic_load_n(&input->owner,__ATOMIC_ACQUIRE);
    if(previous && kill(pid_t(previous),0)<0 && errno==ESRCH)__atomic_compare_exchange_n(&input->owner,&previous,0,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED);
    owner=uint32_t(getpid());uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&input->owner,&empty,owner,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))I_FatalError("Another XZ game owns the controls");
    __atomic_store_n(&input->heartbeat,now(),__ATOMIC_RELEASE);
    for(int i=0;i<2;i++){__atomic_store_n(&input->buttons[i],0,__ATOMIC_RELAXED);__atomic_store_n(&input->jog_time[i],0,__ATOMIC_RELAXED);}
    sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(50005);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    socket_fd=socket(AF_INET,SOCK_STREAM,0);timeval timeout={2,0};
    if(socket_fd<0 || setsockopt(socket_fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) || connect(socket_fd,reinterpret_cast<sockaddr *>(&address),sizeof(address)))I_FatalError("XZ display receiver unavailable");
    std::atexit(close);
    std::fprintf(stdout,"XZ_GZ_DISPLAY local screen and controls connected\n");
}
static SDL_Keycode keycode(unsigned char key){
    switch(key){
        case 0xad:return SDLK_UP;case 0xaf:return SDLK_DOWN;case 0xac:return SDLK_LEFT;case 0xae:return SDLK_RIGHT;
        case 0x9d:return SDLK_LCTRL;case 0xb8:return SDLK_SPACE;case 0xb6:return SDLK_LSHIFT;
        default:return SDL_Keycode(key);
    }
}
static void input_events(){
    if(socket_fd<0)return;
    uint32_t at=now(),buttons=0;int turn=0;
    __atomic_store_n(&input->heartbeat,at,__ATOMIC_RELEASE);
    for(int i=0;i<2;i++){
        buttons|=__atomic_load_n(&input->buttons[i],__ATOMIC_ACQUIRE);
        if(uint32_t(at-__atomic_load_n(&input->jog_time[i],__ATOMIC_ACQUIRE))<100)turn+=int32_t(__atomic_load_n(&input->jog[i],__ATOMIC_RELAXED));
    }
    xz_doom_physical(&controls,buttons,turn);
    for(;;){
        ssize_t n=recv(socket_fd,reinterpret_cast<char *>(&touch)+touch_bytes,sizeof(touch)-touch_bytes,MSG_DONTWAIT);
        if(n<0 && errno==EINTR)continue;
        if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))break;
        if(n<=0)I_FatalError("XZ display connection closed");
        touch_bytes+=size_t(n);
        if(touch_bytes==sizeof(touch)){
            if(memcmp(touch.magic,"VJTE",4) || touch.version!=1 || (touch.type!=1 && touch.type!=2))I_FatalError("Invalid XZ touch packet");
            xz_doom_touch(&controls,touch.type==1,touch.x,touch.y);touch_bytes=0;
        }
    }
    if(controls.quit)std::exit(0);
    int pressed;unsigned char key;
    while(xz_doom_key(&controls,&pressed,&key)){
        SDL_Event event={};event.type=pressed?SDL_KEYDOWN:SDL_KEYUP;event.key.type=event.type;
        event.key.state=pressed?SDL_PRESSED:SDL_RELEASED;event.key.keysym.sym=keycode(key);
        event.key.keysym.scancode=SDL_GetScancodeFromKey(event.key.keysym.sym);SDL_PushEvent(&event);
    }
}
static void send_all(const void *data,size_t count){
    const char *p=static_cast<const char *>(data);
    while(count){ssize_t n=send(socket_fd,p,count,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0)I_FatalError("XZ frame send failed");p+=n;count-=size_t(n);}
}
static void present(const unsigned char *rgba,int width,int height){
    if(socket_fd<0)return;
    if(width!=320 || height!=200)I_FatalError("XZ controls require a 320x200 game view");
    input_events();
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++){
        const unsigned char *p=rgba+((199-y)*320+x)*4;pixels[y*320+x]=uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];
    }
    xz_doom_compose(output,pixels,&controls);
    Frame frame={{'V','J','F','S'},1,0,5,sequence++,800,480,0,0,sizeof(output),0};
    send_all(&frame,sizeof(frame));send_all(output,sizeof(output));
}
}
