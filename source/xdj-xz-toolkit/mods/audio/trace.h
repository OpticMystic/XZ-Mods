/* SPDX-License-Identifier: MIT */
#ifndef XZ_AUDIO_TRACE_H
#define XZ_AUDIO_TRACE_H
#include <stdint.h>
enum xz_audio_trace_stage { XZ_TRACE_DECODE, XZ_TRACE_STEMS, XZ_TRACE_STRETCH, XZ_TRACE_KEY, XZ_TRACE_STAGES };
struct xz_audio_trace_context {
    uint32_t epoch,rate,frames,flags,duration_us,aux0,aux1;
    int32_t position,result,cents;
    float levels[3];
    uint32_t prepared[10];
    uint32_t owner,manager,total_us;
    uint32_t mix[16];
};
uint64_t xz_audio_trace_time(void);
int xz_audio_trace_enabled(void);
int xz_audio_trace_source_sample(void);
void xz_audio_trace_record(unsigned deck,unsigned stage,const float *pcm,const struct xz_audio_trace_context *);
int xz_audio_trace_start(void);
void xz_audio_trace_stop(void);
#endif
