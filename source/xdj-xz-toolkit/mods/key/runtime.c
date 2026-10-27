/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include "keyshift.h"
#include "../audio/runtime.h"
#include "../runtime.h"
#include "../audio/trace.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define XZ_KEY_DECKS 2
#define XZ_TSM_OPERATE UINT32_C(0x00076284)
#define XZ_TSM_ACTIVE_OFFSET 0x10u

typedef int32_t (*xz_tsm_operate_fn)(void *, int32_t, float *, int32_t, int32_t);

struct xz_key_deck {
    void *manager;
    struct xz_key_shift shift;
    uint32_t generation;
    uint32_t rate;
    int applied_cents;
    int have_last_end;
    int32_t last_end;
};

static struct xz_key_deck decks[XZ_KEY_DECKS];
static int desired_cents[XZ_KEY_DECKS];
static uint32_t desired_epoch[XZ_KEY_DECKS];
static unsigned char request_writer[XZ_KEY_DECKS];
static int started;
static int observe_only;
__attribute__((visibility("default"))) struct xz_key_audio_proof xz_mods_key_audio_proof_v1={.version=1};
static xz_tsm_operate_fn original_operate;

static uint32_t active_engine(const void *manager)
{
    return __atomic_load_n((const uint32_t *)((const unsigned char *)manager +
                           XZ_TSM_ACTIVE_OFFSET), __ATOMIC_RELAXED);
}

