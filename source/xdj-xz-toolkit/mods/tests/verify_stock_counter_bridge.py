"""Check the real display-only backbuffer capture with a fake DirectFB surface."""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT.parent/'vendor/tools/xz_runtime/xz_directfb_hook.c').read_text()
start=source.index('static void capture_mods_native(')
opening=source.index('{',start);end=opening+1;depth=1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
body=source[start:end]
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifdef NDEBUG
#error Active assertions required
#endif
#define DFB_OK 0
#define DSLF_WRITE 2
#define DSPF_UNKNOWN 0
#define DSPF_RGB16 1
typedef int DFBSurfacePixelFormat;
typedef struct surface IDirectFBSurface;
struct surface {
 int (*GetSize)(IDirectFBSurface*,int*,int*);
 int (*GetPixelFormat)(IDirectFBSurface*,DFBSurfacePixelFormat*);
 int (*Lock)(IDirectFBSurface*,int,void**,int*);
 int (*Unlock)(IDirectFBSurface*);
};
static unsigned mask,locks,unlocks,captures;static int fail,odd,wrong;
static uint16_t native[800*480],before[800*480];
static unsigned need(void){return mask;}
static void capture(const uint16_t*p,uint32_t w,uint32_t h,uint32_t stride){assert(p==native&&w==800&&h==480&&stride==800);captures++;}
static unsigned (*mods_stock_counter_mask)(void)=need;
static void (*mods_native_frame)(const uint16_t*,uint32_t,uint32_t,uint32_t)=capture;
static int size(IDirectFBSurface*s,int*w,int*h){(void)s;*w=wrong?799:800;*h=480;return 0;}
static int format(IDirectFBSurface*s,DFBSurfacePixelFormat*f){(void)s;*f=DSPF_RGB16;return 0;}
static int lock(IDirectFBSurface*s,int flags,void**p,int*pitch){(void)s;assert(flags==DSLF_WRITE);locks++;*p=native;*pitch=odd?1601:1600;return fail;}
static int unlock(IDirectFBSurface*s){(void)s;unlocks++;return 0;}
'''
checks=r'''
int main(void){
 IDirectFBSurface s={size,format,lock,unlock};memset(native,0x35,sizeof native);memcpy(before,native,sizeof native);
 capture_mods_native(&s);assert(!locks&&!captures);
 mask=1;capture_mods_native(&s);assert(locks==1&&unlocks==1&&captures==1&&!memcmp(before,native,sizeof native));
 odd=1;capture_mods_native(&s);assert(captures==1&&unlocks==2);odd=0;
 fail=1;capture_mods_native(&s);assert(captures==1&&unlocks==2);fail=0;
 wrong=1;capture_mods_native(&s);assert(locks==3);wrong=0;
 mods_native_frame=0;capture_mods_native(&s);assert(locks==3);
 puts("PASS real DirectFB capture: native backbuffer, no writes, idle skip, stride/size/failure guards");return 0;
}
'''
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--zig',required=True);args=parser.parse_args()
# Capture must precede both background-presenter early-return paths.
flip=source[source.index('static DFBResult hooked_primary_flip('):]
assert flip.index('capture_mods_native(thiz)')<flip.index('if (framebuffer_presenter_started')
with tempfile.TemporaryDirectory(prefix='xz-counter-bridge-') as folder:
    c=Path(folder)/'capture.c';c.write_text(fixture+body+checks)
    binary=Path(folder)/'capture.exe'
    subprocess.run([args.zig,'cc','-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
