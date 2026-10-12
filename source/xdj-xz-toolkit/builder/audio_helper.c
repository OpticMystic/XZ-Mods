/* SPDX-License-Identifier: MIT
 * Uses the pinned dr_libs in mods/audio/vendor; their licenses remain in headers.
 * This is a desktop helper, not XZ firmware. Output PCM is streamed in 4096 frames.
 *
 * inspect INPUT          -> JSON describing the decoded stream
 * convert INPUT NEW.wav  -> 16-bit stereo WAV at the source sample rate
 *
 * Accepts WAV (integer or float PCM, any depth), FLAC (any depth) and MP3,
 * mono or stereo, 8 kHz to 192 kHz. Mono is duplicated to both channels.
 * A 44.1 kHz 16-bit stereo WAV converts byte for byte.
 *
 * MP3 decodes every audio frame, the way a player does. A gapless decoder
 * would drop the LAME encoder delay plus its own 529-frame decoder delay at
 * the start and the encoder padding at the end; the JSON "mp3" object reports
 * those counts so a writer can record them, but the PCM keeps them, so the
 * prepared stems sit on the player's full-mix timeline.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

/* 4096 pages of 128 KiB of 96 kHz stereo s16, expressed as source frames at 44.1 kHz. */
#define MAX_FRAMES_44100 ((UINT64_C(4096) * 131072 / 4) * 147 / 320)
#define MIN_RATE 8000u
#define MAX_RATE 192000u

#ifdef _WIN32
static wchar_t *wide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *out = count ? malloc((size_t)count * sizeof(*out)) : NULL;
    if (out) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, count);
    return out;
}
#endif
static FILE *open_path(const char *path, int output) {
#ifdef _WIN32
    wchar_t *w = wide(path);
    if (!w) return NULL;
    int fd = _wopen(w, output ? _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY : _O_RDONLY | _O_BINARY, 0600);
    free(w);
    if (fd < 0) return NULL;
    FILE *f = _fdopen(fd, output ? "wb" : "rb");
    if (!f) _close(fd);
#else
    int fd = open(path, output ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY, 0600);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, output ? "wb" : "rb");
    if (!f) close(fd);
#endif
    return f;
}
static size_t read_bytes(void *context, void *buffer, size_t size) { return fread(buffer, 1, size, context); }
static int seek_file(FILE *f, int offset, int set, int end) { return fseek(f, offset, set ? SEEK_SET : end ? SEEK_END : SEEK_CUR) == 0; }
static drflac_bool32 flac_seek(void *context, int offset, drflac_seek_origin origin) {
    return seek_file(context, offset, origin == DRFLAC_SEEK_SET, origin == DRFLAC_SEEK_END);
}
static drwav_bool32 wav_seek(void *context, int offset, drwav_seek_origin origin) {
    return seek_file(context, offset, origin == DRWAV_SEEK_SET, origin == DRWAV_SEEK_END);
}
static drmp3_bool32 mp3_seek(void *context, int offset, drmp3_seek_origin origin) {
    return seek_file(context, offset, origin == DRMP3_SEEK_SET, origin == DRMP3_SEEK_END);
}
static drflac_bool32 flac_tell(void *context, drflac_int64 *position) { *position = ftell(context); return *position >= 0; }
static drwav_bool32 wav_tell(void *context, drwav_int64 *position) { *position = ftell(context); return *position >= 0; }
static drmp3_bool32 mp3_tell(void *context, drmp3_int64 *position) { *position = ftell(context); return *position >= 0; }
static void le32(unsigned char *out, uint32_t value) { for (int i=0;i<4;i++) out[i]=(unsigned char)(value>>(8*i)); }

static int is_mp3_start(const unsigned char *magic) {
    if (!memcmp(magic, "ID3", 3)) return 1;
    return magic[0] == 0xFF && (magic[1] & 0xE0) == 0xE0;
}

