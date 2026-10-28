#ifndef XZ_NATIVE_RAW_SKIN_H
#define XZ_NATIVE_RAW_SKIN_H
#include <stdint.h>
/* Both callbacks take a GR window. The unlock callback must be DS_GR_UnlockWindow,
   not the lower-level DS_Task_UnlockWindow(HW) intercepted by the caller. */
int xz_native_raw_skin_start(int (*stock_lock)(void *,void **,int *),int (*stock_unlock)(void *));
void xz_native_raw_skin_locked(void *gr,void **pixels,int *pitch,uintptr_t caller,int result);
void xz_native_raw_skin_before_unlock(void *hw);
void xz_native_raw_skin_repaint(void);
/* Any thread: 1 while the loopback cable explains the player's E-8307 hub window (../hub_cause.h). The window
   shows the calm LOOPBACK CABLE CONNECTED card when next drawn or repainted; 0 gives it back its stock pixels. */
void xz_native_raw_skin_hub_calm(int calm);
struct xz_native_raw_skin_proof {
 uint32_t version,captures,markers,alerts,repaints,rejected,cache_full,unknown_colors,evictions;
};
extern struct xz_native_raw_skin_proof xz_native_raw_skin_v1;
#endif
