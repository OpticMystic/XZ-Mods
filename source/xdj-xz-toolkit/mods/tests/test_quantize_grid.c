#include "shell_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char records[3*0x8c];
static uint32_t count,list_pointer;
static int guard_bad,read_fail,torn;
static unsigned reads,checks;
static const unsigned char code[16]={0x08,0x31,0x9f,0xe5,0x01,0x00,0x51,0xe3,0xf0,0x05,0x2d,0xe9,0x41,0x12,0xa0,0x13};
static void put(unsigned offset,uint32_t value){memcpy(records+offset,&value,4);}
static int read_fixture(uintptr_t at,void *out,size_t n){
    if(read_fail)return -1;
    if(at==0x2508dc&&n==16){memcpy(out,code,n);if(guard_bad)((unsigned char *)out)[0]^=1;return 0;}
    uint32_t v;
    if(at==0x1c8a404&&n==4)v=0x50000;
    else if(at==0x50164&&n==4)v=list_pointer;
    else if(at==0x5016c&&n==4)v=count;
    else if(at>=0x60000&&at-0x60000<=sizeof records&&n<=sizeof records-(at-0x60000)){
        memcpy(out,records+(at-0x60000),n);
        if(torn&&n>=0x8c){v=0x30+(reads++&1);memcpy((unsigned char *)out+0x88,&v,4);}
        return 0;
    }else return -1;
    memcpy(out,&v,4);return 0;
}
#define CHECK(x) do{checks++;assert(x);}while(0)
static void reset(void){memset(records,0,sizeof records);count=1;list_pointer=0x60000;guard_bad=read_fail=torn=0;reads=0;put(0,0x62);}
static void unavailable(void){unsigned denominator=123;CHECK(!xz_shell_model_quantize_grid(read_fixture,&denominator));CHECK(denominator==0);}
int main(void){
    reset();
    for(unsigned value=0x30;value<=0x33;value++){
        unsigned denominator=0;put(0x88,value);
        CHECK(xz_shell_model_quantize_grid(read_fixture,&denominator));CHECK(denominator==(1u<<(0x33-value)));
    }
    put(0x88,0x30);unsigned denominator=0;CHECK(xz_shell_model_quantize_grid(read_fixture,&denominator)&&denominator==8);
    put(0x88,0x33);CHECK(xz_shell_model_quantize_grid(read_fixture,&denominator)&&denominator==1);
    put(0x88,0x2f);unavailable();put(0x88,0x34);unavailable();put(0x88,0x33);
    guard_bad=1;unavailable();guard_bad=0;read_fail=1;unavailable();read_fail=0;
    torn=1;unavailable();torn=0;count=0;unavailable();count=513;unavailable();count=1;
    put(0,0x63);unavailable();put(0,0x62);count=2;put(0x8c,0x62);put(0x8c+0x88,0x33);unavailable();
    count=1;list_pointer=0x60001;unavailable();list_pointer=0x60000;
    denominator=99;CHECK(!xz_shell_model_quantize_grid(NULL,&denominator)&&denominator==0);
    CHECK(!xz_shell_model_quantize_grid(read_fixture,NULL));
    printf("RESULT quantize-grid checks=%u failures=0\n",checks);return 0;
}
