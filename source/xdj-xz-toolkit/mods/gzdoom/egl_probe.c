/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* EGL/GLES scalar ABI and constants from the Khronos registries. */
typedef void *Display,*Config,*Context,*Surface;
typedef int Int;typedef unsigned Bool;
static unsigned (*error_code)(void);
static int step(int condition,const char *name) {printf("%s %s error=0x%x\n",name,condition?"PASS":"FAIL",error_code?error_code():0);fflush(stdout);return condition;}
static void flags(const char *path) {unsigned char h[40];int fd=open(path,O_RDONLY);if(fd>=0){if(read(fd,h,40)==40)printf("LIB_ABI %s flags=0x%x\n",path,h[36]|(unsigned)h[37]<<8|(unsigned)h[38]<<16|(unsigned)h[39]<<24);close(fd);}}
#define LOAD(lib,result,name,type) __typeof__((type)0) result=(type)dlsym(lib,name);if(!result){fprintf(stderr,"MISSING %s\n",name);return 2;}
int main(void) {
    alarm(20);flags("/usr/lib/libEGL-fb.so");flags("/usr/lib/libGLESv2-fb.so.2.0.0");
    void *egl=dlopen("/usr/lib/libEGL-fb.so",RTLD_NOW|RTLD_LOCAL),*gles=dlopen("/usr/lib/libGLESv2-fb.so.2.0.0",RTLD_NOW|RTLD_LOCAL);
    if(!egl||!gles){fprintf(stderr,"GPU_LIBRARY %s\n",dlerror());return 2;}
    error_code=(unsigned(*)(void))dlsym(egl,"eglGetError");
    LOAD(egl,get_display,"eglGetDisplay",Display(*)(void *));
    LOAD(egl,initialize,"eglInitialize",Bool(*)(Display,Int *,Int *));
    LOAD(egl,query,"eglQueryString",const char *(*)(Display,Int));
    LOAD(egl,choose,"eglChooseConfig",Bool(*)(Display,const Int *,Config *,Int,Int *));
    LOAD(egl,bind,"eglBindAPI",Bool(*)(unsigned));
    LOAD(egl,pbuffer,"eglCreatePbufferSurface",Surface(*)(Display,Config,const Int *));
    LOAD(egl,context,"eglCreateContext",Context(*)(Display,Config,Context,const Int *));
    LOAD(egl,current,"eglMakeCurrent",Bool(*)(Display,Surface,Surface,Context));
    LOAD(egl,destroy_context,"eglDestroyContext",Bool(*)(Display,Context));
    LOAD(egl,destroy_surface,"eglDestroySurface",Bool(*)(Display,Surface));
    LOAD(egl,terminate,"eglTerminate",Bool(*)(Display));
    void *(*native_display)(int)=(void *(*)(int))dlsym(egl,"fbGetDisplayByIndex");
    void *native=native_display?native_display(0):NULL;
    Display display=get_display(native);Int major=0,minor=0;
    if(!step(display&&initialize(display,&major,&minor),"EGL_INITIALIZE"))return 3;
    printf("EGL_INFO %d.%d vendor=%s version=%s\n",major,minor,query(display,0x3053),query(display,0x3054));
    Int attributes[]={0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3026,8,0x3038};
    Config config=NULL;Int count=0;
    if(!step(choose(display,attributes,&config,1,&count)&&count>0,"DEPTH24_STENCIL8_CONFIG"))return 4;
    if(!step(bind(0x30a0),"BIND_GLES"))return 4;
    Int surface_attributes[]={0x3057,64,0x3056,64,0x3038},context_attributes[]={0x3098,2,0x3038};
    Surface surface=pbuffer(display,config,surface_attributes);Context ctx=context(display,config,NULL,context_attributes);
    if(!step(surface&&ctx&&current(display,surface,surface,ctx),"OFFSCREEN_GLES_CONTEXT"))return 5;
    LOAD(gles,get_string,"glGetString",const unsigned char *(*)(unsigned));
    LOAD(gles,clear_color,"glClearColor",void(*)(float,float,float,float));
    LOAD(gles,clear,"glClear",void(*)(unsigned));
    LOAD(gles,read_pixels,"glReadPixels",void(*)(Int,Int,Int,Int,unsigned,unsigned,void *));
    LOAD(gles,create_shader,"glCreateShader",unsigned(*)(unsigned));
    LOAD(gles,shader_source,"glShaderSource",void(*)(unsigned,Int,const char **,const Int *));
    LOAD(gles,compile_shader,"glCompileShader",void(*)(unsigned));
    LOAD(gles,get_shader,"glGetShaderiv",void(*)(unsigned,unsigned,Int *));
    LOAD(gles,delete_shader,"glDeleteShader",void(*)(unsigned));
    printf("GL_INFO vendor=%s renderer=%s version=%s glsl=%s\n",get_string(0x1f00),get_string(0x1f01),get_string(0x1f02),get_string(0x8b8c));
    const char *extensions=(const char *)get_string(0x1f03);
    printf("GL_CAPS packed_depth_stencil=%d npot=%d\n",extensions&&strstr(extensions,"GL_OES_packed_depth_stencil")!=NULL,extensions&&strstr(extensions,"GL_OES_texture_npot")!=NULL);
    const char *sources[]={"attribute vec4 position; void main(){gl_Position=position;}","precision mediump float; void main(){gl_FragColor=vec4(1.0,0.25,0.5,1.0);}"};
    for(unsigned i=0;i<2;i++){unsigned shader=create_shader(i?0x8b30:0x8b31);shader_source(shader,1,&sources[i],NULL);compile_shader(shader);Int ok=0;get_shader(shader,0x8b81,&ok);delete_shader(shader);if(!step(ok,i?"FRAGMENT_SHADER":"VERTEX_SHADER"))return 6;}
    clear_color(1,.25f,.5f,1);clear(0x4000|0x0100|0x0400);unsigned char pixels[64*64*4];read_pixels(0,0,64,64,0x1908,0x1401,pixels);
    printf("PIXEL_RGBA %u %u %u %u\n",pixels[0],pixels[1],pixels[2],pixels[3]);
    int ok=pixels[0]>=250&&pixels[1]>=60&&pixels[1]<=70&&pixels[2]>=120&&pixels[2]<=135&&pixels[3]>=250;
    current(display,NULL,NULL,NULL);destroy_context(display,ctx);destroy_surface(display,surface);terminate(display);
    return step(ok,"GPU_READBACK")?0:7;
}
