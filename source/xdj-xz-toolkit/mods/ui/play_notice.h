/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_PLAY_NOTICE_H
#define XZ_UI_PLAY_NOTICE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "ui_skin.h"
#include "metrics.h"
#include "extra_lane.h"
#include "notice_policy.h"
/* CLASSIC's play-screen notice in the mods' top bar (top_bar.h), never over the waveform surface (owner
   2026-11-17: "I dont really like the blocking notices ... that block the waveforms"). It takes the METRICS
   readout's columns, x 280..591 on rows 0..17, hiding the readout while it shows; a sentence too wide for them
   reaches right over the METRICS button, to x 671. It answers no touch: METRICS keeps its own.
   The band is black stock pixels when METRICS is off, which the stock does not repaint each frame: the paint
   keeps the pixels it covered and puts them back once it is no longer drawn, unless fresh pixels came in
   (as ui/transient_notice.h does for the MODS badge). Restore first in a frame, draw last. */
enum {
 XZ_PLAY_NOTICE_X=XZ_METRICS_READOUT_X, XZ_PLAY_NOTICE_W=XZ_METRICS_READOUT_W,
 XZ_PLAY_NOTICE_WIDE_W=XZ_METRICS_X+XZ_METRICS_W-XZ_METRICS_READOUT_X, /* 280..671 */
 XZ_PLAY_NOTICE_PAD=8, XZ_PLAY_NOTICE_FILL=0x2b1d00, XZ_PLAY_NOTICE_EDGE=0xffa82e
};
/* What the paint covered, and a hash of the paint itself (not a copy: this lives once per drawing thread). */
struct xz_play_notice_pixels { uint16_t base[XZ_TOP_BAR_H][XZ_PLAY_NOTICE_WIDE_W]; uint64_t paint; int valid,w; };
static inline uint64_t xz_play_notice_hash(const uint16_t *pixels,size_t stride,int w){
 uint64_t h=1469598103934665603ull;
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)for(int x=0;x<w;x++){h^=pixels[y*stride+XZ_PLAY_NOTICE_X+(size_t)x];h*=1099511628211ull;}
 return h;
}
/* Which sentence shows: the route-up notice (ready, deck_view.h, NULL when none), else the players' notice while
   fresh (xz_extra_notice_active, 3.5 s); a quiet one (notice_policy.h) is skipped. Copied into out (bytes long). */
static inline const char *xz_play_notice_pick(const char *ready,const struct xz_extra_decks *s,uint32_t now_ms,
 char *out,size_t bytes){
 if(!out||!bytes)return NULL;
 out[0]=0;
 if(ready&&ready[0]&&!xz_notice_quiet(ready)){snprintf(out,bytes,"%s",ready);return out;}
 if(!xz_extra_notice_active(s,now_ms))return NULL;
 char text[sizeof s->notice];memcpy(text,s->notice,sizeof text);text[sizeof text-1]=0;
 if(xz_notice_quiet(text))return NULL;
 snprintf(out,bytes,"%s",text);
 return out;
}
/* The box a sentence takes: x is always XZ_PLAY_NOTICE_X; its width, 0 for no text. */
static inline int xz_play_notice_width(int theme,const char *text){
 if(!text||!text[0])return 0;
 struct xz_canvas m={NULL,800,800,480,theme};
 return xz_text_width(m,text,1,63)<=XZ_PLAY_NOTICE_W-2*XZ_PLAY_NOTICE_PAD?XZ_PLAY_NOTICE_W:XZ_PLAY_NOTICE_WIDE_W;
}
static inline void xz_play_notice_restore(struct xz_play_notice_pixels *saved,uint16_t *pixels,size_t stride){
 if(!saved||!pixels||!saved->valid)return;
 saved->valid=0;
 if(xz_play_notice_hash(pixels,stride,saved->w)!=saved->paint)return;
 size_t bytes=(size_t)saved->w*sizeof(uint16_t);
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)memcpy(pixels+y*stride+XZ_PLAY_NOTICE_X,saved->base[y],bytes);
}
/* Draws text (NULL or empty: nothing) and keeps what it covered in saved. 1 when drawn. */
static inline int xz_play_notice_draw(uint16_t *pixels,size_t stride,int theme,const char *text,struct xz_play_notice_pixels *saved){
 int w=xz_play_notice_width(theme,text);
 if(!pixels||stride<800||!saved||!w)return 0;
 size_t bytes=(size_t)w*sizeof(uint16_t);
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)memcpy(saved->base[y],pixels+y*stride+XZ_PLAY_NOTICE_X,bytes);
 struct xz_canvas c=xz_top_bar_canvas(pixels,stride,theme);
 xz_rect(c,XZ_PLAY_NOTICE_X,0,w,XZ_TOP_BAR_H,XZ_PLAY_NOTICE_FILL);
 xz_border(c,XZ_PLAY_NOTICE_X,0,w,XZ_TOP_BAR_H,XZ_PLAY_NOTICE_EDGE);
 xz_rect(c,XZ_PLAY_NOTICE_X,0,3,XZ_TOP_BAR_H,XZ_PLAY_NOTICE_EDGE);
 int tw=xz_text_width(c,text,1,63),room=w-2*XZ_PLAY_NOTICE_PAD;
 /* Clipped inside the border: a sentence wider than the wide box loses its end, never paints past x 671. */
 struct xz_canvas inner=xz_sub(c,XZ_PLAY_NOTICE_X+4,1,w-5,XZ_TOP_BAR_H-2);
 int x=(tw<room?(w-tw)/2:XZ_PLAY_NOTICE_PAD)-4;
 xz_text(inner,x,(xz_theme_pixel_text(theme)?XZ_TOP_BAR_PIXEL_Y:XZ_TOP_BAR_LABEL_Y)-1,text,1,63,0xffffff);
 saved->paint=xz_play_notice_hash(pixels,stride,w);saved->w=w;saved->valid=1;
 return 1;
}
#endif
