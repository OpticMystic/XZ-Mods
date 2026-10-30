"""Exercise the real primary Flip hook against a two-buffer dirty-update model."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT.parent/'vendor/tools/xz_runtime/xz_directfb_hook.c').read_text()
signature='static DFBResult hooked_primary_flip('
start=source.index(signature);opening=source.index('{',start);end=opening+1;depth=1
while depth:
 depth+=(source[end]=='{')-(source[end]=='}');end+=1
body=source[start:end]
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Flip acceptance requires active assertions
#endif
typedef int DFBResult;typedef unsigned DFBSurfaceFlipFlags;
typedef struct {int x1,y1,x2,y2;} DFBRegion;
typedef struct {int unused;} IDirectFBSurface;
struct fake_dfb {void (*WaitIdle)(struct fake_dfb*);};
static struct fake_dfb *directfb_interface;
#define DFB_OK 0
#define DSFLIP_BLIT 2u
#define DSFLIP_WAITFORSYNC 5u
static int framebuffer_presenter_started,primary_60hz_presenter_started;
static int open_panel,draws=1;
static void *mods_render=(void*)1;
static uint32_t flip_count;
static uint16_t buffers[2][8];static int front,back=1;
static unsigned last_flags;static const DFBRegion *last_region;
static int mods_panel_visible(void){return open_panel;}
static int overlay_is_active(void){return open_panel;}
static void draw_probe_overlay(IDirectFBSurface *s){(void)s;}
static int draw_mods_on_surface(IDirectFBSurface *s){(void)s;if(draws)buffers[back][7]=0xff00;return draws;}
static int original_primary_flip(IDirectFBSurface *s,const DFBRegion *region,unsigned flags){
 (void)s;last_flags=flags;last_region=region;
 if(flags&DSFLIP_BLIT)memcpy(buffers[front],buffers[back],sizeof(buffers[front]));
 else {int old=front;front=back;back=old;}
 return 19;
}
static void write_runtime_stats_if_due(void){}
/* Helpers the hook gained with the kart primary-display diagnostic. The regression models the stock
   primary (no diagnostic writer), so the native-primary branch is off and its counters are inert. */
static void capture_mods_native(IDirectFBSurface *s){(void)s;}
static int diagnostic_primary_active(int overlay){(void)overlay;return 0;}
static uint32_t xz_kart_primary_display_proof_v1[16];
static uint32_t diagnostic_primary_logged;
static int diagnostic_writer_claim(unsigned owner){(void)owner;return 1;}
static void diagnostic_writer_release(void){}
'''
checks=r'''
int main(void){
 IDirectFBSurface surface={0};DFBRegion dirty={1,0,1,0};
 for(int buffer=0;buffer<2;buffer++)for(int i=0;i<8;i++)buffers[buffer][i]=0x1234;
 buffers[back][0]=0x7171;
 assert(hooked_primary_flip(&surface,&dirty,DSFLIP_BLIT)==19);
 assert(buffers[front][0]==0x7171);
 /* A tap causes a later small stock update. The earlier title/pixels must
    remain present; replacing BLIT with a buffer swap resurrects stale data. */
 buffers[back][1]=0x2222;
 assert(hooked_primary_flip(&surface,&dirty,DSFLIP_BLIT)==19);
 if(buffers[front][0]!=0x7171){
  fprintf(stderr,"FAIL stock title alternated to stale pixels: %04x, Flip flags=%u instead of preserving BLIT\n",buffers[front][0],last_flags);assert(0);
 }
 assert(last_flags&DSFLIP_BLIT);
 /* An idle/no-draw overlay must retain the original region and flags. */
 draws=0;assert(hooked_primary_flip(&surface,&dirty,DSFLIP_BLIT)==19);
 assert(last_region==&dirty&&last_flags==DSFLIP_BLIT);
 puts("PASS real Flip hook: stable stock pixels across tap redraws and preserved BLIT/dirty semantics");
}
'''
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--zig',type=Path,required=True);args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='xz-flip-') as folder:
 path=Path(folder)/'flip.c';path.write_text(fixture+body+checks)
 binary=Path(folder)/'flip.exe'
 subprocess.run([str(args.zig.resolve()),'cc','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
