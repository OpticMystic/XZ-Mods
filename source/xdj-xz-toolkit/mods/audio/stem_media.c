/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "stem_media.h"
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aiff_reader.h"
#define DR_FLAC_NO_OGG
#include "vendor/dr_libs/dr_flac.h"
#include "vendor/dr_libs/dr_wav.h"
#include "vendor/dr_libs/dr_mp3.h"
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
#include "vendor/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#ifndef MP3_CACHE_FRAMES
#define MP3_CACHE_FRAMES 4096u
#endif
#ifndef MP3_CACHE_TILES
#define MP3_CACHE_TILES 16u
#endif
_Static_assert(MP3_CACHE_TILES * MP3_CACHE_FRAMES <= 65536u, "MP3 PCM cache must stay at or below 512 KiB");
struct xz_stem_media {
    enum { MEDIA_WAV, MEDIA_FLAC, MEDIA_MP3, MEDIA_VORBIS, MEDIA_AIFF } kind;
    union { drwav wav; drflac *flac; drmp3 mp3; stb_vorbis *vorbis; struct xz_aiff aiff; } decoder;
    FILE *file;
    uint32_t rate;
    uint64_t frames, position;
    /* Approximately 100 ms between seek points, at most 192 KiB per decoder. */
    drmp3_seek_point *seek_points;
    /* Fixed source-PCM tiles, at most 512 KiB, allocated on first read. */
    float *pcm_cache;
    uint64_t cache_first[MP3_CACHE_TILES];
    size_t cache_frames[MP3_CACHE_TILES];
    unsigned cache_next, cache_attempted;
};

static size_t media_read(void *f, void *out, size_t bytes) { return fread(out, 1, bytes, f); }
#define MEDIA_CALLBACKS(prefix, type, integer, SET, CUR) \
static type##_bool32 prefix##_seek(void *f, int offset, type##_seek_origin origin) { \
    return fseeko(f, offset, origin == SET ? SEEK_SET : origin == CUR ? SEEK_CUR : SEEK_END) == 0; \
} \
static type##_bool32 prefix##_tell(void *f, integer *out) { \
    int64_t at = ftello(f); if (at < 0) return 0; *out = at; return 1; \
}
MEDIA_CALLBACKS(wav, drwav, drwav_int64, DRWAV_SEEK_SET, DRWAV_SEEK_CUR)
MEDIA_CALLBACKS(flac, drflac, drflac_int64, DRFLAC_SEEK_SET, DRFLAC_SEEK_CUR)
MEDIA_CALLBACKS(mp3, drmp3, drmp3_int64, DRMP3_SEEK_SET, DRMP3_SEEK_CUR)
#undef MEDIA_CALLBACKS

int xz_media_identify(int fd, struct xz_media_identity *out) {
#ifdef _WIN32
    (void)fd; (void)out; return 0;
#else
    struct stat s;
    if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_size <= 0) return 0;
    *out = (struct xz_media_identity){s.st_dev, s.st_ino, (uint64_t)s.st_size,
        s.st_mtim.tv_sec, s.st_ctim.tv_sec, s.st_mtim.tv_nsec, s.st_ctim.tv_nsec};
    return 1;