static int run(int argc, char **argv) {
    int convert = argc == 4 && !strcmp(argv[1], "convert");
    if (!convert && !(argc == 3 && !strcmp(argv[1], "inspect"))) {
        fprintf(stderr, "Usage: xz-audio-helper inspect INPUT | convert INPUT NEW.wav\n"); return 2;
    }
    FILE *input = open_path(argv[2], 0), *output = NULL;
    drflac *flac = NULL;
    drwav wav;
    drmp3 mp3;
    int wav_open = 0, mp3_open = 0, okay = 0;
    unsigned char magic[4];
    uint64_t frames = 0, expected = 0, mp3_delay = 0, mp3_padding = 0;
    unsigned rate = 0, channels = 0, bits = 0;
    int16_t pcm[4096*2];
    const char *format = NULL;
    const char *problem = "Invalid, truncated, unsupported or oversized audio, or output already exists";
    if (!input || fread(magic, 1, 4, input) != 4 || fseek(input, 0, SEEK_SET)) goto done;
    if (!memcmp(magic, "fLaC", 4)) {
        flac = drflac_open(read_bytes, flac_seek, flac_tell, input, NULL);
        if (!flac) goto done;
        expected=flac->totalPCMFrameCount; rate=flac->sampleRate; channels=flac->channels; bits=flac->bitsPerSample;
        format="flac";
    } else if (!memcmp(magic, "RIFF", 4)) {
        wav_open = drwav_init(&wav, read_bytes, wav_seek, wav_tell, input, NULL);
        if (!wav_open || (wav.translatedFormatTag != DR_WAVE_FORMAT_PCM && wav.translatedFormatTag != DR_WAVE_FORMAT_IEEE_FLOAT)) goto done;
        expected=wav.totalPCMFrameCount; rate=wav.sampleRate; channels=wav.channels; bits=wav.bitsPerSample;
        format="wav";
    } else if (is_mp3_start(magic)) {
        mp3_open = drmp3_init(&mp3, read_bytes, mp3_seek, mp3_tell, NULL, input, NULL);
        if (!mp3_open) goto done;
        rate=mp3.sampleRate; channels=mp3.channels; bits=16;
        /* MP3 frame counts are only known after decoding; the Xing header is advisory. */
        expected=0;
        format="mp3";
        /* dr_mp3 read the LAME tag and would skip encoder delay + 529 at the start and the padding at the end.
         * Keep every frame: that is the timeline the player's own decoder produces. */
        mp3_delay=mp3.delayInPCMFrames; mp3_padding=mp3.paddingInPCMFrames;
        mp3.delayInPCMFrames=0; mp3.paddingInPCMFrames=0;
    } else goto done;
    if (rate < MIN_RATE || rate > MAX_RATE || channels < 1 || channels > 2) goto done;
    if (expected && (expected * 44100 > MAX_FRAMES_44100 * (uint64_t)rate || !expected)) goto done;
    if (convert) {
        output = open_path(argv[3], 1);
        if (!output) goto done;
        /* Header sizes are patched after the last frame; MP3 has no reliable count up front. */
        unsigned char header[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0";
        le32(header+4, 0); le32(header+24,rate); le32(header+28,rate*4);
        header[32]=4; header[34]=16; memcpy(header+36,"data",4); le32(header+40,0);
        if (fwrite(header,1,44,output)!=44) goto done;
    }
    for (;;) {
        uint64_t got = flac ? drflac_read_pcm_frames_s16(flac,4096,pcm)
                     : wav_open ? drwav_read_pcm_frames_s16(&wav,4096,pcm)
                     : drmp3_read_pcm_frames_s16(&mp3,4096,pcm);
        if (!got) break;
        if (channels == 1) {
            for (uint64_t i = got; i-- > 0;) { pcm[i*2] = pcm[i]; pcm[i*2+1] = pcm[i]; }
        }
        frames += got;
        if ((expected && frames > expected) || frames * 44100 > MAX_FRAMES_44100 * (uint64_t)rate) goto done;
        if (output && fwrite(pcm,4,(size_t)got,output)!=got) goto done;
    }
    if (!frames || (expected && frames != expected) || ferror(input)) goto done;
    if (output) {
        unsigned char size[4];
        le32(size, (uint32_t)(frames*4+36));
        if (fseek(output, 4, SEEK_SET) || fwrite(size,1,4,output)!=4) goto done;
        le32(size, (uint32_t)(frames*4));
        if (fseek(output, 40, SEEK_SET) || fwrite(size,1,4,output)!=4) goto done;
    }
    okay = 1;
done:
    if (flac) drflac_close(flac);
    if (wav_open) drwav_uninit(&wav);
    if (mp3_open) drmp3_uninit(&mp3);
    if (input) fclose(input);
    if (output && fclose(output)) okay=0;
    if (!okay) {
        if (output) {
#ifdef _WIN32
            wchar_t *name=wide(argv[3]);
            if (name) { _wremove(name); free(name); }
#else
            remove(argv[3]);
#endif
        }
        fprintf(stderr,"%s\n",problem); return 1;
    }
    printf("{\"format\":\"%s\",\"frames\":%llu,\"sample_rate\":%u,\"channels\":2,\"sample_width\":2,\"pcm_bytes\":%llu,"
           "\"source_channels\":%u,\"source_bits\":%u",
           convert ? "wav" : format,(unsigned long long)frames,rate,(unsigned long long)(frames*4),channels,bits);
    if (mp3_open)
        printf(",\"mp3\":{\"lame_tag\":%s,\"delay_frames\":%llu,\"padding_frames\":%llu}",
               mp3_delay ? "true" : "false",(unsigned long long)mp3_delay,(unsigned long long)mp3_padding);
    printf("}\n");
    return 0;
}
int main(int argc, char **argv) {
#ifdef _WIN32
    int count;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    char **utf8 = calloc((size_t)count, sizeof(*utf8));
    if (!utf8) { LocalFree(args); return 2; }
    for (int i=0;i<count;i++) {
        int size=WideCharToMultiByte(CP_UTF8,0,args[i],-1,NULL,0,NULL,NULL);
        utf8[i]=malloc((size_t)size);
        if (!utf8[i]) return 2;
        WideCharToMultiByte(CP_UTF8,0,args[i],-1,utf8[i],size,NULL,NULL);
    }
    int result=run(count,utf8);
    for (int i=0;i<count;i++) free(utf8[i]);
    free(utf8); LocalFree(args); return result;
#else
    return run(argc,argv);
#endif
}
