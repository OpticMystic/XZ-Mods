/* SPDX-License-Identifier: MIT
 * Native XZ EGL pbuffer backend. No desktop renderer or physical window. */
#include <dlfcn.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include "stream.inc"
namespace XzOffscreen {
using Handle=void *;
static Handle library,gles,display,surface,context;
static int width=320,height=200;
static unsigned frames;
static uint64_t started;
static bool snapshot_done;
static void (*read_pixels)(int,int,int,int,unsigned,unsigned,void *);
static unsigned (*get_error)();
static void close();
static uint64_t milliseconds(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
static bool enabled(){const char *value=getenv("XZ_GZ_OFFSCREEN");return value&&value[0]=='1';}
template<class T>static T symbol(const char *name){T result=reinterpret_cast<T>(dlsym(library,name));if(!result)I_FatalError("Missing XZ EGL symbol %s",name);return result;}
static void open(int requested_width,int requested_height){
    width=requested_width>0?requested_width:320;height=requested_height>0?requested_height:200;
    if(width>800||height>480)I_FatalError("XZ native render limit is 800x480");
    library=dlopen("/usr/lib/libEGL-fb.so",RTLD_NOW|RTLD_LOCAL);gles=dlopen("/usr/lib/libGLESv2-fb.so.2.0.0",RTLD_NOW|RTLD_LOCAL);
    if(!library||!gles)I_FatalError("XZ graphics libraries missing: %s",dlerror());
    get_error=symbol<unsigned(*)()>("eglGetError");
    auto native=reinterpret_cast<void *(*)(int)>(dlsym(library,"fbGetDisplayByIndex"));
    display=symbol<Handle(*)(void *)>("eglGetDisplay")(native?native(0):nullptr);
    int major=0,minor=0;
    if(!symbol<unsigned(*)(Handle,int *,int *)>("eglInitialize")(display,&major,&minor))I_FatalError("XZ EGL init failed 0x%x",get_error());
    int attributes[]={0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3026,8,0x3038},count=0;
    Handle config=nullptr;
    if(!symbol<unsigned(*)(Handle,const int *,Handle *,int,int *)>("eglChooseConfig")(display,attributes,&config,1,&count)||!count)I_FatalError("XZ has no depth24/stencil8 GLES pbuffer");
    symbol<unsigned(*)(unsigned)>("eglBindAPI")(0x30a0);
    int sizes[]={0x3057,width,0x3056,height,0x3038},version[]={0x3098,2,0x3038};
    surface=symbol<Handle(*)(Handle,Handle,const int *)>("eglCreatePbufferSurface")(display,config,sizes);
    context=symbol<Handle(*)(Handle,Handle,Handle,const int *)>("eglCreateContext")(display,config,nullptr,version);
    if(!surface||!context||!symbol<unsigned(*)(Handle,Handle,Handle,Handle)>("eglMakeCurrent")(display,surface,surface,context))I_FatalError("XZ GLES context failed 0x%x",get_error());
    read_pixels=reinterpret_cast<decltype(read_pixels)>(dlsym(gles,"glReadPixels"));
    if(!read_pixels)I_FatalError("XZ GPU readback unavailable");
    started=milliseconds();std::fprintf(stdout,"XZ_GZ_EGL %dx%d GLES2 depth24 stencil8\n",width,height);
    std::atexit(close);
    XzStream::open();
}
static void close(){
    if(!library||!display)return;
    symbol<unsigned(*)(Handle,Handle,Handle,Handle)>("eglMakeCurrent")(display,nullptr,nullptr,nullptr);
    if(context)symbol<unsigned(*)(Handle,Handle)>("eglDestroyContext")(display,context);
    if(surface)symbol<unsigned(*)(Handle,Handle)>("eglDestroySurface")(display,surface);
    symbol<unsigned(*)(Handle)>("eglTerminate")(display);display=surface=context=nullptr;
}
static void present(){
    ++frames;
    if(frames==1)started=milliseconds();
    const char *snapshot=getenv("XZ_GZ_SNAPSHOT");
    if(XzStream::enabled()||(snapshot&&!snapshot_done&&frames>=5)){
        std::vector<unsigned char> rgba(size_t(width)*height*4);read_pixels(0,0,width,height,0x1908,0x1401,rgba.data());
        XzStream::present(rgba.data(),width,height);
        if(snapshot&&!snapshot_done&&frames>=5){
        int fd=::open(snapshot,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd>=0){size_t remaining=rgba.size(),offset=0;while(remaining){ssize_t n=write(fd,rgba.data()+offset,remaining);if(n<=0)break;remaining-=size_t(n);offset+=size_t(n);}::close(fd);
            std::fprintf(stdout,"XZ_GZ_SNAPSHOT %s bytes=%zu\n",snapshot,offset);snapshot_done=true;}
        }
    }
    if(frames==1||frames%100==0)std::fprintf(stdout,"XZ_GZ_FRAME %u elapsed_ms=%llu\n",frames,(unsigned long long)(milliseconds()-started));
    const char *limit=getenv("XZ_GZ_SECONDS");unsigned seconds=limit?unsigned(strtoul(limit,nullptr,10)):0;
    if(seconds&&milliseconds()-started>=uint64_t(seconds)*1000){std::fprintf(stdout,"XZ_GZ_EXIT frames=%u\n",frames);std::exit(0);}
}
}
