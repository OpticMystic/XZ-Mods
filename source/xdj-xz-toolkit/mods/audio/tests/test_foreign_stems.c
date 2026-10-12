/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../foreign_stems.h"
#include "../stem_media.h"
#include "../overcue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#ifdef NDEBUG
#error Assertions required
#endif
#define BLOCK 1024u
static void pause_worker(void){struct timespec t={0,10000000};nanosleep(&t,NULL);}
static void native_pcm(struct xz_stem_media *source,int64_t at,float *out){
    float raw[(BLOCK+2)*2];assert(!xz_stem_media_read(source,at+27,BLOCK+2,raw));
    for(unsigned i=0;i<BLOCK*2;i++)out[i]=(raw[i]*.75f+raw[i+2]*.25f)*1.25f;
}
static void plan_test(void){
    const float choices[4]={0,.25f,1,2},parts[4]={.02f,.03f,.05f,.07f};
    for(unsigned a=0;a<4;a++)for(unsigned b=0;b<4;b++)for(unsigned c=0;c<4;c++)for(unsigned d=0;d<4;d++){
        float levels[4]={choices[a],choices[b],choices[c],choices[d]};struct xz_oc_plan plan;
        assert(!xz_oc_plan_levels4(levels,&plan));float got=.17f*plan.source,want=0;
        for(unsigned r=0;r<4;r++){
            want+=parts[r]*levels[r];
            for(unsigned slot=0;slot<3;slot++)if(plan.mask[slot]&(1u<<r))got+=plan.gain[slot]*parts[r];
        }assert(fabsf(got-want)<.000001f);
    }
    struct xz_oc_plan plan;float invalid[4]={1,1,1,NAN};assert(xz_oc_plan_levels4(invalid,&plan));
}
int main(int argc,char **argv){
    if(argc<3)return 2;plan_test();char error[160];
    if(!strcmp(argv[2],"overcue-first")){
        struct xz_oc_stream *stream=xz_oc_stream_open(argv[1],44100,error,sizeof(error));
        assert(stream&&xz_oc_stream_stem_count(stream)==3&&!strcmp(xz_oc_stream_format(stream),"OVERCUE 3"));
        xz_oc_stream_close(stream);puts("PASS OverCue priority with a matching AZ set present");return 0;
    }
    struct xz_foreign_stems *set=xz_foreign_open(argv[1],error,sizeof(error));
    if(!strcmp(argv[2],"reject")){if(set){fprintf(stderr,"Unexpected accepted fixture\n");xz_foreign_close(set);return 3;}puts(error);return 0;}
    if(!set){fprintf(stderr,"Open failed: %s\n",error);return 4;}
    unsigned count=(unsigned)atoi(argv[2]);assert(xz_foreign_stem_count(set)==count);assert(xz_foreign_rate(set)==44100);
    float pcm[BLOCK*2],sum[BLOCK*2]={0},part[BLOCK*2];
    for(unsigned r=0;r<count;r++){
        assert(!xz_foreign_read(set,1u<<r,14000,BLOCK,part));
        double energy=0;for(unsigned i=0;i<BLOCK*2;i++){sum[i]+=part[i];energy+=part[i]*part[i];}assert(energy>.001);
    }
    assert(!xz_foreign_read(set,(1u<<count)-1,14000,BLOCK,pcm));
    for(unsigned i=0;i<BLOCK*2;i++)assert(fabsf(sum[i]-pcm[i])<.000002f);
    if(argc>3&&!strcmp(argv[3],"dump")){
#ifdef _WIN32
        assert(_setmode(_fileno(stdout),_O_BINARY)!=-1);
#endif
        for(unsigned r=0;r<count;r++){
            assert(!xz_foreign_read(set,1u<<r,0,BLOCK,pcm));
            if(fwrite(pcm,sizeof(float),BLOCK*2,stdout)!=BLOCK*2)return 5;
        }xz_foreign_close(set);return 0;
    }
    if(argc>3&&!strcmp(argv[3],"reader")){xz_foreign_close(set);puts("PASS foreign role readers");return 0;}
    struct xz_stem_media *original=xz_stem_media_open(argv[1],0);assert(original);
    struct xz_oc_stream *stream=xz_oc_stream_open(argv[1],44100,error,sizeof(error));assert(stream);
    assert(xz_oc_stream_stem_count(stream)==count);
    float unity[4]={1,1,1,1},before[BLOCK*2];struct xz_oc_status status;
    for(unsigned attempt=0;attempt<800;attempt++){
        int64_t at=10000+(attempt%8)*3000;native_pcm(original,at,pcm);memcpy(before,pcm,sizeof(pcm));
        assert(!xz_oc_stream_mix4_trace(stream,pcm,BLOCK,at,unity,NULL));assert(!memcmp(before,pcm,sizeof(pcm)));
        pause_worker();xz_oc_stream_status(stream,&status);if(status.state==XZ_OC_READY)break;
    }
    fprintf(stderr,"foreign alignment %s state=%d lag=%d correlation=%.6f\n",xz_oc_stream_format(stream),status.state,status.lag,status.correlation);
    assert(status.state==XZ_OC_READY&&abs(status.lag-27)<=1&&status.correlation>=.995f);
    for(unsigned mask=0;mask<(1u<<count);mask++){
        float levels[4]={1,1,1,1};for(unsigned r=0;r<count;r++)levels[r]=!!(mask&(1u<<r));
        for(unsigned warm=0;warm<400;warm++){
            native_pcm(original,14000,pcm);xz_oc_stream_mix4_trace(stream,pcm,BLOCK,14000,levels,NULL);
            pause_worker();xz_oc_stream_status(stream,&status);if(status.state==XZ_OC_READY&&warm>2)break;
        }
        assert(status.state==XZ_OC_READY);native_pcm(original,14000,pcm);
        xz_oc_stream_mix4_trace(stream,pcm,BLOCK,14000,levels,NULL);
        if(!mask){for(unsigned i=0;i<BLOCK*2;i++)assert(pcm[i]==0);continue;}
        if(mask==(1u<<count)-1){native_pcm(original,14000,before);assert(!memcmp(before,pcm,sizeof(pcm)));continue;}
        float expected[(BLOCK+2)*2];assert(!xz_foreign_read(set,mask,14027,BLOCK+2,expected));
        double error_sum=0,energy=0;
        for(unsigned i=256;i<BLOCK*2;i++){
            float want=(expected[i]*.75f+expected[i+2]*.25f)*1.25f;
            error_sum+=(pcm[i]-want)*(pcm[i]-want);energy+=pcm[i]*pcm[i];
        }
        assert(energy>.001&&sqrt(error_sum/(BLOCK*2-256))<.004);
    }
    /* A far seek cannot leak a stale selection or replace audio with silence. */
    float vocals[4]={0,0,1,0};native_pcm(original,200000,pcm);memcpy(before,pcm,sizeof(pcm));
    xz_oc_stream_mix4_trace(stream,pcm,BLOCK,200000,vocals,NULL);
    for(unsigned i=0;i<BLOCK*2;i++)assert(isfinite(pcm[i]));
    double energy=0;for(unsigned i=0;i<BLOCK*2;i++)energy+=pcm[i]*pcm[i];assert(energy>.001);
    xz_oc_stream_close(stream);xz_stem_media_close(original);xz_foreign_close(set);
    puts("PASS four-part plans, source alignment, all mute combinations, stock unity and seek fallback");return 0;
}
