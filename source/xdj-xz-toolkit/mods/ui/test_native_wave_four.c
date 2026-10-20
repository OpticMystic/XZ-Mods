/* SPDX-License-Identifier: MIT */
#include "native_wave.h"
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Four-deck native wave acceptance requires active assertions"
#endif
static uint16_t pixels[XZ_NATIVE_WAVE_PIXELS+8],scratch[XZ_NATIVE_WAVE_PIXELS+8],two_deck[XZ_STEM_LAYOUT_COUNT][XZ_NATIVE_WAVE_PIXELS];
static int extra_calls,style_calls,order_drawn;
static void tint(uint16_t *p,uint32_t count){style_calls++;for(uint32_t i=0;i<count;i++)p[i]^=0xffff;}
/* Paints the DECK 3/4 lanes so the test can see them, and checks the native lanes already moved. */
static void extra(uint16_t *p,uint32_t count){
    assert(count==XZ_NATIVE_WAVE_PIXELS&&style_calls==0);extra_calls++;
    struct xz_four_layout g;assert(xz_four_layout_get(order_drawn,&g));
    for(int d=2;d<4;d++)for(unsigned y=g.lane_y[d];y<g.lane_y[d]+XZ_FOUR_LANE_HEIGHT;y++){
        assert(p[y*536]==0);
        for(unsigned x=0;x<536;x++)p[y*536+x]=(uint16_t)(0xd000+d);
    }
}
static int rows_drawn(void *context,uint16_t *out,size_t count,size_t stride,int width,int height){
    (void)context;(void)stride;(void)width;(void)height;for(size_t i=0;i<count;i++)out[i]=0xabcd;return 1;
}
static struct xz_native_wave_scene scene(void){
    return (struct xz_native_wave_scene){1,0,0x4d200c,0x4d1f30,0x1fc2fc,0x1fdbe4,
        2,1,100,200,536,268,9,0x41000000,131,18,667,286,1};
}
static void fill(void){
    for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x++)pixels[y*536+x]=(uint16_t)(y+1);
    for(unsigned i=0;i<8;i++)pixels[XZ_NATIVE_WAVE_PIXELS+i]=scratch[XZ_NATIVE_WAVE_PIXELS+i]=0xdead;
}
static void guards(void){for(unsigned i=0;i<8;i++)assert(pixels[XZ_NATIVE_WAVE_PIXELS+i]==0xdead&&scratch[XZ_NATIVE_WAVE_PIXELS+i]==0xdead);}
static int two(struct xz_native_wave_pair *pair,const struct xz_native_wave_scene *s,int layout){
    assert(xz_native_wave_capture(pair,s,0x1fd058,7,pixels,1072));
    return xz_native_wave_finish_styled(pair,s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,rows_drawn,NULL,tint,layout);
}
static int four(struct xz_native_wave_pair *pair,const struct xz_native_wave_scene *s,int order,xz_native_wave_style style){
    assert(xz_native_wave_capture(pair,s,0x1fd058,7,pixels,1072));
    order_drawn=order;
    return xz_native_wave_finish_four(pair,s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,order,extra,style);
}
int main(void){
    struct xz_native_wave_pair pair={0};struct xz_native_wave_scene s=scene();
    for(int layout=0;layout<XZ_STEM_LAYOUT_COUNT;layout++){
        fill();style_calls=0;assert(two(&pair,&s,layout));
        memcpy(two_deck[layout],pixels,sizeof(two_deck[layout]));
    }
    for(int order=0;order<XZ_FOUR_ORDER_COUNT;order++){
        struct xz_four_layout g;assert(xz_four_layout_get(order,&g));
        for(int styled=0;styled<2;styled++){
            fill();extra_calls=style_calls=0;
            assert(four(&pair,&s,order,styled?tint:NULL)&&extra_calls==1&&style_calls==styled);
            assert(!pair.captured&&!pair.pixels);guards();
            for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x++){
                int d=xz_four_layout_lane_at(&g,(int)y);unsigned src=xz_four_layout_source_row(&g,y);
                uint16_t want=d>=2?(uint16_t)(0xd000+d):src==132?0:(uint16_t)(src+1);
                if(styled)want^=0xffff;
                assert(pixels[y*536+x]==want);
            }
        }
    }
    /* Route down: every saved stem layout comes back exactly, whatever ran in between. */
    for(int layout=0;layout<XZ_STEM_LAYOUT_COUNT;layout++){
        fill();style_calls=0;assert(four(&pair,&s,XZ_FOUR_ORDER,tint));
        fill();style_calls=0;assert(two(&pair,&s,layout));
        assert(!memcmp(pixels,two_deck[layout],sizeof(two_deck[layout])));guards();
    }
    /* Every changed scene or lock identity rejects before touching the frame or calling out. */
    for(int mode=0;mode<12;mode++){
        fill();s=scene();assert(xz_native_wave_capture(&pair,&s,0x1fd058,7,pixels,1072));
        uintptr_t thread=7,hw=200;size_t count=XZ_NATIVE_WAVE_PIXELS;uint16_t *buffer=scratch;
        int order=XZ_FOUR_ORDER;xz_native_wave_style draw=extra;
        switch(mode){
        case 0:s.enabled=0;break;case 1:s.scene_index=13;break;case 2:s.gr++;break;
        case 3:s.lifecycle_epoch++;break;case 4:s.flags=0x40000000;break;case 5:thread++;break;
        case 6:hw++;break;case 7:count--;break;case 8:buffer=pixels+1;break;
        case 9:order=XZ_FOUR_ORDER_COUNT;break;case 10:order=-1;break;case 11:draw=NULL;break;
        }
        extra_calls=style_calls=0;
        assert(!xz_native_wave_finish_four(&pair,&s,thread,hw,buffer,count,order,draw,tint));
        assert(!pair.captured&&!pair.pixels&&!extra_calls&&!style_calls);
        for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x+=67)assert(pixels[y*536+x]==y+1);
    }
    s=scene();
    assert(!xz_native_wave_finish_four(&pair,&s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,XZ_FOUR_ORDER,extra,tint));
    assert(!xz_native_wave_finish_four(NULL,&s,7,200,scratch,XZ_NATIVE_WAVE_PIXELS,XZ_FOUR_ORDER,extra,tint));
    puts("PASS four-deck native wave: both orders, extra before theme, every stem layout restored after the route, paired ownership and transactional failures");
}
