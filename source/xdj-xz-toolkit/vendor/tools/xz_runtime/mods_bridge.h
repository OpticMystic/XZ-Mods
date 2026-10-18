#ifndef XZ_MODS_BRIDGE_H
#define XZ_MODS_BRIDGE_H
#include <stdint.h>
#define XZ_MODS_BRIDGE_V1 1u

struct xz_vj_connection_v1 {
    uint32_t version;
    uint32_t size;
    uint32_t listening;
    uint32_t connected;
    uint32_t discovery;
    uint32_t active;
    uint32_t stats_valid;
    uint32_t frame_hz_milli;
    char ipv4[16];
};

typedef int (*xz_mods_visible_fn_v1)(void);
typedef int (*xz_mods_native_touch_fn_v1)(void);
typedef int (*xz_mods_render_fn_v1)(uint16_t *, uint32_t, uint32_t, uint32_t,
                                   const struct xz_vj_connection_v1 *);
typedef int (*xz_mods_takeover_fn_v1)(void);
typedef uint32_t (*xz_mods_native_color_fn_v1)(uint32_t rgba);
typedef uint32_t (*xz_mods_native_surface_color_fn_v1)(uint32_t surface,uint32_t rgba);
typedef int (*xz_mods_native_style_fn_v1)(void);
/* Optional, lock-free: 1 when the next render that draws writes every pixel (the FLAGSHIP shell owns
   the screen), so the presenter does not copy its base frame underneath first. */
typedef int (*xz_mods_full_frame_fn_v1)(void);

/* Optional mod exports, resolved dynamically. Zero from render means no draw. */
int xz_mods_visible_v1(void);
int xz_mods_native_touch_v1(void);
int xz_mods_render_v1(uint16_t *pixels, uint32_t width, uint32_t height,
                    uint32_t stride_pixels, const struct xz_vj_connection_v1 *connection);
int xz_mods_takeover_v1(void);
int xz_mods_full_frame_v1(void);
/* Receiver export: calibrated native press/release edges, unchanged VJTE wire. */
void xz_vj_touch_v1(int down, int x, int y);
#endif
