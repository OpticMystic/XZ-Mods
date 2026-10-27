/* SPDX-License-Identifier: MIT */
#include "stock_dialog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Stock dialog acceptance requires active assertions
#endif
#include "stock_dialog_fixture.h"
static unsigned logs;static char last_log[160];
void xz_log(const char *text){logs++;snprintf(last_log,sizeof last_log,"%s",text);}
int main(void){
 assert(!xz_stock_dialog_active());
 browse=0x1adb0d8;assert(xz_stock_dialog_active());browse=0;
 status=0x1adb118;assert(xz_stock_dialog_active());status=0;
 const char *codes[]={"E-7206","E-8302","E-8304","E-8305","E-8306"};
 const char *text[]={"AUTH CHIP ERROR","CANNOT PLAY TRACK","UNSUPPORTED","UNSUPPORTED","NO FILE"};
 for(unsigned d=0;d<2;d++)for(uint32_t id=101;id<=105;id++){
  ids[d]=id;assert(!xz_stock_dialog_active());assert(xz_stock_counter_ids(NULL)==(1u<<d));
  struct xz_shell_alert a;assert(xz_stock_counter_alert(id,&a));
  assert(!strcmp(a.code,codes[id-101])&&!strcmp(a.text,text[id-101]));ids[d]=0;
 }
 assert(!xz_stock_counter_ids(NULL));
 unsigned n=logs;ids[0]=8307;assert(!xz_stock_dialog_active()&&!xz_stock_counter_ids(NULL));
 assert(logs==n+1&&strstr(last_log,"id=8307"));
 ids[0]=0;xz_stock_dialog_active();ids[0]=8307;xz_stock_dialog_active();assert(logs==n+1);ids[0]=0;
 failed=1;n=logs;assert(!xz_stock_dialog_active());assert(logs==n+1&&strstr(last_log,"browse prologue read failed"));
 assert(!xz_stock_dialog_active()&&logs==n+1);failed=0;
 mismatch_at=0x147f08;assert(!xz_stock_dialog_active()&&logs==n+2&&strstr(last_log,"status prologue mismatch"));mismatch_at=0;
 torn_at=0x1adae30;assert(!xz_stock_dialog_active()&&strstr(last_log,"window read torn"));torn_at=0;
 failed_at=0x222c668;assert(!xz_stock_dialog_active()&&strstr(last_log,"counter read failed"));failed_at=0;
 /* Counter failure must not suppress a proven live global window. */
 browse=0x1adb0d8;mismatch_at=0xe1d80;assert(xz_stock_dialog_active());browse=0;mismatch_at=0;
 assert(!xz_stock_dialog_active()&&!xz_stock_counter_ids(NULL));
 assert(xz_stock_dialog_observe(NULL)==-1);
 assert(xz_stock_counter_hit(1,0,311)&&xz_stock_counter_hit(1,399,479));
 assert(!xz_stock_counter_hit(1,400,311)&&!xz_stock_counter_hit(1,0,310)&&!xz_stock_counter_hit(3,800,479));
 puts("PASS fail-open reason logs, live windows, exact known error mapping, unknown id once, counter bounds");
 return 0;
}
