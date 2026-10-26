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
/* Restore only our exact last paint. Fresh native pixels always win. The badge covers MODS (top_bar.h). */
struct xz_ui_notice_pixels { uint16_t base[XZ_TOP_BAR_H][XZ_TOP_MODS_W],paint[XZ_TOP_BAR_H][XZ_TOP_MODS_W]; int valid; };
static inline void xz_ui_notice_restore(struct xz_ui_notice_pixels *saved,uint16_t *pixels,size_t stride){
 if(!saved->valid)return;
 saved->valid=0;
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)if(memcmp(pixels+y*stride+XZ_TOP_MODS_X,saved->paint[y],sizeof saved->paint[y]))return;
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)memcpy(pixels+y*stride+XZ_TOP_MODS_X,saved->base[y],sizeof saved->base[y]);
}
/* The existing MODS target opens the panel where ui.notice holds the detail. */
static inline void xz_ui_notice_badge(uint16_t *pixels,size_t stride,int theme,
 const struct xz_ui_transient_notice *notice,uint32_t now,struct xz_ui_notice_pixels *saved){
 if(!xz_ui_notice_text(notice,now))return;
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)memcpy(saved->base[y],pixels+y*stride+XZ_TOP_MODS_X,sizeof saved->base[y]);
 struct xz_canvas c=xz_top_bar_canvas(pixels,stride,theme);
 xz_round_rect(c,XZ_TOP_MODS_X,0,XZ_TOP_MODS_W,XZ_TOP_BAR_H,XZ_TOP_BAR_H/2,15,0x98251f);
 xz_top_bar_label(c,XZ_TOP_MODS_X,XZ_TOP_MODS_W,"LOAD ERR",0x98251f,0xffffff);
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)memcpy(saved->paint[y],pixels+y*stride+XZ_TOP_MODS_X,sizeof saved->paint[y]);
 saved->valid=1;
}
#endif