static int32_t operate_hook(void *manager, int32_t foreground_position,
                            float *output, int32_t frames,
                            int32_t background_position)
{
    uint32_t active_before = 0, rate = 0, generation = 0;
    int deck = -1, desired, eligible;
    struct xz_key_deck *state = NULL;
    eligible = __atomic_load_n(&started, __ATOMIC_ACQUIRE) && manager && output &&
        frames > 0 && frames <= (int32_t)XZ_KEY_SHIFT_MAX_BLOCK_FRAMES;
    if (__atomic_load_n(&started, __ATOMIC_ACQUIRE) && manager &&
        (eligible || (xz_audio_trace_enabled() && output && frames > 0 && frames <= 65536))) {
        active_before = active_engine(manager);
        deck = xz_audio_output_deck(manager, &rate, &generation);
        if (deck >= 0 && deck < XZ_KEY_DECKS) state = &decks[deck];
    }
    uint64_t native_started=xz_audio_trace_enabled()?xz_audio_trace_time():0;
    struct xz_audio_batch batch;xz_audio_batch_begin(deck,foreground_position,&batch);
    int32_t result = original_operate(manager, foreground_position, output,
                                      frames, background_position);
    xz_audio_batch_end(&batch);
    uint64_t native_finished=native_started?xz_audio_trace_time():0;
    /* The stock no-active-DSP branch returns without writing output. Require a
     * stable nonzero engine and the same published owner snapshot across the
     * original call. Transition blocks are skipped and break continuity. */
    if (!eligible || !__atomic_load_n(&started, __ATOMIC_ACQUIRE) ||
        !state || !active_before ||
        active_before != active_engine(manager)) {
        if (state && eligible) state->have_last_end = 0;
        for(int i=0;i<XZ_KEY_DECKS;i++)if(&decks[i]==state||decks[i].manager==manager)
            __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[i].epoch,0,__ATOMIC_RELEASE);
        if(native_started&&state){
            struct xz_audio_trace_context trace={.epoch=generation,.rate=rate,.frames=frames>0?(uint32_t)frames:0,
                .flags=0x1000u|(!eligible?0x2000u:0u)|(!active_before?0x4000u:0u),
                .position=foreground_position,.result=result,.aux1=active_before,
                .duration_us=(uint32_t)(native_finished-native_started),.manager=(uint32_t)(uintptr_t)manager};
            xz_audio_trace_record((unsigned)deck,XZ_TRACE_STRETCH,NULL,&trace);
        }
        return result;
    }
    {
        uint32_t rate_after=0, generation_after=0;
        int deck_after = xz_audio_output_deck(manager, &rate_after,
                                              &generation_after);
        if (deck_after != deck || rate_after != rate ||
            generation_after != generation || rate != XZ_KEY_SHIFT_SAMPLE_RATE) {
            state->have_last_end = 0;
            __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].epoch,0,__ATOMIC_RELEASE);
            if(native_started){
                struct xz_audio_trace_context trace={.epoch=generation,.rate=rate,.frames=(uint32_t)frames,
                    .flags=0x8000u,.position=foreground_position,.result=result,.aux0=generation_after,.aux1=rate_after,
                    .duration_us=(uint32_t)(native_finished-native_started),.manager=(uint32_t)(uintptr_t)manager};
                xz_audio_trace_record((unsigned)deck,XZ_TRACE_STRETCH,NULL,&trace);
            }
            return result;
        }
    }
    if (__atomic_load_n(&observe_only, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].epoch,0,__ATOMIC_RELEASE);
        struct xz_audio_trace_context trace={.epoch=generation,.rate=rate,.frames=(uint32_t)frames,
            .flags=0x100u,.position=foreground_position,.result=result,.cents=0,
            .aux0=(uint32_t)background_position,.aux1=active_before,
            .duration_us=(uint32_t)(native_finished-native_started),.manager=(uint32_t)(uintptr_t)manager,
            .owner=native_started?__atomic_load_n((const uint32_t*)((const unsigned char*)manager+0x5c),__ATOMIC_RELAXED):0};
        if(native_started){
            xz_audio_trace_record((unsigned)deck,XZ_TRACE_STRETCH,output,&trace);
            trace.duration_us=0;trace.total_us=(uint32_t)(xz_audio_trace_time()-native_started);
            xz_audio_trace_record((unsigned)deck,XZ_TRACE_KEY,output,&trace);
        }
        return result;
    }
    uint32_t reset=0;
    if (state->generation != generation || state->rate != rate) {
        reset=1;
        xz_key_shift_init(&state->shift);
        state->generation = generation;
        state->rate = rate;
        state->applied_cents = INT_MIN;
        state->have_last_end = 0;
    } else if (state->have_last_end && foreground_position != state->last_end) {
        reset=2;
        /* Foreground position is the stream-continuity token. Background/slip
         * movement alone does not discard the warmed pitch history. */
        xz_key_shift_init(&state->shift);
        state->applied_cents = INT_MIN;
        state->have_last_end = 0;
    }
    uint32_t requested_epoch = __atomic_load_n(&desired_epoch[deck], __ATOMIC_ACQUIRE);
    desired = __atomic_load_n(&desired_cents[deck], __ATOMIC_ACQUIRE);
    if ((requested_epoch != generation && requested_epoch != UINT32_MAX) ||
        requested_epoch != __atomic_load_n(&desired_epoch[deck], __ATOMIC_ACQUIRE)) desired = 0;
    if (desired != state->applied_cents) {
        if (xz_key_shift_set_cents(&state->shift, desired) != 0) {
            __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].epoch,0,__ATOMIC_RELEASE);
            return result;
        }
        state->applied_cents = desired;
    }
    struct xz_audio_trace_context trace={.epoch=generation,.rate=rate,.frames=(uint32_t)frames,.flags=reset,
        .position=foreground_position,.result=result,.cents=desired,.aux0=(uint32_t)background_position,.aux1=active_before,
        .duration_us=(uint32_t)(native_finished-native_started),.manager=(uint32_t)(uintptr_t)manager,
        .owner=native_started?__atomic_load_n((const uint32_t*)((const unsigned char*)manager+0x5c),__ATOMIC_RELAXED):0};
    if(native_started)xz_audio_trace_record((unsigned)deck,XZ_TRACE_STRETCH,output,&trace);
    uint64_t key_started=native_started?xz_audio_trace_time():0;
    if(!__atomic_load_n(&observe_only,__ATOMIC_ACQUIRE))
        (void)xz_key_shift_process(&state->shift, output, (uint32_t)frames);
    if(native_started){trace.duration_us=(uint32_t)(xz_audio_trace_time()-key_started);trace.total_us=(uint32_t)(xz_audio_trace_time()-native_started);xz_audio_trace_record((unsigned)deck,XZ_TRACE_KEY,output,&trace);}
    uint32_t ratio_bits;memcpy(&ratio_bits,&state->shift.ratio,4);
    state->manager=manager;
    __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].epoch,0,__ATOMIC_RELEASE);
    __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].ratio_bits,ratio_bits,__ATOMIC_RELAXED);
    __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].cents,desired,__ATOMIC_RELAXED);
    __atomic_add_fetch(&xz_mods_key_audio_proof_v1.deck[deck].blocks,1,__ATOMIC_RELAXED);
    __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[deck].epoch,generation,__ATOMIC_RELEASE);
    state->last_end = result;
    state->have_last_end = 1;
    return result;
}

int xz_key_set_desired_semitones(int deck, int semitones)
{
    if (deck < 0 || deck >= XZ_KEY_DECKS ||
        semitones < XZ_KEY_SHIFT_MIN_SEMITONES ||
        semitones > XZ_KEY_SHIFT_MAX_SEMITONES) return -1;
    return xz_key_set_desired_cents(deck, UINT32_MAX, semitones * 100);
}

int xz_key_get_desired_semitones(int deck, int *semitones_out)
{
    if (deck < 0 || deck >= XZ_KEY_DECKS || !semitones_out) return -1;
    *semitones_out = __atomic_load_n(&desired_cents[deck], __ATOMIC_ACQUIRE) / 100;
    return 0;
}

