/* SPDX-License-Identifier: MIT */
#ifndef XZ_JOG_DISPLAY_RUNTIME_H
#define XZ_JOG_DISPLAY_RUNTIME_H
#include <stdint.h>
#include "extra_decks.h"
#include "jog_display.h"

int xz_jog_display_runtime_start(void);
void xz_jog_display_runtime_stop(void);
/* Existing connector only, after its snapshot. Bounded chunk/reduction budget. */
void xz_jog_display_runtime_service_assets(void);

/* Lock-free publisher called when extra deck state updates. */
void xz_jog_display_runtime_publish_deck(int deck_index,const struct xz_extra_deck *deck,uint32_t now_ms);
void xz_jog_display_runtime_publish(const struct xz_extra_decks *decks,uint32_t now_ms);

/* Convert a published extra deck state to a Jog LCD snapshot at now_ms. */
int xz_jog_display_snapshot_from_deck(int deck_index,const struct xz_extra_deck *deck,uint64_t now_ms,struct xz_jd_snapshot *out);

/* Test seam for exercising the JogCpuCom::assemble hook without hardware. */
struct xz_jog_display_test_ops {
 void (*assemble)(void *jog_cpu_com);
 uint64_t (*now_ms)(void);
 uint32_t (*control_revision)(void);
};
void xz_jog_display_runtime_test_install(const struct xz_jog_display_test_ops *originals,void (**assemble_hook_out)(void *));
const struct xz_jd *xz_jog_display_runtime_state(int side_index);
#endif
