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
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
static atomic_uint route = 7, generation = 11;
static atomic_int close_gate, close_entered;
static atomic_int read_gate, read_entered;
static struct xz_media_identity close_identity;
int __real_close(int);
ssize_t __real_pread64(int,void *,size_t,off64_t);
ssize_t __wrap_pread64(int fd,void *out,size_t bytes,off64_t offset) {
    if(atomic_load(&read_gate)){
        atomic_store(&read_entered,1);
        while(atomic_load(&read_gate)){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    }
    return __real_pread64(fd,out,bytes,offset);
}
/* The deck's step runs on its real-time thread. A close of a stick descriptor there, or a dup2 over one (dup2
   closes what it replaces), is a FLUSH request to the stick's FUSE daemon that waits behind every read queued at
   the stick. Counted per file of the two disposable USB paths, while the test thread is inside step(). */
static const char *stick_path[2];
static _Thread_local int in_step;
static atomic_int stick_closed_in_step;
static int is_stick(int fd) {
    struct stat a, b;
    if (fstat(fd, &a)) return 0;
    for (unsigned d = 0; d < 2; d++)
        if (stick_path[d] && !stat(stick_path[d], &b) && a.st_dev == b.st_dev && a.st_ino == b.st_ino) return 1;
    return 0;
}
int __wrap_close(int fd) {
    struct xz_media_identity id;
    if (in_step && is_stick(fd)) atomic_fetch_add(&stick_closed_in_step, 1);
    if (atomic_load(&close_gate) && xz_media_identify(fd,&id) && xz_media_same(&id,&close_identity)) {
        atomic_store(&close_entered,1);
        while(atomic_load(&close_gate)){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    }
    return __real_close(fd);
}
int __real_dup2(int, int);
int __wrap_dup2(int from, int to) {
    if (in_step && from != to && is_stick(to)) atomic_fetch_add(&stick_closed_in_step, 1);
    return __real_dup2(from, to);
}
static int (*real_step)(void *, unsigned, struct xz_stem_media *, int);
static int counted_step(void *s, unsigned d, struct xz_stem_media *m, int use) {
    in_step = 1; int r = real_step(s, d, m, use); in_step = 0; return r;
}
static atomic_uint lead_count[2], lead_most;
static _Atomic uint64_t lead_first[2], lead_total[2];
static void lead_seen(unsigned deck, uint64_t from, uint64_t bytes) {
    assert(deck < 2);
    if (!atomic_load(&lead_count[deck])) atomic_store(&lead_first[deck], from);
    if (bytes > atomic_load(&lead_most)) atomic_store(&lead_most, (unsigned)bytes);
    atomic_fetch_add(&lead_total[deck], bytes); atomic_fetch_add(&lead_count[deck], 1);
}
static void lead_reset(void) {
    for (unsigned d = 0; d < 2; d++) { atomic_store(&lead_count[d], 0); atomic_store(&lead_total[d], 0); }
    atomic_store(&lead_most, 0);
}
static void lead_until(unsigned deck, uint64_t total) {
    for(unsigned i=0;i<8000&&atomic_load(&lead_total[deck])<total;i++){struct timespec t={0,1000000};nanosleep(&t,NULL);}
}
static uint32_t get_route(void *p) { (void)p; return atomic_load(&route); }
static uint64_t get_generation(void *p,unsigned source) { (void)p; assert(source==1); return atomic_load(&generation); }
static void tick(void) { struct timespec t={0,1000000};nanosleep(&t,NULL); }
static struct xz_ram_status status(struct xz_ram_extra *s,unsigned d) { struct xz_ram_status r;xz_ram_extra_status(s,d,&r);return r; }
static void block_copy(struct xz_ram_extra *s,struct xz_ram_player_service *ops,struct xz_stem_media *m,unsigned d){
    atomic_store(&read_entered,0);atomic_store(&read_gate,1);
    for(unsigned i=0;i<10000&&!atomic_load(&read_entered);i++){ops->step(s,d,m,0);tick();}
    assert(atomic_load(&read_entered));
}
static void await_candidate(struct xz_ram_extra *s,struct xz_ram_player_service *ops,struct xz_stem_media *m,unsigned d){
    block_copy(s,ops,m,d);atomic_store(&read_gate,0);
    for(unsigned i=0;i<10000&&status(s,d).phase!=XZ_RAM_ATTACHING;i++)tick();
    assert(status(s,d).phase==XZ_RAM_ATTACHING);
}
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
    xz_ram_extra_lead_probe=lead_seen; /* before any deck is bound: leading starts with the load */
    struct xz_ram_player_service ops=xz_ram_extra_player_service(s);
    real_step=ops.step;ops.step=counted_step;stick_path[0]=argv[2];stick_path[1]=argv[3];
    struct xz_stem_media *m[2],*ref[2];struct xz_media_identity original[2];
    for(unsigned d=0;d<2;d++) {
        m[d]=xz_stem_media_open(argv[d+2],untrimmed);ref[d]=xz_stem_media_open(argv[1],untrimmed);assert(m[d]&&ref[d]);
        ops.bind(s,d,m[d],1,42,11,argv[d+2],XZ_RAM_CUES|XZ_RAM_WAVE|XZ_RAM_GRID);
        int fd;assert(xz_stem_media_source_snapshot(m[d],original+d,&fd));close(fd);
        compare(m[d],ref[d],12345,999);
    }
    /* Leading (extra.c). A loaded deck has the megabyte after its reader's place read ahead before it ever plays,
       in passes of a quarter megabyte at most: deck 0 says where its reader is, deck 1 says nothing and is led
       from the head of its file. No copy is asked for: this worker never says "loaded" (the manual path). */
    uint64_t file_bytes=original[0].bytes,lead=file_bytes<(1u<<20)?file_bytes:(1u<<20);
    ops.reading(s,0,1u);
    lead_until(0,lead);lead_until(1,lead);
    for(unsigned i=0;i<300;i++){ops.step(s,0,m[0],0);tick();}
    assert(atomic_load(&lead_total[0])==lead&&atomic_load(&lead_total[1])==lead);   /* and nothing twice */
    assert(!atomic_load(&lead_first[0])&&!atomic_load(&lead_first[1])&&atomic_load(&lead_most)<=(256u<<10));
    assert(status(s,0).state==XZ_RAM_EMPTY);
    struct xz_ram_status r=status(s,0);atomic_store(&route,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ROUTE_DOWN);atomic_store(&route,7);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    ops.step(s,0,m[0],0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_CANCEL,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_EMPTY);
    for(unsigned i=0;i<20;i++){ops.step(s,0,m[0],1);tick();}assert(status(s,0).state==XZ_RAM_EMPTY);
    /* The deck now sounds from the stick and gets no copy (cancelled above). What was read for it is not read
       again; a jump either way is followed, sounding or cued. */
    lead_reset();
    for(unsigned i=0;i<400;i++)tick();
    assert(!atomic_load(&lead_count[0]));
    if(file_bytes>lead+8192u){
        uint64_t far=(lead+8192u)&~(uint64_t)4095u,rest=file_bytes-far<(1u<<20)?file_bytes-far:(1u<<20);
        ops.reading(s,0,(uint32_t)(far>>12)+1u);               /* a jump past what was read */
        lead_until(0,rest);for(unsigned i=0;i<300;i++)tick();
        assert(atomic_load(&lead_total[0])==rest&&atomic_load(&lead_first[0])==far);
        lead_reset();ops.step(s,0,m[0],0);ops.reading(s,0,1u);  /* cued now, and back to the start */
        lead_until(0,lead);for(unsigned i=0;i<300;i++)tick();
        assert(atomic_load(&lead_total[0])==lead&&!atomic_load(&lead_first[0]));
    }
    /* A reader that has already read somewhere when it is bound (a load reads the first buffer at the cue point,
       extra_player.c) is led from there at once, not from the head of its file: nobody has to say where it is. */
    {
        ops.retire(s,1,m[1]);m[1]=xz_stem_media_open(argv[3],untrimmed);assert(m[1]);
        float pcm[2048];int64_t cue=(int64_t)(xz_stem_media_frames(m[1])/2u);
        assert(!xz_stem_media_read(m[1],cue,1024,pcm));
        uint64_t at=xz_stem_media_source_offset(m[1]),from=(at-1u)&~(uint64_t)4095u;assert(at>4096u);
        lead_reset();ops.bind(s,1,m[1],1,42,11,argv[3],XZ_RAM_CUES|XZ_RAM_WAVE|XZ_RAM_GRID);
        for(unsigned i=0;i<4000&&!atomic_load(&lead_count[1]);i++)tick();
        assert(atomic_load(&lead_count[1]));
        if(atomic_load(&lead_first[1])!=from)fprintf(stderr,"lead of a bound reader at %llu began at %llu\n",
            (unsigned long long)from,(unsigned long long)atomic_load(&lead_first[1]));
        assert(atomic_load(&lead_first[1])==from);
        uint64_t rest=original[1].bytes-from<(1u<<20)?original[1].bytes-from:(1u<<20);
        lead_until(1,rest);for(unsigned i=0;i<300;i++)tick();assert(atomic_load(&lead_total[1])==rest);
    }
    ops.step(s,0,m[0],0);xz_ram_extra_lead_probe=NULL;
    /* New load attempt invalidates old requests, even the same track ID. */
    ops.invalidate(s,0);wait_state(s,&ops,m[0],0,XZ_RAM_EMPTY);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_STALE);r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    ops.step(s,0,m[0],0);atomic_store(&route,8);ops.step(s,0,m[0],0);
    wait_state(s,&ops,m[0],0,XZ_RAM_ERROR);r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    await_candidate(s,&ops,m[0],0);
    atomic_store(&route,9);ops.step(s,0,m[0],0);wait_state(s,&ops,m[0],0,XZ_RAM_ERROR);
    r=status(s,0);assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_READY);
    /* The attach on the deck's thread closed no stick descriptor, not even through dup2. */
    if(atomic_load(&stick_closed_in_step))fprintf(stderr,"step closed %d stick descriptors\n",atomic_load(&stick_closed_in_step));
    assert(!atomic_load(&stick_closed_in_step));
    /* Release at a nonaligned cursor without reopening the decoder. */
    compare(m[0],ref[0],23456,917);r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_RELEASE,&r.key)==XZ_RAM_ACCEPTED);
    wait_state(s,&ops,m[0],0,XZ_RAM_EMPTY);compare(m[0],ref[0],24373,1024);
    int fd;struct xz_media_identity restored;assert(xz_stem_media_source_snapshot(m[0],&restored,&fd));close(fd);
    assert(xz_media_same(&restored,&original[0]));
    /* Replacing a reader cannot wait for a blocked background USB read. */
    r=status(s,0);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_ACCEPTED);block_copy(s,&ops,m[0],0);
    ops.retire(s,0,m[0]);
    m[0]=xz_stem_media_open(argv[2],untrimmed);assert(m[0]);
    ops.bind(s,0,m[0],1,42,11,argv[2],XZ_RAM_CUES|XZ_RAM_WAVE|XZ_RAM_GRID);
    assert(xz_ram_extra_command(s,0,XZ_RAM_LOAD,&r.key)==XZ_RAM_STALE);
    atomic_store(&read_gate,0);
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
            await_candidate(s,&ops,m[d],d);
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
    assert(!atomic_load(&stick_closed_in_step)); /* every attach, release and cleanup above */
    struct xz_ram_usage usage;xz_ram_usage(&usage);assert(usage.original_held==2*xz_ram_round(original[0].bytes));
    for(unsigned d=0;d<2;d++){ops.retire(s,d,m[d]);xz_stem_media_close(ref[d]);}
    xz_ram_extra_destroy(s);xz_ram_usage(&usage);assert(!usage.original_held&&!usage.original_pending);
    printf("PASS two RAM decoders, route/cancel/session races, cursor-preserving release, destroyed USBs, %llu frames/decoder, seeks/EOF and exact retirement\n",(unsigned long long)frames);
}
