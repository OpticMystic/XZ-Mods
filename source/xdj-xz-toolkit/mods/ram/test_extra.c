/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#ifdef NDEBUG
#error RAM acceptance requires active assertions
#endif
#include "extra.h"
#include "budget.h"
#include "../audio/stem_media.h"
#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static atomic_uint route = 7, generation = 11;
static atomic_int close_gate, close_entered;
static struct xz_media_identity close_identity;
int __real_close(int);
int __wrap_close(int fd) {
    struct xz_media_identity id;
    if (atomic_load(&close_gate) && xz_media_identify(fd,&id) && xz_media_same(&id,&close_identity)) {
        atomic_store(&close_entered,1);
        while(atomic_load(&close_gate)){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    }
    return __real_close(fd);
}
static uint32_t get_route(void *p) { (void)p; return atomic_load(&route); }
static uint64_t get_generation(void *p,unsigned source) { (void)p; assert(source==1); return atomic_load(&generation); }
static void tick(void) { struct timespec t={0,1000000};nanosleep(&t,NULL); }
static struct xz_ram_status status(struct xz_ram_extra *s,unsigned d) { struct xz_ram_status r;xz_ram_extra_status(s,d,&r);return r; }
static void wait_state(struct xz_ram_extra *s,struct xz_ram_player_service *ops,struct xz_stem_media *m,unsigned d,enum xz_ram_state state) {
    for(unsigned i=0;i<10000;i++) {
        ops->step(s,d,m,0);struct xz_ram_status r=status(s,d);
        if(r.state==state&&r.phase==XZ_RAM_IDLE)return;
        if(r.state==XZ_RAM_ERROR&&state!=XZ_RAM_ERROR){fprintf(stderr,"RAM error: %s\n",r.message);assert(0);}
        tick();
    }
    fprintf(stderr,"timeout state %d wanted %d\n",status(s,d).state,state);assert(0);
}
static void compare(struct xz_stem_media *m,struct xz_stem_media *ref,int64_t at,size_t n) {
    float a[2048],b[2048];assert(n<=1024);
    assert(!xz_stem_media_read(m,at,n,a)&&!xz_stem_media_read(ref,at,n,b));assert(!memcmp(a,b,n*2*sizeof(float)));
}
int main(int argc,char **argv) {
    assert(argc==4||argc==5); /* reference, disposable USB1, disposable USB2, optional untrimmed MP3 */
    int untrimmed=argc==5?atoi(argv[4]):0;
    struct xz_ram_extra *s=xz_ram_extra_create(NULL,get_route,get_generation);assert(s);
    struct xz_ram_player_service ops=xz_ram_extra_player_service(s);
    struct xz_stem_media *m[2],*ref[2];struct xz_media_identity original[2];
    for(unsigned d=0;d<2;d++) {
        m[d]=xz_stem_media_open(argv[d+2],untrimmed);ref[d]=xz_stem_media_open(argv[1],untrimmed);assert(m[d]&&ref[d]);
        ops.bind(s,d,m[d],1,42,11,argv[d+2],XZ_RAM_CUES|XZ_RAM_WAVE|XZ_RAM_GRID);
        int fd;assert(xz_stem_media_source_snapshot(m[d],original+d,&fd));close(fd);
        compare(m[d],ref[d],12345,999);
    }
    struct xz_ram_status r=status(s,0);atomic_store(&route,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ROUTE_DOWN);atomic_store(&route,7);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    ops.step(s,0,m[0],0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_CANCEL,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_EMPTY);
    for(unsigned i=0;i<20;i++){ops.step(s,0,m[0],1);tick();}assert(status(s,0).state==XZ_RAM_EMPTY);
    /* New load attempt invalidates old requests, even the same track ID. */
    ops.invalidate(s,0);wait_state(s,&ops,m[0],0,XZ_RAM_EMPTY);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_STALE);r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    ops.step(s,0,m[0],0);atomic_store(&route,8);ops.step(s,0,m[0],0);
    wait_state(s,&ops,m[0],0,XZ_RAM_ERROR);r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    ops.step(s,0,m[0],0);
    for(unsigned i=0;i<10000&&status(s,0).phase!=XZ_RAM_ATTACHING;i++)tick();
    assert(status(s,0).phase==XZ_RAM_ATTACHING);
    atomic_store(&route,9);ops.step(s,0,m[0],0);wait_state(s,&ops,m[0],0,XZ_RAM_ERROR);
    r=status(s,0);assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_READY);
    /* Release at a nonaligned cursor without reopening the decoder. */
    compare(m[0],ref[0],23456,917);r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_RELEASE,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_EMPTY);compare(m[0],ref[0],24373,1024);
    int fd;struct xz_media_identity restored;assert(xz_stem_media_source_snapshot(m[0],&restored,&fd));close(fd);
    assert(xz_media_same(&restored,&original[0]));
    /* Same name, size, generation and track ID, but a different inode must never restore. */
    r=status(s,0);assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_READY);
    char backup[1024];assert(snprintf(backup,sizeof backup,"%s.previous",argv[2])<(int)sizeof backup);
    assert(!rename(argv[2],backup));
    FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");assert(in&&out);unsigned char buffer[32768];size_t n;
    while((n=fread(buffer,1,sizeof buffer,in)))assert(fwrite(buffer,1,n,out)==n);
    assert(!ferror(in));fclose(in);assert(!fclose(out));
    r=status(s,0);assert(xz_ram_extra_command(s,0,XZ_RAM_RELEASE,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_DETACHED);compare(m[0],ref[0],45678,1024);
    assert(!unlink(argv[2])&&!rename(backup,argv[2]));
    ops.retire(s,0,m[0]);m[0]=xz_stem_media_open(argv[2],untrimmed);assert(m[0]);
    ops.bind(s,0,m[0],1,42,11,argv[2],XZ_RAM_CUES|XZ_RAM_WAVE|XZ_RAM_GRID);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_STALE);
    for(unsigned d=0;d<2;d++) {
        r=status(s,d);assert(xz_ram_extra_command(s,d,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
        if(d==1){
            ops.step(s,d,m[d],0);
            for(unsigned i=0;i<10000&&status(s,d).phase!=XZ_RAM_ATTACHING;i++)tick();
            assert(status(s,d).phase==XZ_RAM_ATTACHING);
            close_identity=original[d];atomic_store(&close_gate,1);ops.step(s,d,m[d],0);
            for(unsigned i=0;i<10000&&!atomic_load(&close_entered);i++)tick();assert(atomic_load(&close_entered));
            assert(status(s,d).state==XZ_RAM_LOADING);
            assert(xz_ram_extra_command(s,d,XZ_RAM_CANCEL,&r.key)==XZ_RAM_ALREADY_HELD);
            atomic_store(&route,0);atomic_store(&generation,12);ops.step(s,d,m[d],0);
            atomic_store(&close_gate,0);wait_state(s,&ops,m[d],d,XZ_RAM_DETACHED);atomic_store(&route,9);
        }else wait_state(s,&ops,m[d],d,XZ_RAM_READY);
        struct xz_media_identity backing;assert(xz_stem_media_source_snapshot(m[d],&backing,&fd));close(fd);
        assert(backing.inode!=original[d].inode||backing.device!=original[d].device);
    }
    /* Destroy content, not merely its path: the old open descriptor can no longer supply audio. */
    for(unsigned d=0;d<2;d++){fd=open(argv[d+2],O_WRONLY|O_TRUNC);assert(fd>=0);close(fd);assert(!unlink(argv[d+2]));}
    atomic_store(&generation,12);
    uint64_t frames=xz_stem_media_frames(m[0]);
    for(unsigned d=0;d<2;d++) {
        ops.step(s,d,m[d],1);r=status(s,d);assert(r.state==XZ_RAM_DETACHED);
        assert((r.retained_assets&15)==15&&xz_ram_extra_command(s,d,XZ_RAM_RELEASE,&r.key)==XZ_RAM_REFUSED);
        for(uint64_t at=0;at<frames+1024;at+=997)compare(m[d],ref[d],(int64_t)at,997);
        for(unsigned i=0;i<100;i++)compare(m[d],ref[d],(int64_t)((uint64_t)i*197119%(frames-1024)),1024);
    }
    atomic_store(&route,0);
    for(unsigned d=0;d<2;d++){ops.step(s,d,m[d],1);r=status(s,d);assert(r.state==XZ_RAM_DETACHED&&!r.route_up&&(r.retained_assets&XZ_RAM_AUDIO));}
    struct xz_ram_usage usage;xz_ram_usage(&usage);assert(usage.original_held==2*xz_ram_round(original[0].bytes));
    for(unsigned d=0;d<2;d++){ops.retire(s,d,m[d]);xz_stem_media_close(ref[d]);}
    xz_ram_extra_destroy(s);xz_ram_usage(&usage);assert(!usage.original_held&&!usage.original_pending);
    printf("PASS two RAM decoders, route/cancel/session races, cursor-preserving release, destroyed USBs, %llu frames/decoder, seeks/EOF and exact retirement\n",(unsigned long long)frames);
}
