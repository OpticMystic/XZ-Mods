/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "receiver_address.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
static int calls,io_failure,answer=1;
static const char *wanted="192.0.2.27";
static int unterminated;
int __wrap_ioctl(int fd,unsigned long command,...) {
    assert(fd==7&&command==SIOCGIFADDR);calls++;
    va_list ap;va_start(ap,command);struct ifreq *request=va_arg(ap,struct ifreq *);va_end(ap);
    assert(!strcmp(request->ifr_name,"eth0"));
    if(io_failure)return -1;
    struct sockaddr_in *address=(struct sockaddr_in *)&request->ifr_addr;
    address->sin_family=AF_INET;assert(inet_pton(AF_INET,"169.254.168.59",&address->sin_addr)==1);return 0;
}
#if defined(__arm__)
/* The host cross sysroot redirects ioctl to its time64 ABI. */
int __wrap___ioctl_time64(int,unsigned long,...) __attribute__((alias("__wrap_ioctl")));
#endif
static int provider(char out[16]) {
    if(unterminated)memset(out,'1',16);else snprintf(out,16,"%s",wanted);
    return answer;
}
int main(void) {
    char out[16];
    int got=xz_receiver_network_ipv4(7,NULL,out);
    if(!got||strcmp(out,"169.254.168.59")||calls!=1)fprintf(stderr,"fallback got=%d ip=%s calls=%d\n",got,out,calls);
    assert(got&&!strcmp(out,"169.254.168.59")&&calls==1);
    calls=0;assert(xz_receiver_network_ipv4(7,provider,out)&&!strcmp(out,wanted)&&calls==0);
    assert(xz_receiver_network_ipv4(-1,provider,out)&&!strcmp(out,wanted)&&calls==0);
    answer=0;assert(xz_receiver_network_ipv4(7,provider,out)&&!strcmp(out,"169.254.168.59"));
    answer=2;assert(xz_receiver_network_ipv4(7,provider,out)&&!strcmp(out,"169.254.168.59"));
    answer=1;
    const char *bad[]={"", "0.0.0.0", "127.0.0.1", "224.0.0.1", "255.255.255.255", "1.2.3", "192.0.2.999", "bad"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
        wanted=bad[i];int before=calls;
        assert(xz_receiver_network_ipv4(7,provider,out)&&!strcmp(out,"169.254.168.59")&&calls==before+1);
    }
    unterminated=1;assert(xz_receiver_network_ipv4(7,provider,out)&&!strcmp(out,"169.254.168.59"));unterminated=0;
    io_failure=1;memset(out,0x55,16);assert(!xz_receiver_network_ipv4(7,provider,out));
    for(unsigned i=0;i<16;i++)assert(out[i]==0);
    wanted="198.18.0.10";assert(xz_receiver_network_ipv4(7,provider,out)&&!strcmp(out,wanted));
    answer=0;assert(!xz_receiver_network_ipv4(-1,provider,out)&&!out[0]);
    puts("PASS receiver preferred IPv4, legacy fallback, unavailable and malformed provider");return 0;
}
