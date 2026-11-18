/* SPDX-License-Identifier: MIT */
#include "../proc_parse.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    struct xz_net_proc_route r={0};uint32_t parent=99;
    assert(xz_net_proc_route_parse("eth0\t00000000\t010CA8C0\t0003\t0\t0\t42760\t00000000\t0\t0\t0\n",&r));
    assert(!strcmp(r.name,"eth0")&&r.gateway==0x010CA8C0u&&r.flags==3&&r.metric==42760&&!r.destination&&!r.mask);
    assert(xz_net_proc_route_parse("a12345678901234 FFFFFFFF ffffffff FFFFFFFF 4294967295 0 4294967295 ffffffff",&r));
    assert(r.metric==UINT32_MAX&&r.mask==UINT32_MAX);
    const char *bad[]={"", "Iface Destination Gateway Flags RefCnt Use Metric Mask", "a123456789012345 0 0 0 0 0 0 0",
        "eth0 100000000 0 0 0 0 0 0","eth0 0 0 0 4294967296 0 0 0","eth0 0 0 0 0 0 4294967296 0",
        "eth0 0 0 0 0 0 -1 0","eth0 0 0 0 0 0 +1 0","eth0 0 0 0 0 0 0","eth0 0 0 0 0 0 1x 0"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(!xz_net_proc_route_parse(bad[i],&r));
    assert(xz_net_proc_parent("123 (xz client (copy)) S 42 123 0 0",&parent)&&parent==42);
    assert(xz_net_proc_parent("1 (init) R 0 1 1",&parent)&&parent==0);
    assert(xz_net_proc_parent("9 (a)\tS\t2147483647\t0",&parent)&&parent==INT32_MAX);
    const char *bad_parent[]={"", "123 command S 4", "123 (cmd) SS 4", "123 (cmd) S -4", "123 (cmd) S 2147483648", "123 (cmd) S 4x", "123 (cmd) S", "123 (cmd)"};
    for(unsigned i=0;i<sizeof bad_parent/sizeof bad_parent[0];i++)assert(!xz_net_proc_parent(bad_parent[i],&parent));
    puts("PASS proc route/parent parsing: real field shapes, limits, spacing, nested names and malformed input");return 0;
}
