#define XZ_LED_PORTABLE_TEST
#ifndef XZ_JOG_DISPLAY_PROOF
#define XZ_JOG_DISPLAY_PROOF
#endif
#include "../ui/native_led.c"
#include <assert.h>
static unsigned logs,in_hook;static char last[192];
void xz_log(const char *s){assert(!in_hook);snprintf(last,sizeof last,"%s",s);logs++;}
static void apply(unsigned channel,unsigned present,unsigned forced,unsigned wanted){
 unsigned char row[XZ_LED_RECORD_BYTES]={0};uint16_t count=present?1:0;uint32_t id=XZ_LED_ID_MASTER_TEMPO;
 memcpy(row,&id,4);memcpy(row+4,&channel,4);row[0x18]=(unsigned char)forced;
 struct xz_extra_deck d={.loaded=1,.master_tempo=(int)wanted};in_hook=1;
 xz_native_led_extra_deck(row,sizeof row,&count,NULL,0,0,0,100,channel,&d);in_hook=0;
 if(present)assert(word(row+0x10)==(forced?0:wanted));
}
int main(void){
 apply(1,1,0,1);assert(!logs);xz_native_led_service_feedback();assert(logs==1&&strstr(last,"records=1 before=0 after=1 force=0"));
 for(unsigned i=0;i<100;i++){apply(1,1,0,1);xz_native_led_service_feedback();}assert(logs==1);
 apply(1,0,0,1);xz_native_led_service_feedback();assert(logs==2&&strstr(last,"records=0"));
 apply(2,1,1,1);xz_native_led_service_feedback();assert(logs==3&&strstr(last,"side=2")&&strstr(last,"after=0 force=1"));
 apply(2,1,0,0);xz_native_led_service_feedback();assert(logs==4&&strstr(last,"wanted=0"));
 puts("PASS MT record presence, native/owned states, force, both sides, transition retention and background-only logging");return 0;
}
