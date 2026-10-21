// SPDX-License-Identifier: GPL-2.0-only
#ifndef XZ_USB_TRANSPORT_H
#define XZ_USB_TRANSPORT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define XZ_USB_RATE 44100u
#define XZ_USB_BLOCK_FRAMES 256u
#define XZ_USB_INITIAL_GENERATION 1u
typedef struct xz_usb_transport xz_usb_transport;
typedef enum { XZ_USB_DECK_3 = 3, XZ_USB_DECK_4 = 4 } xz_usb_deck;
typedef enum {
    XZ_USB_OK, XZ_USB_AGAIN, XZ_USB_INVALID, XZ_USB_STALE,
    XZ_USB_STOPPED, XZ_USB_WRONG_DEVICE, XZ_USB_BUSY, XZ_USB_IO, XZ_USB_TIMEOUT
} xz_usb_result;
typedef enum { XZ_USB_STARTING, XZ_USB_RUNNING, XZ_USB_CLOSING, XZ_USB_CLOSED, XZ_USB_FAULTED } xz_usb_state;
typedef struct {
    uint32_t applied_generation;
    uint32_t source_valid; /* bit 0: staged tag, bit 1: completed tag */
    uint64_t staged_frames, completed_frames, underrun_frames;
    uint64_t source_after_staged, source_after_completed;
    uint64_t prior_audio_through_wire_frame;
} xz_usb_deck_position;
typedef struct {
    xz_usb_state state;
    xz_usb_result fault;
    int system_errno, cleanup_errno;
    uint32_t requested_generation[2];
    xz_usb_deck_position deck[2];
    uint64_t wire_submitted_frames, wire_completed_frames, input_frames;
    uint64_t input_urbs, output_urbs, fallback_packets, feedback_replaced;
    uint64_t maximum_completion_gap_us;
} xz_usb_position;

/* Control worker only. Validates an existing tmpfs character node and device,
 * refuses a busy interface or alternate != 0, and primes silent USB queues.
 * Success starts in STARTING; RUNNING follows eight valid input completions.
 * The host owns the RAM hub module, node creation and stopped-deck policy.
 * Setup may block in the kernel. On failure *out stays NULL. */
xz_usb_result xz_usb_transport_open(const char *ram_usb_node, xz_usb_transport **out);

/* Nonblocking, all-or-none copy of 1..256 interleaved stereo float frames at
 * 44100 Hz. Finite samples are clipped to [-1,1]. Success transfers a copy.
 * AGAIN means this deck's queue is full or input timing is still starting.
 * BUSY means a submit/flush for this deck is in progress. STALE rejects PCM
 * decoded before a seek. source_after is the player's opaque end-of-block
 * source-frame tag, carried unchanged without pitch/rate interpretation. */
xz_usb_result xz_usb_transport_submit(xz_usb_transport *, xz_usb_deck, uint32_t generation,
                                      const float *stereo, uint32_t frames, uint64_t source_after);

/* Atomically discard pending old PCM and publish its first replacement block.
 * The new block is queued before the new requested generation is released.
 * expected_generation must match the current route/deck generation.
 * AGAIN (starting/full), BUSY, STALE, INVALID and STOPPED publish no block and leave the
 * requested generation unchanged; *generation changes only on OK. Full queues
 * drain normally before retry. No allocation or consumer lock is added.
 * Already staged USB audio retains the ordinary flush tail contract below. */
xz_usb_result xz_usb_transport_flush_and_submit(xz_usb_transport *, xz_usb_deck,
                                      uint32_t expected_generation, const float *stereo, uint32_t frames,
                                      uint64_t source_after, uint32_t *generation);

/* Nonblocking asynchronous discard of this deck's pending PCM. Returns a new
 * generation for future submits. May return BUSY: retry without submitting old
 * PCM. Applied generation in position acknowledges the discard. Already staged
 * USB audio remains; allow one in-progress URB plus the nominal 32 ms USB queue
 * after the request. The other deck keeps streaming. No cursor is reset. */
xz_usb_result xz_usb_transport_flush(xz_usb_transport *, xz_usb_deck, uint32_t *generation);

/* Nonblocking coherent cached snapshot. Counters/tags belong to applied_generation.
 * Tags advance only when a whole source block reaches that stage. Underrun
 * silence advances wire time, never source counters. USB completion is not an
 * audible-position measurement. Snapshot may lag the worker by a publication
 * interval (normally 10 ms). On AGAIN only state/fault/errno/requested_generation
 * are valid; retain the last successful counters. */
xz_usb_result xz_usb_transport_position(xz_usb_transport *, xz_usb_position *out);

/* Control worker only; quiesce all other callers first. Signals stop and joins;
 * only the USB worker discards/reaps/restores alt 0/releases/closes. On TIMEOUT
 * (~2 s), *io remains valid and must be retained for a later close retry. Never
 * unload the library or free its storage while that worker remains alive.
 * On other results resources are freed and *io becomes NULL. NULL is harmless.
 * Returns IO if final rollback failed. No forced thread cancellation. */
xz_usb_result xz_usb_transport_close(xz_usb_transport **io);

#ifdef __cplusplus
}
#endif
#endif
