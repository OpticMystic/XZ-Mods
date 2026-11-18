/* SPDX-License-Identifier: MIT */
#ifndef XZ_JOG_WAVE_DIAGNOSTIC_H
#define XZ_JOG_WAVE_DIAGNOSTIC_H
#ifndef XZ_JOG_WAVE_DIAGNOSTIC
#ifdef XZ_JOG_DISPLAY_PROOF
#define XZ_JOG_WAVE_DIAGNOSTIC 1
#else
#define XZ_JOG_WAVE_DIAGNOSTIC 0
#endif
#endif
#if XZ_JOG_WAVE_DIAGNOSTIC
#include <stdatomic.h>
#include <stdint.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"wave counters require lock-free atomics");
enum { JW_SERVICE, JW_INVALID, JW_BUSY, JW_CHANGED, JW_ABSENT, JW_OK,
 JW_ENCODED, JW_PUBLISHED, JW_SPAN_REJECT, JW_ADOPTED, JW_COMMITTED, JW_COUNTERS };
struct xz_jog_wave_diagnostic_v1 { uint32_t version,size; _Atomic unsigned count[2][JW_COUNTERS]; };
_Static_assert(sizeof(struct xz_jog_wave_diagnostic_v1)==96,"wave proof ABI");
__attribute__((visibility("default"))) struct xz_jog_wave_diagnostic_v1 xz_mods_jog_wave_diagnostic_v1={.version=1,.size=96};
/* Independent modulo-2^32 counters. COMMITTED counts owned software waveform
 * frames, not native replay, physical sends, or MCU acceptance. */
#define JW_INC(side,field) ((void)atomic_fetch_add_explicit(&xz_mods_jog_wave_diagnostic_v1.count[(side)][(field)],1u,memory_order_relaxed))
#define JW_COPY(side,result) JW_INC(side,JW_INVALID+(int)(result)+3)
#else
#define JW_INC(side,field) ((void)0)
#define JW_COPY(side,result) ((void)0)
#endif
#endif
