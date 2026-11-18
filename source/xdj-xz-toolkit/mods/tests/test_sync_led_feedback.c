#define main retained_mt_main
#include "test_led_feedback.c"
#undef main
#include "../ui/extra_lane.h"
static void sync_apply(unsigned channel,unsigned present,unsigned forced,unsigned dimmed,unsigned wanted){
 unsigned char row[XZ_LED_RECORD_BYTES]={0},expected[XZ_LED_RECORD_BYTES];uint16_t count=present?1:0;uint32_t id=XZ_LED_ID_BEAT_SYNC;
 memcpy(row,&id,4);memcpy(row+4,&channel,4);put(row+0x14,dimmed);row[0x18]=(unsigned char)forced;memcpy(expected,row,sizeof row);
 struct xz_extra_deck d={.loaded=1,.sync={.requested=(int)wanted}};in_hook=1;
 xz_native_led_extra_deck(row,sizeof row,&count,NULL,0,0,0,100,channel,&d);in_hook=0;
 if(present&&!forced)put(expected+0x10,wanted);assert(!memcmp(row,expected,sizeof row));
}
static void all_sync_lamps(void){
 for(unsigned side=1;side<=2;side++)for(int r=0;r<=XZ_EXTRA_SYNC_TEMPO_FOLLOW;r++)
 for(int q=0;q<2;q++)for(int locked=0;locked<2;locked++)for(int loaded=0;loaded<2;loaded++)
 for(int requested=0;requested<2;requested++)for(int forced=0;forced<2;forced++)for(unsigned now=0;now<1000;now+=125){
  struct xz_extra_deck d={.loaded=loaded,.quantize=q,.master=1,
   .sync={.requested=requested,.locked=locked,.reason=r,.master_deck=0}};
  assert(xz_extra_sync_present(&d).led_on==!!(loaded&&requested));
  unsigned char row[XZ_LED_RECORD_BYTES]={0},expected[XZ_LED_RECORD_BYTES];uint16_t count=1;
  put(row,XZ_LED_ID_BEAT_SYNC);put(row+4,side);put(row+0x10,1);put(row+0x14,1);row[0x18]=(unsigned char)forced;
  memcpy(expected,row,sizeof row);if(!forced)put(expected+0x10,(unsigned)(loaded&&requested));
  assert(xz_extra_lamp(&d,XZ_LED_ID_BEAT_SYNC)==(loaded&&requested?XZ_LED_STATE_ON:XZ_LED_STATE_OFF));
  xz_native_led_extra_deck(row,sizeof row,&count,NULL,0,0,0,now,side,&d);
  assert(!memcmp(row,expected,sizeof row)&&count==1);
  memcpy(expected,row,sizeof row);
  xz_native_led_extra_deck(row,sizeof row,&count,NULL,0,0,0,now,3-side,&d);
  assert(!memcmp(row,expected,sizeof row));
 }
}
int main(void){
 assert(retained_mt_main()==0);unsigned before=logs;
 sync_apply(1,1,0,1,1);assert(logs==before);xz_native_led_service_feedback();assert(logs>before&&strstr(last,"SYNC_LED_STATE: side=1 wanted=1 records=1 before=0 after=1 force=0 raw14=1"));
 before=logs;for(unsigned i=0;i<100;i++){sync_apply(1,1,0,1,1);xz_native_led_service_feedback();}assert(logs==before);
 sync_apply(1,0,0,0,1);xz_native_led_service_feedback();assert(strstr(last,"wanted=1 records=0"));
 sync_apply(2,1,1,1,1);xz_native_led_service_feedback();assert(strstr(last,"side=2 wanted=1 records=1 before=0 after=0 force=1 raw14=1"));
 sync_apply(2,1,0,0,0);xz_native_led_service_feedback();assert(strstr(last,"side=2 wanted=0 records=1"));
 all_sync_lamps();
 puts("PASS all reason/quantize/lock/load/request/forced/time SYNC LED cases; SYNC material feedback, absent/forced/dimmed records, exact write parity, both sides, background logging");return 0;
}