#endif
}
int xz_media_same(const struct xz_media_identity *a, const struct xz_media_identity *b) {
    return a->device == b->device && a->inode == b->inode && a->bytes == b->bytes &&
        a->modified == b->modified && a->changed == b->changed &&
        a->modified_ns == b->modified_ns && a->changed_ns == b->changed_ns;
}
int xz_stem_media_source_snapshot(struct xz_stem_media *m, struct xz_media_identity *id, int *fd) {
    *fd = -1;
#ifndef _WIN32
    if (!m || !xz_media_identify(fileno(m->file), id)) return 0;
    *fd = dup(fileno(m->file));
    if (*fd >= 0 && fcntl(*fd, F_SETFD, FD_CLOEXEC)) { close(*fd); *fd = -1; }
#else
    (void)m; (void)id;
#endif
    return *fd >= 0;
}
int xz_stem_media_replace_file(struct xz_stem_media *m, const struct xz_media_identity *expected,
                               int prepared, int *old_fd) {
    *old_fd = -1;
#ifdef _WIN32
    (void)m; (void)expected; (void)prepared; return 0;
#else
    struct xz_media_identity current;
    int target = fileno(m->file), flags = fcntl(target, F_GETFD);
    off_t physical = lseek(target, 0, SEEK_CUR);
    if (flags < 0 || physical < 0 || !xz_media_identify(target, &current) ||
        !xz_media_same(expected, &current) || lseek(prepared, physical, SEEK_SET) < 0) return 0;
    *old_fd = dup(target);
    if (*old_fd < 0 || fcntl(*old_fd, F_SETFD, FD_CLOEXEC)) return 0;
    if (dup2(prepared, target) < 0) return 0;
    if (!fcntl(target, F_SETFD, flags)) return 1;
    if (dup2(*old_fd, target) < 0 || fcntl(target, F_SETFD, flags)) return -1;
    return 0;
#endif
}

void xz_stem_media_close(struct xz_stem_media *m) {
    if (!m) return;
    switch (m->kind) {
    case MEDIA_WAV: drwav_uninit(&m->decoder.wav); break;
    case MEDIA_FLAC: drflac_close(m->decoder.flac); break;
    case MEDIA_MP3: drmp3_uninit(&m->decoder.mp3); break;
    case MEDIA_VORBIS: stb_vorbis_close(m->decoder.vorbis); break;
    case MEDIA_AIFF: break;
    }
    if (m->file) fclose(m->file);
    free(m->pcm_cache);
    free(m->seek_points);
    free(m);
}

