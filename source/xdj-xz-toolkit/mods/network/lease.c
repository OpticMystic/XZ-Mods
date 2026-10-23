/* SPDX-License-Identifier: MIT */
#include "linux.h"
#include <string.h>
enum xz_net_result xz_net_lease_encode(const struct xz_net_lease *l,unsigned char out[64]) {
    struct xz_net_identity id={.ifindex=1,.identity_epoch=1};memcpy(id.name,l->interface,16);
    if(xz_net_validate_identity(&id)||l->kind<1||l->kind>4||l->has_gateway>1)return XZ_NET_INVALID;
    if(l->kind<=XZ_NET_LEASE_RENEW&&(!l->seconds||!l->address.prefix||l->address.prefix>32||l->address.ip.octet[0]==0||l->address.ip.octet[0]==127||l->address.ip.octet[0]>=224))return XZ_NET_INVALID;
    memset(out,0,64);memcpy(out,"XZNL",4);out[4]=1;out[5]=(unsigned char)l->kind;
    memcpy(out+8,l->interface,strlen(l->interface));
    if(l->kind<=XZ_NET_LEASE_RENEW){out[6]=l->address.prefix;out[7]=l->has_gateway;memcpy(out+24,l->address.ip.octet,4);memcpy(out+28,l->gateway.octet,4);for(unsigned i=0;i<4;i++)out[32+i]=(unsigned char)(l->seconds>>(24-8*i));}
    return XZ_NET_OK;
}
enum xz_net_result xz_net_lease_decode(const unsigned char in[64],struct xz_net_lease *l) {
    if(memcmp(in,"XZNL",4)||in[4]!=1||in[5]<1||in[5]>4||in[7]>1||!memchr(in+8,0,16))return XZ_NET_INVALID;
    memset(l,0,sizeof *l);l->kind=(enum xz_net_lease_kind)in[5];memcpy(l->interface,in+8,16);
    l->address.prefix=in[6];l->has_gateway=in[7];memcpy(l->address.ip.octet,in+24,4);memcpy(l->gateway.octet,in+28,4);
    for(unsigned i=0;i<4;i++)l->seconds=l->seconds*256+in[32+i];
    unsigned char canonical[64];if(xz_net_lease_encode(l,canonical)||memcmp(canonical,in,64))return XZ_NET_INVALID;return XZ_NET_OK;
}
