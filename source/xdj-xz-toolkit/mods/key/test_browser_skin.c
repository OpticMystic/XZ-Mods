/* SPDX-License-Identifier: MIT */
#define main existing_skin_test
#include "../ui/test_native_skin_runtime.c"
#undef main
#define changed browser_changed
#define running browser_running
#define worker browser_worker
#include "browser.c"
#undef changed
#undef running
#undef worker
#include <sys/mman.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
static unsigned draws;
static unsigned char window_fixture[0xa0];
static const uint16_t native_title[]={'N','i','g','h','t',' ','D','r','i','v','e',0};
static uint32_t draw_spy(void *window,void *font,const uint16_t *text,uint32_t meta,
    uint32_t a,uint32_t b,const void *clip,int32_t x,int32_t y,uint32_t flags,uint32_t language){
    assert(window==window_fixture&&font==(void*)3&&clip==(void*)4&&x==12&&y==17&&a==19&&b==23&&flags==29&&language==31);
    assert(text==native_title&&meta==0x1000b&&!key_prefix_color);draws++;return 0x1234;
}
static uint32_t line_spy(void *window,void *font,const uint16_t *text,uint32_t meta,
    uint32_t a,uint32_t b,const void *clip,int32_t x,int32_t y,uint32_t flags,uint32_t language){
    (void)window;(void)font;(void)a;(void)b;(void)clip;(void)x;(void)y;(void)flags;(void)language;
    assert((meta&65535)==2&&text[0]=='9'&&text[1]=='A'&&!text[2]);draws++;return 0x9a;
}
/* Stock text is never rewritten: rows and key lines come from deferred setters, so
   the key and header UI lives on the final frame (ui/key_strip.c). */
int main(int argc,char **argv){
    assert(argc==2);existing_skin_test();
    assert(!xz_key_catalog_load(argv[1],&catalogs[0]));
    struct xz_playing_key current={.key={0,0}};xz_key_browser_master(&current,XZ_KEY_CAMELOT);
    xz_key_browser_scope(1,1,XZ_KEY_CAMELOT,0);stock_wstring=draw_spy;memset(window_fixture+0x74,0x11,16);
    assert(wstring_hook(window_fixture,(void*)3,native_title,0x1000b,19,23,(void*)4,12,17,29,31)==0x1234);
    assert(draws==1&&!wstring_depth&&packing_exponent==0);
    for(unsigned i=0;i<16;i++)assert(window_fixture[0x74+i]==0x11);
    xz_key_browser_scope(0,0,0,-1);
    uint16_t stock[3]={'9','A',0};xz_key_line_publish(1,"> 10A -22c");stock_wstring=line_spy;
    assert(wstring_hook(window_fixture,(void*)5,stock,0x20002,1,2,(void*)6,401,233,3,4)==0x9a&&draws==2);
    xz_key_line_publish(1,"");
    xz_key_catalog_free(&catalogs[0]);puts("PASS native text wrapper: one stock draw, text, arguments and bounds unchanged, no title-cell packing");return 0;
}