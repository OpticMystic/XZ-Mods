#ifndef XZ_UI_TRANSIENT_NOTICE_H
#define XZ_UI_TRANSIENT_NOTICE_H
#include <stdint.h>
#include "ui_skin.h"
#include <string.h>
enum { XZ_UI_NOTICE_BYTES=64, XZ_UI_NOTICE_MS=3500 };
struct xz_ui_transient_notice { uint32_t at_ms; char text[XZ_UI_NOTICE_BYTES]; };
/* Caller owns the UI lock; cached renderers consume their own value copy. */
static inline void xz_ui_notice_set(struct xz_ui_transient_notice *out,const char *text,uint32_t now){
 unsigned i=0;
 if(text)for(;i<XZ_UI_NOTICE_BYTES-1&&text[i];i++)out->text[i]=text[i];
 for(;i<XZ_UI_NOTICE_BYTES;i++)out->text[i]=0;
 out->at_ms=now;
}
static inline const char *xz_ui_notice_text(const struct xz_ui_transient_notice *notice,uint32_t now){
 return notice->text[0]&&(uint32_t)(now-notice->at_ms)<XZ_UI_NOTICE_MS?notice->text:0;
}
/* Restore only our exact last paint. Fresh native pixels always win. */
struct xz_ui_notice_pixels { uint16_t base[24][56],paint[24][56]; int valid; };
static inline void xz_ui_notice_restore(struct xz_ui_notice_pixels *saved,uint16_t *pixels,size_t stride){
 if(!saved->valid)return;
 saved->valid=0;
 for(unsigned y=0;y<24;y++)if(memcmp(pixels+y*stride+744,saved->paint[y],sizeof saved->paint[y]))return;
 for(unsigned y=0;y<24;y++)memcpy(pixels+y*stride+744,saved->base[y],sizeof saved->base[y]);
}
/* The existing MODS target opens the panel where ui.notice holds the detail. */
static inline void xz_ui_notice_badge(uint16_t *pixels,size_t stride,int theme,
 const struct xz_ui_transient_notice *notice,uint32_t now,struct xz_ui_notice_pixels *saved){
 if(!xz_ui_notice_text(notice,now))return;
 for(unsigned y=0;y<24;y++)memcpy(saved->base[y],pixels+y*stride+744,sizeof saved->base[y]);
 struct xz_canvas c={pixels,stride,800,480,theme};
 xz_round_rect(c,744,0,56,24,12,15,0x98251f);
 xz_text(c,748,8,"LOAD ERR",1,8,0xffffff);
 for(unsigned y=0;y<24;y++)memcpy(saved->paint[y],pixels+y*stride+744,sizeof saved->paint[y]);
 saved->valid=1;
}
#endif
