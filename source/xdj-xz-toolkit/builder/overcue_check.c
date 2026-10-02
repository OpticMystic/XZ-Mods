/* SPDX-License-Identifier: MIT */
#include "../mods/audio/overcue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static int run(int argc, char **argv) {
    if(argc==6&&!strcmp(argv[1],"--role")){
        char *end;errno=0;unsigned long long frames=strtoull(argv[3],&end,10);
        if(errno||!*argv[3]||*end||xz_oc_verify_role(argv[2],(uint64_t)frames,argv[5],argv[4])){
            fputs("OverCue role failed format, page or complete PCM verification.\n",stderr);return 1;
        }puts("{\"verified\":true}");return 0;
    }
    const char *source = argc == 2 ? argv[1] : NULL;
    if (!source) { fputs("Choose one original track on an OverCue USB.\n", stderr); return 2; }
    struct xz_oc_assets assets;
    char error[256];
    if (xz_oc_open(source, &assets, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); return 1;
    }
    unsigned files = 0,flac_files=0;
    for (unsigned mask = 1; mask <= 7; mask++) {
        struct xz_oc_file *file = xz_oc_mix_file(&assets, mask);
        if (!file || !file->file) {
            fputs("All seven OverCue prepared mixes are required.\n", stderr);
            xz_oc_close(&assets); return 1;
        }
        if(xz_oc_verify(file)){
            fputs("OverCue audio failed decoding or page/complete PCM checksum verification.\n",stderr);
            xz_oc_close(&assets);return 1;
        }
        files++;if(file->codec==3)flac_files++;
    }
    printf("{\"format\":\"overcue-stems/4\",\"sample_rate\":96000,\"channels\":2,"
           "\"frames\":%llu,\"verified_mixes\":%u,\"all_pages_verified\":true,"
           "\"page_codec\":\"%s\",\"flac_mixes\":%u,\"source_identity_verified\":%s,\"audio_alignment_verified\":false}\n",
           (unsigned long long)assets.frames, files,flac_files==7?"flac-96k":flac_files?"mixed":"zlib",flac_files,
           assets.source_hash_verified?"true":"false");
    xz_oc_close(&assets); return 0;
}
int main(int argc,char **argv){
#ifdef _WIN32
    int count=0;LPWSTR *wide=CommandLineToArgvW(GetCommandLineW(),&count);if(!wide)return 2;
    char **utf8=calloc((size_t)count,sizeof(*utf8));int valid=utf8!=NULL;
    for(int i=0;valid&&i<count;i++){
        utf8[i]=malloc(4096);valid=utf8[i]&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,utf8[i],4096,NULL,NULL);
    }
    int rc=valid?run(count,utf8):2;
    if(utf8){for(int i=0;i<count;i++)free(utf8[i]);free(utf8);}LocalFree(wide);
    (void)argc;(void)argv;return rc;
#else
    return run(argc,argv);
#endif
}
