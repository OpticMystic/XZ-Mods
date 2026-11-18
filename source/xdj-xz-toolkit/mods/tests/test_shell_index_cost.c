/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#ifndef XZ_INDEX_COST_INSTRUMENTED
#error Run through verify_shell_index_cost.py.
#endif
static uint64_t token_inspections, map_inspections;
static size_t map_index(size_t index) { map_inspections++; return index; }
#include "../shell_analysis.c"

int xz_oc_bundle(const char *source,char *folder,size_t capacity) {
    (void)source; (void)folder; (void)capacity; return -1;
}

int main(int argc,char **argv) {
    if(argc!=3)return 2;
    unsigned count=(unsigned)strtoul(argv[2],NULL,10);
    struct xz_shana_stems *map=NULL;
    int built=xz_shana_stems_build(argv[1],&map);
    char track[100],folder[1200],suffix[64];
    int correct=!built&&xz_shana_stems_count(map)==count;
    for(unsigned i=1;correct&&i<=count;i++){
        snprintf(track,sizeof track,"/Contents/Track %04u.mp3",i);
        snprintf(suffix,sizeof suffix,"/CDJMODS/stems/%016x",i);
        correct=!xz_shana_stems_find(map,track,folder,sizeof folder)&&strstr(folder,suffix);
    }
    uint64_t budget=(uint64_t)count*512;
    int bounded=token_inspections+map_inspections<=budget;
    printf("{\"entries\":%u,\"token_inspections\":%llu,\"map_inspections\":%llu,"
           "\"budget\":%llu,\"last_entry_correct\":%s,\"work_budget_met\":%s}\n",
           count,(unsigned long long)token_inspections,(unsigned long long)map_inspections,
           (unsigned long long)budget,correct?"true":"false",bounded?"true":"false");
    xz_shana_stems_free(map);
    return correct&&bounded?0:1;
}
