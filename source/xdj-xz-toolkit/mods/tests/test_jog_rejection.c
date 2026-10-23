#define main retained_asset_suite_main
#include "test_jog_assets_runtime.c"
#undef main
static char last_log[192];
static unsigned log_count;
void xz_log(const char *text){assert(!in_hook);snprintf(last_log,sizeof last_log,"%s",text);log_count++;}
int main(void){
 reset(1025);complete();tick(0);tick(1);
 media[0]++;tick(0);assert(!jd_side[0].cached);
 xz_jog_display_runtime_service_assets();assert(!log_count);
 clock_ms+=251;tick(0);xz_jog_display_runtime_service_assets();
 assert(log_count==1&&strstr(last_log,"side=1 reason=MEDIA_GENERATION"));
 for(unsigned i=0;i<50;i++){clock_ms++;tick(0);xz_jog_display_runtime_service_assets();}
 assert(log_count==1);
 values.deck[0].timeline.media_generation=media[0];
 values.deck[0].timeline.at_ms=(uint32_t)clock_ms;publish();tick(0);xz_jog_display_runtime_service_assets();
 assert(jd_side[0].cached&&log_count==2&&strstr(last_log,"RECOVERED"));
 content[1]++;tick(1);xz_jog_display_runtime_service_assets();clock_ms+=251;tick(1);xz_jog_display_runtime_service_assets();
 assert(log_count==3&&strstr(last_log,"side=2 reason=CONTENT_EPOCH"));
 selection.deck[1]=2;selection.epoch[1]++;tick(1);xz_jog_display_runtime_service_assets();
 assert(log_count==4&&strstr(last_log,"RECOVERED"));
 free(columns[0]);free(columns[1]);puts("PASS persistent Jog reason, independent sides, no callback I/O, throttling and recovery");return 0;
}
