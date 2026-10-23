#define main retained_mt_main
#include "test_led_feedback.c"
#undef main
static void sync_apply(unsigned channel,unsigned present,unsigned forced,unsigned dimmed,unsigned wanted){
 unsigned char row[XZ_LED_RECORD_BYTES]={0},expected[XZ_LED_RECORD_BYTES];uint16_t count=present?1:0;uint32_t id=XZ_LED_ID_BEAT_SYNC;
 memcpy(row,&id,4);memcpy(row+4,&channel,4);put(row+0x14,dimmed);row[0x18]=(unsigned char)forced;memcpy(expected,row,sizeof row);
 struct xz_extra_deck d={.loaded=1,.sync={.requested=(int)wanted}};in_hook=1;
 xz_native_led_extra_deck(row,sizeof row,&count,NULL,0,0,0,100,channel,&d);in_hook=0;
 if(present&&!forced)put(expected+0x10,wanted);assert(!memcmp(row,expected,sizeof row));
}
int main(void){
 assert(retained_mt_main()==0);unsigned before=logs;
 sync_apply(1,1,0,1,1);assert(logs==before);xz_native_led_service_feedback();assert(logs>before&&strstr(last,"SYNC_LED_STATE: side=1 wanted=1 records=1 before=0 after=1 force=0 raw14=1"));
 before=logs;for(unsigned i=0;i<100;i++){sync_apply(1,1,0,1,1);xz_native_led_service_feedback();}assert(logs==before);
 sync_apply(1,0,0,0,1);xz_native_led_service_feedback();assert(strstr(last,"wanted=1 records=0"));
 sync_apply(2,1,1,1,1);xz_native_led_service_feedback();assert(strstr(last,"side=2 wanted=1 records=1 before=0 after=0 force=1 raw14=1"));
 sync_apply(2,1,0,0,0);xz_native_led_service_feedback();assert(strstr(last,"side=2 wanted=0 records=1"));
 puts("PASS SYNC material feedback, absent/forced/dimmed records, exact write parity, both sides, background logging");return 0;
}
