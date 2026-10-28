/* SPDX-License-Identifier: MIT */
#include "native_wave.h"
#include "wave_viewport.h"
#include <string.h>

static _Thread_local uint16_t rows[XZ_NATIVE_WAVE_ROWS_PIXELS];

int xz_native_wave_scene_valid(const struct xz_native_wave_scene *s) {
    return s && s->enabled && !s->panel_open &&
        s->window_table == 0x4d200cu && s->control_table == 0x4d1f30u &&
        s->control_set == 0x1fc2fcu && s->control_clear == 0x1fdbe4u &&
        s->scene_index == 2 && s->visible && s->gr && s->hw &&
        s->width == 536 && s->height == 268 && s->pixel_type == 9 &&
        (s->flags & 0x40000000u) != 0 &&
        s->x == 131 && s->y == 18 && s->right == 667 && s->bottom == 286;
}
void xz_native_wave_reset(struct xz_native_wave_pair *p) {
    if(p)memset(p,0,sizeof(*p));
}
int xz_native_wave_capture(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                           uintptr_t caller,uintptr_t thread,uint16_t *pixels,int pitch) {
    if(!p)return 0;
    xz_native_wave_reset(p);
    if(caller != 0x1fd058u || !xz_native_wave_scene_valid(s) || !(s->flags & 0x01000000u) || !pixels ||
        ((uintptr_t)pixels & 1u) || pitch != 1072)return 0;
    p->captured=1;p->thread=thread;p->scene=*s;p->pixels=pixels;return 1;
}
/* Consumes the pair and checks it still describes this locked window and a private scratch. */
static int claim(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,uintptr_t thread,
                 uintptr_t hw,uint16_t *scratch,size_t count,struct xz_native_wave_pair *saved) {
    *saved=*p;
    xz_native_wave_reset(p);
    if(!saved->captured || !xz_native_wave_scene_valid(s) || !(s->flags & 0x01000000u) || thread != saved->thread ||
        hw != saved->scene.hw || hw != s->hw || s->gr != saved->scene.gr ||
        s->lifecycle_epoch != saved->scene.lifecycle_epoch || !scratch ||
        ((uintptr_t)scratch & 1u) || count < XZ_NATIVE_WAVE_PIXELS)return 0;
    uintptr_t a=(uintptr_t)scratch,b=(uintptr_t)saved->pixels;
    size_t bytes=XZ_NATIVE_WAVE_PIXELS*sizeof(*scratch);
    return a<=UINTPTR_MAX-bytes && b<=UINTPTR_MAX-bytes && !(a<b+bytes && b<a+bytes);
}
int xz_native_wave_finish_styled(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,
                         xz_native_wave_render render,void *context,xz_native_wave_style style,int inline_rows) {
    return xz_native_wave_finish_styled_after(p,s,thread,hw,scratch,count,render,context,style,inline_rows,NULL);
}
int xz_native_wave_finish_styled_after(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,
                         xz_native_wave_render render,void *context,xz_native_wave_style style,int inline_rows,
                         xz_native_wave_style after) {
    struct xz_native_wave_pair saved;
    if(!p || !claim(p,s,thread,hw,scratch,count,&saved) || (unsigned)inline_rows>=XZ_STEM_LAYOUT_COUNT ||
       (inline_rows&&!render)||(!inline_rows&&!style&&!after))return 0;
    size_t bytes=XZ_NATIVE_WAVE_PIXELS*sizeof(*scratch);
    memcpy(scratch,saved.pixels,bytes);
    if(style)style(scratch,XZ_NATIVE_WAVE_PIXELS);
    if(inline_rows){
        struct xz_wave_layout g;if(!xz_wave_layout_get(inline_rows,&g))return 0;
        size_t row_pixels=536*g.control_height;
        if(!xz_wave_layout_pixels(scratch,XZ_NATIVE_WAVE_PIXELS,536,inline_rows) ||
           !render(context,rows,row_pixels*2,536,536,(int)g.control_height*2))return 0;
        memcpy(scratch+536*g.control_y[0],rows,row_pixels*sizeof(*rows));
        memcpy(scratch+536*g.control_y[1],rows+row_pixels,row_pixels*sizeof(*rows));
    }
    if(after)after(scratch,XZ_NATIVE_WAVE_PIXELS);
    memcpy(saved.pixels,scratch,bytes);
    return 1;
}
int xz_native_wave_finish_four(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,int order,
                         xz_native_wave_style extra,xz_native_wave_style style) {
    struct xz_native_wave_pair saved;
    if(!p || !claim(p,s,thread,hw,scratch,count,&saved) || !extra)return 0;
    size_t bytes=XZ_NATIVE_WAVE_PIXELS*sizeof(*scratch);
    memcpy(scratch,saved.pixels,bytes);
    if(!xz_four_layout_pixels(scratch,XZ_NATIVE_WAVE_PIXELS,536,order))return 0;
    extra(scratch,XZ_NATIVE_WAVE_PIXELS);
    if(style)style(scratch,XZ_NATIVE_WAVE_PIXELS);
    memcpy(saved.pixels,scratch,bytes);
    return 1;
}
int xz_native_wave_finish_pair(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,
                         xz_native_wave_render render,void *context,int inline_rows,unsigned pair,
                         xz_native_wave_style halves,xz_native_wave_style style,xz_native_wave_style after) {
    struct xz_native_wave_pair saved;
    if(!p || !claim(p,s,thread,hw,scratch,count,&saved) || (unsigned)inline_rows>=XZ_STEM_LAYOUT_COUNT ||
       (inline_rows&&!render) || !(pair&3u) || (pair&~3u) || !halves)return 0;
    size_t bytes=XZ_NATIVE_WAVE_PIXELS*sizeof(*scratch);
    memcpy(scratch,saved.pixels,bytes);
    /* The software halves go in where the stock lanes are, so the one theme pass sees the whole surface in the
       stock geometry; only then does the native half take its stem layout and rows. */
    halves(scratch,XZ_NATIVE_WAVE_PIXELS);
    if(style)style(scratch,XZ_NATIVE_WAVE_PIXELS);
    unsigned native=~pair&3u;
    if(inline_rows&&native){
        struct xz_wave_layout g;if(!xz_wave_layout_get(inline_rows,&g))return 0;
        size_t row_pixels=536*g.control_height;
        if(!xz_wave_layout_pixels_decks(scratch,XZ_NATIVE_WAVE_PIXELS,536,inline_rows,native) ||
           !render(context,rows,row_pixels*2,536,536,(int)g.control_height*2))return 0;
        for(unsigned d=0;d<2;d++)if(native&(1u<<d))memcpy(scratch+536*g.control_y[d],rows+d*row_pixels,row_pixels*sizeof(*rows));
    }
    if(after)after(scratch,XZ_NATIVE_WAVE_PIXELS);
    memcpy(saved.pixels,scratch,bytes);
    return 1;
}
int xz_native_wave_finish(struct xz_native_wave_pair *p,const struct xz_native_wave_scene *s,
                         uintptr_t thread,uintptr_t hw,uint16_t *scratch,size_t count,
                         xz_native_wave_render render,void *context){
    return xz_native_wave_finish_styled(p,s,thread,hw,scratch,count,render,context,NULL,1);
}
