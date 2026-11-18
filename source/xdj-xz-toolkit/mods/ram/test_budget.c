/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error RAM acceptance requires active assertions
#endif
#include "budget.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
static uint64_t free_bytes;
static uint64_t sample(void) { return free_bytes; }
static void empty(void) {
    struct xz_ram_usage u; xz_ram_usage(&u);
    assert(!u.original_pending && !u.original_held && !u.assets_pending && !u.assets_held);
}
static void *contend(void *context) {
    struct xz_ram_ticket *t = context;
    for (unsigned n = 0; n < 1000; ++n) {
        if (xz_ram_reserve(t, XZ_RAM_ORIGINAL, 32u << 20) == XZ_RAM_ADMITTED) {
            xz_ram_publish(t); xz_ram_return(t);
        }
    }
    return NULL;
}
int main(void) {
    xz_ram_free_sampler(sample); free_bytes = UINT64_MAX;
    struct xz_ram_ticket t[5] = {{0}};
    assert(xz_ram_reserve(t, XZ_RAM_ORIGINAL, 0) == XZ_RAM_BAD_SIZE);
    assert(xz_ram_reserve(t, XZ_RAM_ORIGINAL, UINT64_MAX) == XZ_RAM_BAD_SIZE);
    assert(xz_ram_reserve(t, XZ_RAM_ORIGINAL, XZ_RAM_TRACK_LIMIT + 1ull) == XZ_RAM_BAD_SIZE);
    assert(xz_ram_reserve(t, XZ_RAM_ORIGINAL, XZ_RAM_TRACK_LIMIT) == XZ_RAM_ADMITTED);
    assert(xz_ram_reserve(t+1, XZ_RAM_ORIGINAL, XZ_RAM_TRACK_LIMIT-1) == XZ_RAM_ADMITTED);
    assert(xz_ram_reserve(t+2, XZ_RAM_ORIGINAL, 1) == XZ_RAM_FULL);
    xz_ram_publish(t); xz_ram_publish(t+1);
    assert(xz_ram_reserve(t+2, XZ_RAM_ORIGINAL, 1) == XZ_RAM_FULL);
    xz_ram_return(t); xz_ram_return(t+1); empty();
    uint64_t page = xz_ram_round(1);
    free_bytes = XZ_RAM_RESERVE + page - 1;
    assert(xz_ram_reserve(t, XZ_RAM_ORIGINAL, 1) == XZ_RAM_LOW_MEMORY);
    ++free_bytes; assert(xz_ram_reserve(t, XZ_RAM_ORIGINAL, 1) == XZ_RAM_ADMITTED);
    assert(t->bytes == page && xz_ram_reserve(t+1, XZ_RAM_ORIGINAL, 1) == XZ_RAM_LOW_MEMORY);
    xz_ram_return(t); xz_ram_return(t); empty();
    free_bytes = XZ_RAM_RESERVE + XZ_RAM_TOTAL_LIMIT + page - 1;
    assert(xz_ram_reserve(t, XZ_RAM_ASSETS, 1) == XZ_RAM_LOW_MEMORY);
    ++free_bytes; assert(xz_ram_reserve(t, XZ_RAM_ASSETS, 1) == XZ_RAM_ADMITTED);
    assert(xz_ram_reserve(t+1, XZ_RAM_ASSETS, 1) == XZ_RAM_LOW_MEMORY);
    xz_ram_return(t);
    /* Every ordering of the four original owners shares the same allowance. */
    free_bytes = UINT64_MAX;
    for (unsigned a=0;a<4;a++) for(unsigned b=0;b<4;b++) for(unsigned c=0;c<4;c++) for(unsigned d=0;d<4;d++) {
        if(a==b||a==c||a==d||b==c||b==d||c==d)continue;
        unsigned order[]={a,b,c,d};
        for(unsigned i=0;i<4;i++)assert(xz_ram_reserve(t+order[i],XZ_RAM_ORIGINAL,32u<<20)==XZ_RAM_ADMITTED);
        assert(xz_ram_reserve(t+4,XZ_RAM_ORIGINAL,1)==XZ_RAM_FULL);
        for(unsigned i=0;i<4;i++){xz_ram_publish(t+order[i]);xz_ram_return(t+order[i]);}
        empty();
    }
    pthread_t workers[5];
    for(unsigned i=0;i<5;i++)assert(!pthread_create(workers+i,NULL,contend,t+i));
    for(unsigned i=0;i<5;i++)assert(!pthread_join(workers[i],NULL));
    empty(); xz_ram_free_sampler(NULL);
    puts("PASS shared original/assets ledger: boundaries, page rounding, 24 deck orders, pending/held and concurrent admission");
}
