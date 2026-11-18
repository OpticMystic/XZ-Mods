/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "dj_name_key.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dj_name_key_table.h"

/* NFC as in UAX #15: full canonical decomposition, canonical ordering, then canonical composition, over the whole
   name at once. Working entries are ccc<<24|cp. An invalid UTF-8 byte b becomes the entry RAW+b: a starter that
   never decomposes or composes, so it blocks composition across it and is written back as the same byte. */
#define RAW 0x110000u
#define CP 0x1FFFFFu
#define SB 0xAC00u
#define LB 0x1100u
#define VB 0x1161u
#define TB 0x11A7u
#define LCOUNT 19u
#define VCOUNT 21u
#define TCOUNT 28u
#define NCOUNT (VCOUNT*TCOUNT)
#define SCOUNT (LCOUNT*NCOUNT)
#define COUNT(a) (sizeof(a)/sizeof*(a))
#define LOCAL 256

static unsigned ccc(uint32_t c){
    if(c<0x300||c>=RAW)return 0;
    size_t lo=0,hi=COUNT(xz_dj_ccc_run);
    while(lo<hi){size_t m=lo+(hi-lo)/2;uint32_t start=xz_dj_ccc_run[m]>>11;
        if(c<start)hi=m;else if(c-start>(xz_dj_ccc_run[m]&0x7FF))lo=m+1;else return xz_dj_ccc_class[m];}
    return 0;
}
static const uint64_t *decomposition(uint32_t c){
    if(c<0xC0||c>=RAW)return NULL;
    size_t lo=0,hi=COUNT(xz_dj_decomp);
    while(lo<hi){size_t m=lo+(hi-lo)/2;uint32_t k=(uint32_t)(xz_dj_decomp[m]>>42);
        if(c<k)hi=m;else if(c>k)lo=m+1;else return xz_dj_decomp+m;}
    return NULL;
}
static uint32_t compose(uint32_t a,uint32_t b){
    if(a-LB<LCOUNT&&b-VB<VCOUNT)return SB+((a-LB)*VCOUNT+(b-VB))*TCOUNT;
    if(a-SB<SCOUNT&&(a-SB)%TCOUNT==0&&b-TB-1<TCOUNT-1)return a+(b-TB);
    if(b<0x300||b>=RAW||a>=RAW)return 0;
    uint64_t want=(uint64_t)a<<21|b,mask=((uint64_t)1<<42)-1;
    size_t lo=0,hi=COUNT(xz_dj_compose);
    while(lo<hi){size_t m=lo+(hi-lo)/2;uint64_t e=xz_dj_decomp[xz_dj_compose[m]];
        if(want<(e&mask))hi=m;else if(want>(e&mask))lo=m+1;else return (uint32_t)(e>>42);}
    return 0;
}
/* Mappings nest a few levels at most (the generator asserts under 8), so the recursion stays shallow. */
static size_t decompose(uint32_t c,uint32_t *w){
    if(c-SB<SCOUNT){uint32_t s=c-SB;w[0]=LB+s/NCOUNT;w[1]=VB+s%NCOUNT/TCOUNT;w[2]=TB+s%TCOUNT;return s%TCOUNT?3:2;}
    const uint64_t *d=decomposition(c);
    if(!d){w[0]=ccc(c)<<24|c;return 1;}
    size_t k=decompose((uint32_t)(*d>>21)&CP,w);
    uint32_t second=(uint32_t)*d&CP;
    return second?k+decompose(second,w+k):k;
}
static size_t utf8(const unsigned char *p,size_t n,uint32_t *c){
    unsigned b=p[0];size_t len;uint32_t v,min;
    if(b<0x80){*c=b;return 1;}
    if(b<0xC2)return 0;
    if(b<0xE0){len=2;v=b&0x1F;min=0x80;}else if(b<0xF0){len=3;v=b&0x0F;min=0x800;}else if(b<0xF5){len=4;v=b&7;min=0x10000;}else return 0;
    if(n<len)return 0;
    for(size_t i=1;i<len;i++){if((p[i]&0xC0)!=0x80)return 0;v=v<<6|(p[i]&0x3F);}
    if(v<min||v>0x10FFFF||(v>=0xD800&&v<=0xDFFF))return 0;
    *c=v;return len;
}

