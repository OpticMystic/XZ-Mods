/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "native_clock.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
static int read_memory(void *ctx,uint32_t address,void *out,size_t n){return pread(*(int*)ctx,out,n,(off_t)address)==(ssize_t)n?0:-1;}
int main(int argc,char **argv){
    if(argc!=2)return 2;char *end;long pid=strtol(argv[1],&end,10);if(*end||pid<1)return 2;
    char path[80];snprintf(path,sizeof(path),"/proc/%ld/mem",pid);int fd=open(path,O_RDONLY);if(fd<0)return 2;
    if(!xz_arcade_native_clock_valid(read_memory,&fd)){puts("NATIVE_CLOCK_GUARD_FAILED");close(fd);return 1;}
    float mixer_bpm;unsigned tap_mode;
    if(xz_arcade_mixer_clock_read(read_memory,&fd,&mixer_bpm,&tap_mode))printf("mixer_bpm=%.2f tap_mode=%u phase_requires_alignment=1\n",(double)mixer_bpm,tap_mode);
    else puts("mixer_tempo_unavailable");
    for(int i=0;i<12;i++){
        for(unsigned deck=0;deck<2;deck++){struct xz_arcade_track_clock s;
            if(xz_arcade_native_clock_read(read_memory,&fd,deck,&s))printf("deck=%u bpm=%.3f phase=%.4f playing=%u position_ms=%u\n",deck+1,(double)s.bpm,(double)s.bar_phase,s.playing,s.position_ms);
            else printf("deck=%u no_consistent_grid\n",deck+1);
        }
        struct timespec interval={0,100000000};nanosleep(&interval,NULL);
    }
    close(fd);return 0;
}

