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
 browse=0x1adb0d8;mismatch_at=0xe1d80;assert(xz_stock_dialog_active());
 mismatch_at=0x147f08;assert(xz_stock_dialog_active());browse=0;
 status=0x1adb118;mismatch_at=0x146fcc;assert(xz_stock_dialog_active());status=0;mismatch_at=0;
 assert(!xz_stock_dialog_active()&&!xz_stock_counter_ids(NULL));
 assert(xz_stock_dialog_observe(NULL)==-1);
 /* The loopback cable's hub warning is never a live window: a text that starts with the firmware's hub text, whatever
    follows. Any other E-8307 text, an unreadable or torn text, or a second live window is an ordinary window, and
    its own text is published for MODS and MODERN to show (an unreadable one as XZ_STOCK_WINDOW_UNREAD). */
 char shown[XZ_STOCK_WINDOW_TEXT];
 assert(!xz_stock_usb_hub_text()&&!xz_stock_window_text(shown)&&!shown[0]);
 status=0x1adb118;caution_text(status_text,"E-8307: UNSUPPORTED DEVICE");n=logs;
 assert(!xz_stock_dialog_active()&&!strcmp(xz_stock_usb_hub_text(),"E-8307: UNSUPPORTED DEVICE"));
 assert(logs==n+1&&strstr(last_log,"hub warning E-8307: UNSUPPORTED DEVICE; not an error")&&!xz_stock_window_text(shown));
 caution_text(status_text,"E-8307: HUBS ARE NOT SUPPORTED");
 assert(!xz_stock_dialog_active()&&!strcmp(xz_stock_usb_hub_text(),"E-8307: HUBS ARE NOT SUPPORTED"));
 caution_text(status_text,"E-8307: UNSUPPORTED DEVICES");assert(!xz_stock_dialog_active()&&xz_stock_usb_hub_text());
 caution_text(status_text,"E-8307: HUBS ARE NOT SUPPORTED   ");assert(!xz_stock_dialog_active()&&xz_stock_usb_hub_text());
 /* Row breaks the player puts between its lines read as spaces. */
 caution_text(status_text,"E-8307:\nHUBS ARE\r\nNOT SUPPORTED");assert(!xz_stock_dialog_active()&&xz_stock_usb_hub_text());
 assert(xz_stock_usb_hub_at(xz_read_memory,XZ_STOCK_STATUS_TEXT)==1);
 /* The same window being redrawn: a cleared buffer, the start of the text or two reads that differ prove nothing,
    so the window stays the hub warning (2026-10-29: these looks made CLASSIC's top row flicker). */
 caution_text(status_text,NULL);assert(!xz_stock_dialog_active()&&!xz_stock_usb_hub_at(xz_read_memory,XZ_STOCK_STATUS_TEXT));
 caution_text(status_text,"E-8307: HU");assert(!xz_stock_dialog_active());
 caution_text(status_text,"E-8307: HUBS ARE NOT SUPPORTED");torn_at=0x1ada718;assert(!xz_stock_dialog_active());torn_at=0;
 /* A new window in its place starts as an ordinary window until its own text proves the hub warning. */
 status=0x1adb150;caution_text(status_text,NULL);assert(xz_stock_dialog_active());
 caution_text(status_text,"E-8307: HUBS ARE NOT SUPPORTED");assert(!xz_stock_dialog_active());status=0x1adb118;
 n=logs;caution_text(status_text,"E-8307: USB ACCESS ERROR");assert(xz_stock_dialog_active()&&!xz_stock_usb_hub_text());
 assert(xz_stock_window_text(shown)==24&&!strcmp(shown,"E-8307: USB ACCESS ERROR")&&strstr(last_log,"window E-8307: USB ACCESS ERROR"));
 /* Line breaks and runs of spaces read as one space; anything outside printable ASCII reads as '?'. */
 caution_text(status_text,"  E-8302:\n CANNOT  PLAY\tTRACK  ");status_text[23]=0x00e9;
 assert(xz_stock_dialog_active()&&xz_stock_window_text(shown)&&!strcmp(shown,"E-8302: CANNOT PLAY?TRACK"));
 caution_text(status_text,"E-8307: UNSUPPORTED");assert(xz_stock_dialog_active()&&!strcmp((xz_stock_window_text(shown),shown),"E-8307: UNSUPPORTED"));
 caution_text(status_text,"E-8307: UNSUPPORTED DEVICE");torn_at=0x1ada718;assert(xz_stock_dialog_active());torn_at=0;
 failed_at=0x1ada718;assert(xz_stock_dialog_active()&&xz_stock_window_text(shown)&&!strcmp(shown,XZ_STOCK_WINDOW_UNREAD));failed_at=0;
 browse=0x1adb0d8;assert(xz_stock_dialog_active()&&!xz_stock_usb_hub_text()&&!strcmp((xz_stock_window_text(shown),shown),XZ_STOCK_WINDOW_UNREAD));
 caution_text(browse_text,"E-8307: HUBS ARE NOT SUPPORTED");assert(!xz_stock_dialog_active()&&xz_stock_usb_hub_text()&&!xz_stock_window_text(shown));
 status=0;caution_text(status_text,NULL);assert(!xz_stock_dialog_active()&&xz_stock_usb_hub_text());
 browse=0;caution_text(browse_text,NULL);n=logs;assert(!xz_stock_dialog_active()&&!xz_stock_usb_hub_text());
 assert(logs==n+1&&strstr(last_log,"hub warning cleared"));
 /* Our own caution-draw hooks own both entries since preview.22.13.3 (ui/native_raw_skin.c): live windows and the
    decks' counter errors are still seen. Any other change to those entries is still a mismatch. */
 hooked_entries=1;
 browse=0x1adb0d8;caution_text(browse_text,"E-8304: USB ACCESS ERROR");assert(xz_stock_dialog_active());browse=0;caution_text(browse_text,NULL);
 status=0x1adb118;caution_text(status_text,"E-8304: USB ACCESS ERROR");assert(xz_stock_dialog_active());status=0;caution_text(status_text,NULL);
 ids[0]=103;assert(!xz_stock_dialog_active()&&xz_stock_counter_ids(NULL)==1u);ids[0]=0;
 mismatch_at=0x146fcc;assert(!xz_stock_dialog_active()&&strstr(last_log,"browse prologue mismatch"));mismatch_at=0;
 assert(!xz_stock_dialog_active()&&strstr(last_log,"observation ok"));
 hooked_entries=0;
 assert(xz_stock_counter_hit(1,0,311)&&xz_stock_counter_hit(1,399,479));
 assert(!xz_stock_counter_hit(1,400,311)&&!xz_stock_counter_hit(1,0,310)&&!xz_stock_counter_hit(3,800,479));
 puts("PASS fail-open reason logs, live windows and their text, exact known error mapping, unknown id once, counter bounds, E-8307 hub warning never a window");
 return 0;
}
