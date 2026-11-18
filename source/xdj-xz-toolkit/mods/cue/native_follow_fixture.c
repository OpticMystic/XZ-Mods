/* Emulator-only fixture. Never run this binary on the controller. */
#include "native.h"
#include <stddef.h>
int32_t test_target_ms;
void *memcpy(void *out,const void *in,size_t size){unsigned char *d=out;const unsigned char *s=in;for(size_t i=0;i<size;i++)d[i]=s[i];return out;}
void *memset(void *out,int value,size_t size){unsigned char *d=out;for(size_t i=0;i<size;i++)d[i]=(unsigned char)value;return out;}
int memcmp(const void *left,const void *right,size_t size){const unsigned char *a=left,*b=right;for(size_t i=0;i<size;i++)if(a[i]!=b[i])return (int)a[i]-(int)b[i];return 0;}
int xz_read_memory(uint32_t address,void *out,size_t size){(void)address;(void)out;(void)size;return -1;}
int test_follow(int deck,int32_t at,int verified){
    struct xz_cue_native native={{(void*)0x10030000,(void*)0x10031000},verified,{0,0}};
    struct xz_cue_api api=xz_cue_native_api(&native);
    test_target_ms=at;
    return api.follow_memory(api.context,deck,at);
}
int test_smart_cue(int deck,int32_t at,int verified){
    struct xz_cue_native native={{(void*)0x10030000,(void*)0x10031000},verified,{0,0}};
    struct xz_cue cues;
    struct xz_cue_event event={.deck=deck,.pad=0,.operation=0,.hotcue_mode=1,.mode_button=-1};
    test_target_ms=at;
    xz_cue_init(&cues,xz_cue_native_api(&native));xz_cue_settings(&cues,0,1);
    if(xz_cue_before(&cues,&event))return -2;
    xz_cue_after(&cues,&event,1);
    event.operation=2;
    if(xz_cue_before(&cues,&event))return -3;
    xz_cue_after(&cues,&event,1);
    return cues.deck[deck].last_error==XZ_CUE_OK;
}
void _start(void){volatile int keep=test_follow(0,0,0)+test_smart_cue(0,0,0);(void)keep;}