int xz_dj_name_key(const char *s,size_t n,char *out,size_t cap){
    const unsigned char *p=(const unsigned char*)s;
    if(n>(size_t)(INT_MAX-1)/3||n>SIZE_MAX/sizeof(uint32_t)/XZ_DJ_NAME_KEY_DECOMP_PER_BYTE)return -1;
    size_t i=0;
    while(i<n&&p[i]<0x80)i++;
    if(i==n){
        if(cap<n+1)return -1;
        for(i=0;i<n;i++)out[i]=(char)(p[i]>='A'&&p[i]<='Z'?p[i]+32:p[i]);
        out[n]=0;return (int)n;
    }
    uint32_t local[LOCAL],*w=local;
    size_t room=n*XZ_DJ_NAME_KEY_DECOMP_PER_BYTE;
    if(room>LOCAL&&!(w=malloc(room*sizeof*w)))return -1;
    size_t k=0;
    for(i=0;i<n;){uint32_t c;size_t len=utf8(p+i,n-i,&c);
        if(len){k+=decompose(c,w+k);i+=len;}else w[k++]=RAW+p[i++];}
    /* Stable insertion sort by combining class; starters (class 0) bound every run. */
    for(size_t j=1;j<k;j++){uint32_t e=w[j];size_t m=j;
        if(!(e>>24))continue;
        while(m>0&&(w[m-1]>>24)>(e>>24)){w[m]=w[m-1];m--;}
        w[m]=e;}
    size_t o=0;
    if(k){
        size_t starter=0;unsigned last=w[0]>>24?256:0;o=1;
        for(size_t j=1;j<k;j++){uint32_t e=w[j],x;unsigned cl=e>>24;
            if((last<cl||last==0)&&(x=compose(w[starter]&CP,e&CP))){w[starter]=x;continue;}
            if(!cl)starter=o;
            last=cl;w[o++]=e;}
    }
    size_t q=0;int ok=1;
    for(size_t j=0;j<o&&ok;j++){uint32_t c=w[j]&CP;unsigned char b[4];size_t len;
        if(c>=RAW){b[0]=(unsigned char)(c-RAW);len=1;}
        else if(c<0x80){b[0]=(unsigned char)(c>='A'&&c<='Z'?c+32:c);len=1;}
        else if(c<0x800){b[0]=(unsigned char)(0xC0|c>>6);b[1]=(unsigned char)(0x80|(c&0x3F));len=2;}
        else if(c<0x10000){b[0]=(unsigned char)(0xE0|c>>12);b[1]=(unsigned char)(0x80|(c>>6&0x3F));b[2]=(unsigned char)(0x80|(c&0x3F));len=3;}
        else{b[0]=(unsigned char)(0xF0|c>>18);b[1]=(unsigned char)(0x80|(c>>12&0x3F));b[2]=(unsigned char)(0x80|(c>>6&0x3F));b[3]=(unsigned char)(0x80|(c&0x3F));len=4;}
        if(cap<q+len+1)ok=0;else{memcpy(out+q,b,len);q+=len;}}
    if(w!=local)free(w);
    if(!ok)return -1;
    out[q]=0;return (int)q;
}

int xz_dj_name_key_compare(const char *a,const char *b){
    size_t na=strlen(a),nb=strlen(b);
    char la[3*LOCAL+1],lb[3*LOCAL+1];
    char *ka=XZ_DJ_NAME_KEY_CAP(na)<=sizeof la?la:malloc(XZ_DJ_NAME_KEY_CAP(na));
    char *kb=XZ_DJ_NAME_KEY_CAP(nb)<=sizeof lb?lb:malloc(XZ_DJ_NAME_KEY_CAP(nb));
    int x=ka?xz_dj_name_key(a,na,ka,XZ_DJ_NAME_KEY_CAP(na)):-1,y=kb?xz_dj_name_key(b,nb,kb,XZ_DJ_NAME_KEY_CAP(nb)):-1;
    int r;
    if(x<0||y<0){r=strcmp(a,b);}
    else{size_t m=(size_t)(x<y?x:y);r=memcmp(ka,kb,m);if(!r)r=(x>y)-(x<y);}
    if(ka!=la)free(ka);
    if(kb!=lb)free(kb);
    return (r>0)-(r<0);
}
