/* SPDX-License-Identifier: MIT */
#define main seek_regression_main
#include "test_overcue_seek.c"
#undef main
int main(void){
    struct xz_oc_stream s;init(&s);float pcm[BLOCK*2];uint32_t trace[16];
    block(&s,15000,pcm,trace);assert(uniform(pcm,0));assert(s.starved==XZ_OC_STARVE_HELD);
    for(unsigned i=0;i<1100;i++)block(&s,15000,pcm,trace);
    assert(s.starved==XZ_OC_STARVE_UNITY&&uniform(pcm,.75f));cleanup(&s);
    puts("PASS uncovered muted mix holds silence transiently and restores native unity after bounded starvation");
    return 0;
}
