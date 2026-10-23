/* SPDX-License-Identifier: MIT */
#ifndef XZ_RECEIVER_NETWORK_ADDRESS_H
#define XZ_RECEIVER_NETWORK_ADDRESS_H
#include <arpa/inet.h>
#include <net/if.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

typedef int (*xz_network_address_fn_v1)(char out[16]);

/* The optional service supplies only a verified current preference. Keep the
 * existing eth0 fallback for old runtimes and a service that has not applied. */
static inline int xz_receiver_network_ipv4(int fd,xz_network_address_fn_v1 preferred,char out[16]) {
    char text[16]={0};struct in_addr address;
    memset(out,0,16);
    if(preferred&&preferred(text)==1&&memchr(text,0,sizeof text)&&inet_pton(AF_INET,text,&address)==1){
        uint32_t first=ntohl(address.s_addr)>>24;
        if(first&&first!=127&&first<224&&inet_ntop(AF_INET,&address,out,16))return 1;
    }
    if(fd<0)return 0;
    struct ifreq request;memset(&request,0,sizeof request);
    memcpy(request.ifr_name,"eth0",5);
    if(ioctl(fd,SIOCGIFADDR,&request))return 0;
    const struct sockaddr_in *value=(const struct sockaddr_in *)&request.ifr_addr;
    return inet_ntop(AF_INET,&value->sin_addr,out,16)!=NULL;
}
#endif
