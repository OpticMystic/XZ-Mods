/* SPDX-License-Identifier: MIT */
#include "stock_dialog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Stock dialog acceptance requires active assertions
#endif
#include "stock_dialog_fixture.h"
int main(void){
 assert(!xz_stock_dialog_active());
 browse=0x1adb0d8;assert(xz_stock_dialog_active());browse=0;
 status=0x1adb118;assert(xz_stock_dialog_active());status=0;
 for(unsigned d=0;d<2;d++){
  for(uint32_t id=101;id<=105;id++){ids[d]=id;assert(xz_stock_dialog_active());}
  ids[d]=146;assert(xz_stock_dialog_active());ids[d]=0;
 }
 const uint32_t benign[]={0,99,106,108,109,110,111,123,124,125,131,132,133,136,137,138,139};
 for(unsigned i=0;i<sizeof benign/sizeof *benign;i++){ids[0]=benign[i];assert(!xz_stock_dialog_active());}
 ids[0]=8307;assert(xz_stock_dialog_active()); /* unknown is never hidden */
 ids[0]=0;failed=1;assert(xz_stock_dialog_active());failed=0;
 torn=1;assert(xz_stock_dialog_active());torn=0;
 assert(!xz_stock_dialog_active());
 assert(xz_stock_dialog_observe(NULL)==-1);
 puts("PASS guarded browse/status windows, both counter warnings, benign states, unknown and torn reads");
 return 0;
}
