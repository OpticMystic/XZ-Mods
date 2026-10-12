/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "stem_media.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct xz_stem_media {
    enum { MEDIA_WAV, MEDIA_FLAC, MEDIA_MP3, MEDIA_VORBIS } kind;
    union { drwav wav; drflac *flac; drmp3 mp3; stb_vorbis *vorbis; } decoder;
    uint32_t rate;
    uint64_t frames, position;
};

void xz_stem_media_close(struct xz_stem_media *m) {
    if (!m) return;
    switch (m->kind) {
    case MEDIA_WAV: drwav_uninit(&m->decoder.wav); break;
    case MEDIA_FLAC: drflac_close(m->decoder.flac); break;
    case MEDIA_MP3: drmp3_uninit(&m->decoder.mp3); break;
    case MEDIA_VORBIS: stb_vorbis_close(m->decoder.vorbis); break;
    }
    free(m);
}

struct xz_stem_media *xz_stem_media_open(const char *path, int untrimmed) {
    unsigned char magic[4];
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = fread(magic, 1, sizeof(magic), f); fclose(f);
    if (n != sizeof(magic)) return NULL;
    struct xz_stem_media *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    unsigned channels;
    if (!memcmp(magic, "fLaC", 4)) {
        m->kind = MEDIA_FLAC; m->decoder.flac = drflac_open_file(path, NULL);
        if (!m->decoder.flac) { free(m); return NULL; }
        m->rate = m->decoder.flac->sampleRate; channels = m->decoder.flac->channels;
        m->frames = m->decoder.flac->totalPCMFrameCount;
    } else if (!memcmp(magic, "OggS", 4)) {
        int error = 0;
        m->kind = MEDIA_VORBIS; m->decoder.vorbis = stb_vorbis_open_filename(path, &error, NULL);
        if (!m->decoder.vorbis) { free(m); return NULL; }
        stb_vorbis_info info = stb_vorbis_get_info(m->decoder.vorbis);
        m->rate = info.sample_rate; channels = (unsigned)info.channels;
        m->frames = stb_vorbis_stream_length_in_samples(m->decoder.vorbis);
    } else if (!memcmp(magic, "RIFF", 4) || !memcmp(magic, "RF64", 4)) {
        m->kind = MEDIA_WAV;
        if (!drwav_init_file(&m->decoder.wav, path, NULL)) { free(m); return NULL; }
        m->rate = m->decoder.wav.sampleRate; channels = m->decoder.wav.channels;
        m->frames = m->decoder.wav.totalPCMFrameCount;
    } else if (!memcmp(magic, "ID3", 3) || (magic[0] == 255 && (magic[1] & 224) == 224)) {
        m->kind = MEDIA_MP3;
        if (!drmp3_init_file(&m->decoder.mp3, path, NULL)) { free(m); return NULL; }
        if (untrimmed) m->decoder.mp3.delayInPCMFrames = m->decoder.mp3.paddingInPCMFrames = 0;
        m->rate = m->decoder.mp3.sampleRate; channels = m->decoder.mp3.channels;
        m->frames = drmp3_get_pcm_frame_count(&m->decoder.mp3);
        if (!drmp3_seek_to_pcm_frame(&m->decoder.mp3, 0)) { xz_stem_media_close(m); return NULL; }
    } else { free(m); return NULL; }
    if (channels != 2 || m->rate < 32000 || m->rate > 96000 || !m->frames || m->frames > UINT32_MAX) {
        xz_stem_media_close(m); return NULL;
    }
    return m;
}

uint32_t xz_stem_media_rate(const struct xz_stem_media *m) { return m ? m->rate : 0; }
uint64_t xz_stem_media_frames(const struct xz_stem_media *m) { return m ? m->frames : 0; }

int xz_stem_media_read(struct xz_stem_media *m, int64_t first, size_t frames, float *out) {
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
        case MEDIA_MP3: ok = drmp3_seek_to_pcm_frame(&m->decoder.mp3, (uint64_t)first); break;
        case MEDIA_VORBIS: ok = stb_vorbis_seek(m->decoder.vorbis, (unsigned)first); break;
        }
        if (!ok) return -1;
    }
    uint64_t got = 0;
    switch (m->kind) {
    case MEDIA_WAV: got = drwav_read_pcm_frames_f32(&m->decoder.wav, frames, out); break;
    case MEDIA_FLAC: got = drflac_read_pcm_frames_f32(m->decoder.flac, frames, out); break;
    case MEDIA_MP3: got = drmp3_read_pcm_frames_f32(&m->decoder.mp3, frames, out); break;
    case MEDIA_VORBIS: got = (unsigned)stb_vorbis_get_samples_float_interleaved(m->decoder.vorbis, 2, out, (int)frames * 2); break;
    }
    m->position = (uint64_t)first + got;
    if (got != frames) return -1;
    for (size_t i = 0; i < frames * 2; ++i) if (!isfinite(out[i])) return -1;
    return 0;
}