static void publish_request(int deck,uint32_t epoch,int cents){
    __atomic_store_n(&desired_epoch[deck],0,__ATOMIC_RELEASE);
    __atomic_store_n(&desired_cents[deck],cents,__ATOMIC_RELEASE);
    __atomic_store_n(&desired_epoch[deck],epoch,__ATOMIC_RELEASE);
}
/* Control writers serialize here. The audio callback never takes this lock. */
static void lock_request(int deck){while(__atomic_test_and_set(&request_writer[deck],__ATOMIC_ACQUIRE)){}}
static void unlock_request(int deck){__atomic_clear(&request_writer[deck],__ATOMIC_RELEASE);}
int xz_key_set_desired_cents(int deck,uint32_t epoch,int cents){
    if(deck<0||deck>=XZ_KEY_DECKS||!epoch||cents < -1200||cents>1200)return -1;
    lock_request(deck);publish_request(deck,epoch,cents);unlock_request(deck);return 0;
}
int xz_key_clear_desired_cents(int deck,uint32_t epoch,int expected_cents){
    if(deck<0||deck>=XZ_KEY_DECKS||!epoch)return -1;
    lock_request(deck);
    int matches=__atomic_load_n(&desired_epoch[deck],__ATOMIC_ACQUIRE)==epoch&&
        __atomic_load_n(&desired_cents[deck],__ATOMIC_ACQUIRE)==expected_cents;
    if(matches)publish_request(deck,epoch,0);
    unlock_request(deck);return matches?0:-1;
}

int xz_key_get_desired_cents(int deck, uint32_t epoch, int *out)
{
    if (deck < 0 || deck >= XZ_KEY_DECKS || !out) return -1;
    uint32_t before = __atomic_load_n(&desired_epoch[deck], __ATOMIC_ACQUIRE);
    int cents = __atomic_load_n(&desired_cents[deck], __ATOMIC_ACQUIRE);
    *out = before == epoch && before == __atomic_load_n(&desired_epoch[deck], __ATOMIC_ACQUIRE) ? cents : 0;
    return 0;
}

int xz_key_runtime_start(void)
{
    static const unsigned char guard[8] =
        {0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    int i;
    if (__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return 0;
    if (!original_operate &&
        xz_hook_arm(XZ_TSM_OPERATE, guard, (void *)operate_hook,
                    (void **)&original_operate) != 0) return -1;
    for (i = 0; i < XZ_KEY_DECKS; ++i) {
        if(!__atomic_load_n(&observe_only,__ATOMIC_ACQUIRE))xz_key_shift_init(&decks[i].shift);
        decks[i].generation = 0;
        decks[i].manager = NULL;
        decks[i].rate = 0;
        decks[i].applied_cents = INT_MIN;
        decks[i].have_last_end = 0;
        __atomic_store_n(&xz_mods_key_audio_proof_v1.deck[i].epoch,0,__ATOMIC_RELEASE);
    }
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    return 0;
}
int xz_key_has_applied(int deck,uint32_t epoch){
    if((unsigned)deck>=XZ_KEY_DECKS||!epoch||!__atomic_load_n(&started,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&observe_only,__ATOMIC_ACQUIRE))return 0;
    return __atomic_load_n(&xz_mods_key_audio_proof_v1.deck[deck].epoch,__ATOMIC_ACQUIRE)==epoch;
}
int xz_key_get_applied_cents(int deck,uint32_t epoch,int *out)
{
    if(deck<0||deck>=XZ_KEY_DECKS||!epoch||!out)return -1;
    *out=0;
    if(!__atomic_load_n(&started,__ATOMIC_ACQUIRE)||__atomic_load_n(&observe_only,__ATOMIC_ACQUIRE))return 0;
    struct xz_key_audio_deck_proof *p=&xz_mods_key_audio_proof_v1.deck[deck];
    uint32_t before=__atomic_load_n(&p->epoch,__ATOMIC_ACQUIRE);
    uint32_t bits=__atomic_load_n(&p->ratio_bits,__ATOMIC_RELAXED);float ratio;memcpy(&ratio,&bits,4);
    if(before==epoch&&before==__atomic_load_n(&p->epoch,__ATOMIC_ACQUIRE)&&isfinite(ratio)&&ratio>=.5f&&ratio<=2.0f)
        *out=(int)roundf(1200*log2f(ratio));
    return 0;
}
void xz_key_runtime_observe(int observe){__atomic_store_n(&observe_only,observe!=0,__ATOMIC_RELEASE);}

void xz_key_runtime_stop(void)
{
    int i;
    __atomic_store_n(&started, 0, __ATOMIC_RELEASE);
    for (i = 0; i < XZ_KEY_DECKS; ++i)
        __atomic_store_n(&desired_cents[i], 0, __ATOMIC_RELEASE);
    for (i = 0; i < XZ_KEY_DECKS; ++i)
        __atomic_store_n(&desired_epoch[i], 0, __ATOMIC_RELEASE);
}
