"""Run the actual header contact handler with native release payloads on Windows or Linux."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);a=p.parse_args()
source=(root.parent/'ui_runtime.c').read_text()
start=source.index('static int head_shown,')
end=source.index('static void touch_hook(',start)
handler=source[start:end]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "native_touch.h"
#include "key_strip.h"
#undef assert
#define assert(p) ((p)?(void)0:(fprintf(stderr,"FAIL %s line %d\n",#p,__LINE__),exit(1)))
static struct { int visible,capture; } touch;
static struct xz_ui_model model;
static struct { unsigned version,drawn[2],errors[2],head_drawn,head_taps,head_opened,head_failed; } xz_mods_key_strip_proof_v1;
static struct { unsigned version,releases; int down_x,down_y,up_x,up_y; } xz_mods_browse_tap_proof_v1;
static unsigned opens;
static int menu_ok=1;
static int open_menu(void){opens++;return menu_ok;}
static int (*xz_native_browse_open_sort)(void)=open_menu;
uint32_t xz_game_milliseconds(void){return 1000;}
'''
tests=r'''
static int event(int x,int y,int down,int previous){
    struct xz_touch_status s={.down=(uint8_t)down,.x=(uint32_t)x,.y=(uint32_t)y};
    return browse_head_touch(&s,previous);
}
int main(void){
    head_shown=1;
    assert(!event(620,10,1,0));
    model.metrics_enabled=1;assert(!event(430,16,1,0));model.metrics_enabled=0;
    assert(event(430,16,1,0));
    assert(event(0,0,0,0));
    assert(opens==1&&xz_mods_key_strip_proof_v1.head_opened==1);
    assert(!event(0,0,0,0)&&opens==1);
    assert(event(570,16,1,0));
    assert(event(570,16,1,0)&&opens==1);
    assert(event(0,0,0,0)&&opens==2);
    assert(event(430,16,1,0));
    assert(event(100,100,1,0));
    assert(event(430,16,1,0));
    assert(event(0,0,0,0)&&opens==2);
    assert(event(430,16,1,0));
    assert(event(570,16,1,0));
    assert(event(0,0,0,0)&&opens==2);
    assert(!event(100,100,1,0));
    assert(!event(430,16,1,1));
    touch.visible=1;assert(!event(430,16,1,0));touch.visible=0;
    touch.capture=1;assert(!event(430,16,1,0));touch.capture=0;
    head_shown=0;assert(!event(430,16,1,0));head_shown=1;
    menu_ok=-1;assert(event(430,16,1,0));assert(event(0,0,0,0));
    assert(opens==3&&xz_mods_key_strip_proof_v1.head_failed==1&&head_notice_until);
    printf("PASS production header: cleared release, one activation, drag-off/cross-cell cancel, stock contacts and failures\n");
}
'''
with tempfile.TemporaryDirectory(prefix='xz-header-contact-') as tmp:
    tmp=Path(tmp);c=tmp/'contact.c';c.write_text(prefix+handler+tests)
    exe=tmp/'contact.exe'
    subprocess.run([str(a.zig),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-Wno-unused-variable','-I',str(root),str(c),str(root/'key_strip.c'),str(root/'ui_draw.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