struct xz_stem_media *xz_stem_media_open(const char *path, int untrimmed) {
    unsigned char magic[4];
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = fread(magic, 1, sizeof(magic), f);
    if (n != sizeof(magic) || fseeko(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    struct xz_stem_media *m = calloc(1, sizeof(*m));
    if (!m) { fclose(f); return NULL; }
    m->file = f;
    unsigned channels;
    if (!memcmp(magic, "fLaC", 4)) {
        m->kind = MEDIA_FLAC; m->decoder.flac = drflac_open(media_read, flac_seek, flac_tell, f, NULL);
        if (!m->decoder.flac) { fclose(f); free(m); return NULL; }
        m->rate = m->decoder.flac->sampleRate; channels = m->decoder.flac->channels;
        m->frames = m->decoder.flac->totalPCMFrameCount;
    } else if (!memcmp(magic, "OggS", 4)) {
        int error = 0;
        m->kind = MEDIA_VORBIS; m->decoder.vorbis = stb_vorbis_open_file(f, 0, &error, NULL);
        if (!m->decoder.vorbis) { fclose(f); free(m); return NULL; }
        stb_vorbis_info info = stb_vorbis_get_info(m->decoder.vorbis);
        m->rate = info.sample_rate; channels = (unsigned)info.channels;
        m->frames = stb_vorbis_stream_length_in_samples(m->decoder.vorbis);
    } else if (!memcmp(magic, "RIFF", 4) || !memcmp(magic, "RF64", 4)) {
        m->kind = MEDIA_WAV;
        if (!drwav_init(&m->decoder.wav, media_read, wav_seek, wav_tell, f, NULL)) { fclose(f); free(m); return NULL; }
        m->rate = m->decoder.wav.sampleRate; channels = m->decoder.wav.channels;
        m->frames = m->decoder.wav.totalPCMFrameCount;
    } else if (!memcmp(magic, "FORM", 4)) {
        m->kind = MEDIA_AIFF;
        if (!xz_aiff_init(&m->decoder.aiff, f)) { xz_stem_media_close(m); return NULL; }
        m->rate = m->decoder.aiff.rate; channels = m->decoder.aiff.channels;
        m->frames = m->decoder.aiff.frames;
    } else if (!memcmp(magic, "ID3", 3) || (magic[0] == 255 && (magic[1] & 224) == 224)) {
        m->kind = MEDIA_MP3;
        if (!drmp3_init(&m->decoder.mp3, media_read, mp3_seek, mp3_tell, NULL, f, NULL)) { fclose(f); free(m); return NULL; }
        if (untrimmed) m->decoder.mp3.delayInPCMFrames = m->decoder.mp3.paddingInPCMFrames = 0;
        m->rate = m->decoder.mp3.sampleRate; channels = m->decoder.mp3.channels;
        m->frames = drmp3_get_pcm_frame_count(&m->decoder.mp3);
        uint64_t desired = m->frames / (m->rate / 10u);
        uint32_t count = desired > 8192u ? 8192u : desired < 2048u ? 2048u : (uint32_t)desired;
        m->seek_points = calloc(count, sizeof(*m->seek_points));
        if (m->seek_points && drmp3_calculate_seek_points(&m->decoder.mp3, &count, m->seek_points))
            drmp3_bind_seek_table(&m->decoder.mp3, count, m->seek_points);
        else { free(m->seek_points); m->seek_points = NULL; }
        if (!drmp3_seek_to_pcm_frame(&m->decoder.mp3, 0)) { xz_stem_media_close(m); return NULL; }
    } else { fclose(f); free(m); return NULL; }
    if (channels != 2 || m->rate < 32000 || m->rate > 96000 || !m->frames || m->frames > UINT32_MAX) {
        xz_stem_media_close(m); return NULL;
    }
    return m;
}

uint32_t xz_stem_media_rate(const struct xz_stem_media *m) { return m ? m->rate : 0; }
uint64_t xz_stem_media_frames(const struct xz_stem_media *m) { return m ? m->frames : 0; }

static int media_read_uncached(struct xz_stem_media *m, int64_t first, size_t frames, float *out) {
    if (!m || !out || frames > 262144) return -1;
    memset(out, 0, frames * 2 * sizeof(*out));
    if (first < 0) {
        uint64_t skip = (uint64_t)(-(first + 1)) + 1;
        if (skip >= frames) return 0;
        out += (size_t)skip * 2; frames -= (size_t)skip; first = 0;
    }
    if ((uint64_t)first >= m->frames) return 0;
    if (frames > m->frames - (uint64_t)first) frames = (size_t)(m->frames - (uint64_t)first);
    if ((uint64_t)first != m->position) {
        int ok = 0;
        switch (m->kind) {
        case MEDIA_WAV: ok = drwav_seek_to_pcm_frame(&m->decoder.wav, (uint64_t)first); break;
        case MEDIA_FLAC: ok = drflac_seek_to_pcm_frame(m->decoder.flac, (uint64_t)first); break;
        case MEDIA_MP3: {
            /* dr_mp3's internal currentPCMFrame includes encoder delay, while
             * read counts and this API use trimmed PCM. Discard a logical delta;
             * its absolute forward seek mixes those two coordinate systems. */
            uint64_t raw_target = (uint64_t)first + m->decoder.mp3.delayInPCMFrames;
            if (m->seek_points && raw_target >= m->seek_points[0].pcmFrameIndex) {
                ok = drmp3_seek_to_pcm_frame(&m->decoder.mp3, raw_target);
                if (ok) break;
                /* A damaged frame can defeat indexed preroll. Reset before the
                 * exact sequential fallback, including after a partial seek. */
                if (!drmp3_seek_to_pcm_frame(&m->decoder.mp3, 0)) break;
                ok = drmp3_read_pcm_frames_s16(&m->decoder.mp3, (uint64_t)first, NULL) == (uint64_t)first;
                break;
            }
            uint64_t from = m->position;
            ok = 1;
            if ((uint64_t)first < from) { ok = drmp3_seek_to_pcm_frame(&m->decoder.mp3, 0); from = 0; }
            uint64_t skip = (uint64_t)first - from;
            /* stem_mp3.c uses the decoder's s16 output; this is its discard path. */
            if (ok && drmp3_read_pcm_frames_s16(&m->decoder.mp3, skip, NULL) != skip) ok = 0;
            break;
        }
        case MEDIA_VORBIS: ok = stb_vorbis_seek(m->decoder.vorbis, (unsigned)first); break;
        case MEDIA_AIFF: ok = xz_aiff_seek(&m->decoder.aiff, (uint64_t)first); break;
        }
        if (!ok) { m->position = UINT64_MAX; return -1; }
    }
    uint64_t got = 0;
    switch (m->kind) {
    case MEDIA_WAV: got = drwav_read_pcm_frames_f32(&m->decoder.wav, frames, out); break;
    case MEDIA_FLAC: got = drflac_read_pcm_frames_f32(m->decoder.flac, frames, out); break;
    case MEDIA_MP3: got = drmp3_read_pcm_frames_f32(&m->decoder.mp3, frames, out); break;
    case MEDIA_VORBIS: got = (unsigned)stb_vorbis_get_samples_float_interleaved(m->decoder.vorbis, 2, out, (int)frames * 2); break;
    case MEDIA_AIFF: got = xz_aiff_read(&m->decoder.aiff, frames, out); break;
    }
    m->position = (uint64_t)first + got;
    if (got != frames) { m->position = UINT64_MAX; return -1; }
    for (size_t i = 0; i < frames * 2; ++i) if (!isfinite(out[i])) {
        m->position = UINT64_MAX; return -1;
    }
    return 0;
}

int xz_stem_media_read(struct xz_stem_media *m, int64_t first, size_t frames, float *out) {
    if (!m || !out || frames > 262144) return -1;
    if (m->kind == MEDIA_MP3 && !m->cache_attempted) {
        m->cache_attempted = 1;
        m->pcm_cache = malloc(MP3_CACHE_TILES * MP3_CACHE_FRAMES * 2u * sizeof(float));
    }
    if (!m->pcm_cache) return media_read_uncached(m, first, frames, out);
    memset(out, 0, frames * 2 * sizeof(*out));
    if (first < 0) {
        uint64_t skip = (uint64_t)(-(first + 1)) + 1;
        if (skip >= frames) return 0;
        out += (size_t)skip * 2; frames -= (size_t)skip; first = 0;
    }
    if ((uint64_t)first >= m->frames) return 0;
    if (frames > m->frames - (uint64_t)first) frames = (size_t)(m->frames - (uint64_t)first);
    while (frames) {
        uint64_t tile = (uint64_t)first / MP3_CACHE_FRAMES * MP3_CACHE_FRAMES;
        unsigned slot;
        for (slot = 0; slot < MP3_CACHE_TILES; ++slot)
            if (m->cache_frames[slot] && m->cache_first[slot] == tile) break;
        if (slot == MP3_CACHE_TILES) {
            slot = m->cache_next;
            m->cache_next = (m->cache_next + 1u) % MP3_CACHE_TILES;
            m->cache_frames[slot] = 0;
            size_t count = (size_t)(m->frames - tile);
            if (count > MP3_CACHE_FRAMES) count = MP3_CACHE_FRAMES;
            if (media_read_uncached(m, (int64_t)tile, count,
                    m->pcm_cache + slot * MP3_CACHE_FRAMES * 2u)) return -1;
            m->cache_first[slot] = tile;
            m->cache_frames[slot] = count;
        }
        size_t offset = (size_t)((uint64_t)first - tile);
        size_t count = m->cache_frames[slot] - offset;
        if (count > frames) count = frames;
        memcpy(out, m->pcm_cache + (slot * MP3_CACHE_FRAMES + offset) * 2u,
               count * 2u * sizeof(*out));
        first += (int64_t)count; frames -= count; out += count * 2u;
    }
    return 0;
}
