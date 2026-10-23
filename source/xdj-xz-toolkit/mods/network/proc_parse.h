/* SPDX-License-Identifier: MIT. Bounded Linux proc text without newer scanf ABI imports. */
#ifndef XZ_NETWORK_PROC_PARSE_H
#define XZ_NETWORK_PROC_PARSE_H
#include <stdint.h>
#include <string.h>
struct xz_net_proc_route {char name[16];uint32_t destination,gateway,flags,metric,mask;};
static inline int xz_net_proc_space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static inline int xz_net_proc_number(const char **cursor,unsigned base,uint32_t *out){
    const char *p=*cursor;while(xz_net_proc_space(*p))p++;
    uint32_t value=0;unsigned count=0;
    for(;;){unsigned digit;
        if(*p>='0'&&*p<='9')digit=(unsigned)(*p-'0');
        else if(*p>='a'&&*p<='f')digit=(unsigned)(*p-'a')+10;
        else if(*p>='A'&&*p<='F')digit=(unsigned)(*p-'A')+10;
        else break;
        if(digit>=base||value>(UINT32_MAX-digit)/base)return 0;
        value=value*base+digit;p++;count++;
    }
    if(!count||(*p&&!xz_net_proc_space(*p)))return 0;
    *cursor=p;*out=value;return 1;
}
static inline int xz_net_proc_route_parse(const char *text,struct xz_net_proc_route *out){
    if(!text||!out)return 0;
    struct xz_net_proc_route result={0};while(xz_net_proc_space(*text))text++;
    unsigned n=0;while(*text&&!xz_net_proc_space(*text)){
        if(n==sizeof result.name-1)return 0;
        result.name[n++]=*text++;
    }
    if(!n)return 0;
    uint32_t values[7];const unsigned bases[]={16,16,16,10,10,10,16};
    for(unsigned i=0;i<7;i++)if(!xz_net_proc_number(&text,bases[i],values+i))return 0;
    result.destination=values[0];result.gateway=values[1];result.flags=values[2];result.metric=values[5];result.mask=values[6];
    *out=result;return 1;
}
static inline int xz_net_proc_parent(const char *text,uint32_t *parent){
    if(!text||!parent)return 0;
    const char *p=strrchr(text,')');if(!p||!xz_net_proc_space(*++p))return 0;
    while(xz_net_proc_space(*p))p++;
    if(!*p||xz_net_proc_space(*p)||!xz_net_proc_space(p[1]))return 0;
    p++;
    uint32_t value;if(!xz_net_proc_number(&p,10,&value)||value>INT32_MAX)return 0;
    *parent=value;return 1;
}
#endif
