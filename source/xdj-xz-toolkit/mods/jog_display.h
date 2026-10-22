#ifndef XZ_JOG_DISPLAY_H
#define XZ_JOG_DISPLAY_H
#include <stddef.h>
#include <stdint.h>

#define XZ_JD_FRAME 148u
#define XZ_JD_PACKET 64u
#define XZ_JD_FAMILIES 6u
#define XZ_JD_WAVE_PACKETS 128u
#define XZ_JD_ART_PACKETS 4784u
#define XZ_JD_STORAGE_BYTES ((4u+XZ_JD_WAVE_PACKETS+XZ_JD_ART_PACKETS)*2u*XZ_JD_PACKET)

enum xz_jd_result { XZ_JD_OK, XZ_JD_NOT_READY, XZ_JD_BAD_INPUT, XZ_JD_UNSUPPORTED, XZ_JD_RESTORING, XZ_JD_STALE };
enum xz_jd_phase { XZ_JD_NATIVE, XZ_JD_EXTRA_CLEAR, XZ_JD_EXTRA, XZ_JD_NATIVE_CLEAR, XZ_JD_REPLAY };
struct xz_jd_snapshot {
    uint64_t generation, captured_ms;
    uint32_t position_ms, length_ms;
    uint8_t logical_deck, paused;
    int16_t tempo_rate; /* Exact signed UiGetPlayTempoRate wire units. */
    uint8_t tempo_range, vinyl_mode, master_tempo, tempo_known;
};
struct xz_jd_family {
    uint8_t *bank[2];
    uint16_t capacity, count, building_count, next, replay;
    uint32_t version, replay_version;
    uint8_t active, complete;
};
struct xz_jd {
    struct xz_jd_family family[XZ_JD_FAMILIES];
    uint64_t native_epoch, software_generation;
    uint32_t freshness_ms;
    uint16_t observed_sequence, wire_sequence;
    uint8_t physical_side, selected, clear_next, replay_family, replay_ticks;
    uint8_t observed, ever_owned, unavailable;
    enum xz_jd_phase phase;
};

/* All calls belong to the native serializer owner thread. Storage remains owned
 * by this state until shutdown. No allocation, locks, syscalls or device I/O. */
int xz_jd_init(struct xz_jd *, unsigned physical_side, void *storage, size_t storage_bytes, uint32_t freshness_ms);
/* Call with an authoritative native media identity/generation, before packets
 * for that media are observed. Packet track number alone is not an identity. */
void xz_jd_native_epoch(struct xz_jd *, uint64_t epoch);
/* Optional owner-thread priming from a complete native cached producer batch.
 * Streaming observation inside step uses the same validated packet format. */
int xz_jd_capture(struct xz_jd *, const uint8_t packet[XZ_JD_PACKET]);
int xz_jd_ready(const struct xz_jd *);
int xz_jd_select(struct xz_jd *, unsigned logical_deck, const struct xz_jd_snapshot *, uint64_t now_ms);
/* Keep software control assignment visible while its track telemetry is absent.
   Emits clear/unavailable packets, never the hidden native deck's live state. */
int xz_jd_select_unavailable(struct xz_jd *, unsigned logical_deck);
/* Input must be ORIGINAL output of native assemble, before previous overrides.
 * A stale software snapshot keeps logical ownership but clears displayed data.
 * Native frame routing/configuration is retained; only proven display fields
 * are changed. Unsupported PC/HID ownership is returned without mutation. */
int xz_jd_step(struct xz_jd *, const uint8_t native_frame[XZ_JD_FRAME], const struct xz_jd_snapshot *, uint64_t now_ms, uint8_t output[XZ_JD_FRAME]);
uint32_t xz_jd_crc(const uint8_t payload[144]);
void xz_jd_clear_packet(unsigned logical_deck, unsigned family, uint8_t output[64]);
#endif
