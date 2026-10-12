"""Replay the live countdown frame through the actual render entry with a busy UI lock."""
from pathlib import Path
import argparse,struct,subprocess,tempfile
from PIL import Image
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--frame',type=Path,required=True)
p.add_argument('--capture',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent
source=(root.parent/'ui_runtime.c').read_text()
start=source.index('__attribute__((visibility("default"))) int xz_mods_render_v1(')
render=source[start:source.index('\n}',start)+2]
image=Image.open(a.frame).convert('RGB');assert image.size==(800,480)
raw=bytearray(b''.join(struct.pack('<H',(r>>3)<<11|(g>>2)<<5|b>>3) for r,g,b in image.getdata()))
captured=a.capture.read_bytes();step=12+110*35*2*2
# Frame36 in the initial capture contains stock Bars on both decks.
native=bytearray(raw)
for deck in range(2):
    region=captured[32+36*step+12+deck*7700:32+36*step+12+(deck+1)*7700]
    for row in range(35):
        offset=((119+deck*143+row)*800+4)*2
        native[offset:offset+220]=region[row*220:(row+1)*220]
with tempfile.TemporaryDirectory(prefix='xz-key-stability-') as folder:
    folder=Path(folder);(folder/'native.rgb565').write_bytes(native)
    cache=''
    if (root/'key_frame_cache.h').exists():
        cache='#include "key_frame_cache.h"\nstatic _Thread_local struct xz_key_frame_cache key_frame_cache;\n'
    if 'badge_frame_cache;' in source:
        begin=source.index('static _Thread_local struct {',source.index('static _Thread_local struct xz_key_frame_cache'))
        cache+=source[begin:source.index('badge_frame_cache;',begin)+len('badge_frame_cache;')]+'\n'
    proof='struct { unsigned version,busy,cached; } xz_mods_key_present_proof_v1={1,0,0};\n'
    program='''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "key_strip.h"
#include "../arcade/game_nav.h"
#include "../../vendor/tools/xz_runtime/mods_bridge.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x);exit(1);} } while(0)
static int started=1,ui_mutex,forced_busy,visible,fb_takeover_active,network_enabled,network_connected;
static struct { int visible; } touch;
static struct xz_ui_model model;
static struct xz_ui ui;
static struct xz_key_strip_deck key_strip[2];
static struct { unsigned version,enabled; } xz_mods_key_feedback_proof_v1={1,1};
static char device_ip[16];
static int arcade_present;
static struct { int phase,mode,live,clock_deck,bpm,x,y; } arcade;
static struct { unsigned frames;int phase,mode,live,deck,bpm,x,y; } xz_arcade_proof_v1;
static int pthread_mutex_trylock(int *m){(void)m;return forced_busy;}
static int pthread_mutex_unlock(int *m){(void)m;return 0;}
static void refresh(void){}
static int xz_read_memory(uint32_t a,void*p,size_t n){(void)a;(void)p;(void)n;return -1;}
static int shortcut_visible(void){return 0;}
static int shortcut_stock_rgb(void){return 0;}
static int xz_native_play_visible(void){return 0;}
uint32_t xz_ui_model_stem_color(const struct xz_ui_model*m,int i){(void)m;(void)i;return 0;}
void xz_ui_render_shortcut_wave(uint16_t*p,size_t s,int i,const uint32_t*c){(void)p;(void)s;(void)i;(void)c;}
void xz_ui_render_native_buttons(uint16_t*p,size_t s,int t,int l,int v,int a){(void)p;(void)s;(void)t;(void)l;(void)v;(void)a;}
static int xz_rhythm_lab_present(void){return 0;}
static int xz_wave_rider_present(void){return 0;}
unsigned xz_game_milliseconds(void){return 1;}
static int xz_rhythm_lab_draw(uint16_t*p,size_t n,size_t s,unsigned t,struct xz_game_targets*g){(void)p;(void)n;(void)s;(void)t;(void)g;return 0;}
static int xz_wave_rider_draw(uint16_t*p,size_t n,size_t s,unsigned t,struct xz_game_targets*g){(void)p;(void)n;(void)s;(void)t;(void)g;return 0;}
static struct xz_game_nav game_nav;static struct xz_game_targets game_frame;
enum game{GAME_NONE,GAME_ARCADE};static enum game game_locked(void){return arcade_present?GAME_ARCADE:GAME_NONE;}
static void arcade_tick(void){}
static int xz_arcade_render_targets(void*c,uint16_t*p,size_t n,size_t s,struct xz_game_targets*g){(void)c;(void)p;(void)n;(void)s;(void)g;return 0;}
int xz_ui_render(const struct xz_ui*u,const struct xz_ui_model*m,uint16_t*p,size_t n,size_t s){(void)u;(void)m;(void)p;(void)n;(void)s;return 1;}
static void badge(uint16_t *pixels,size_t stride){for(int d=0;d<2;d++)xz_key_strip_render_deck(pixels,stride,model.theme,d,&key_strip[d],model.key_feedback);}
'''+cache+proof+render+'''
static void load(uint16_t*p,const char*path){FILE*f=fopen(path,"rb");CHECK(f);CHECK(fread(p,800*480*2,1,f)==1);CHECK(!fclose(f));}
static int equal_keys(const uint16_t*a,const uint16_t*b){for(int d=0;d<2;d++)for(int y=119+d*143;y<154+d*143;y++)if(memcmp(a+y*800+4,b+y*800+4,107*2))return 0;return 1;}
int main(int argc,char**argv){
 CHECK(argc==2);uint16_t*p=malloc(800*480*2),*expected=malloc(800*480*2),*stock=malloc(800*480*2);CHECK(p&&expected&&stock);
 load(stock,argv[1]);memcpy(p,stock,800*480*2);model.key_feedback=1;
 CHECK(xz_mods_render_v1(p,800,480,800,NULL)==1);CHECK(!equal_keys(p,stock));memcpy(expected,p,800*480*2);
 memcpy(p,stock,800*480*2);forced_busy=1;
 CHECK(xz_mods_render_v1(p,800,480,800,NULL)==1);CHECK(equal_keys(p,expected));
 xz_mods_key_feedback_proof_v1.enabled=0;memcpy(p,stock,800*480*2);CHECK(!xz_mods_render_v1(p,800,480,800,NULL));CHECK(equal_keys(p,stock));xz_mods_key_feedback_proof_v1.enabled=1;
 visible=1;memcpy(p,stock,800*480*2);CHECK(!xz_mods_render_v1(p,800,480,800,NULL));CHECK(equal_keys(p,stock));visible=0;
 fb_takeover_active=1;CHECK(!xz_mods_render_v1(p,800,480,800,NULL));CHECK(equal_keys(p,stock));fb_takeover_active=0;
 forced_busy=0;model.key_feedback=0;memcpy(p,stock,800*480*2);xz_mods_render_v1(p,800,480,800,NULL);
 forced_busy=1;CHECK(!xz_mods_render_v1(p,800,480,800,NULL));CHECK(equal_keys(p,stock));
 CHECK(!xz_mods_render_v1(p,799,480,800,NULL));
 free(stock);free(expected);free(p);puts("PASS recorded native countdown survives busy renderer; panel/takeover/off gates remain native");return 0;
}
'''
    path=folder/'stability.c';path.write_bytes(program.encode());exe=folder/'stability.exe'
    subprocess.run([str(a.zig),'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-I',str(root),
                    str(path),str(root/'key_strip.c'),str(root/'ui_draw.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(folder/'native.rgb565')],check=True)
