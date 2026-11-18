/* SPDX-License-Identifier: MIT */
#ifndef XZ_STOCK_COUNTER_INCLUDED
#define XZ_STOCK_COUNTER_INCLUDED
#include "shell/shell.h"
#define XZ_STOCK_COUNTER_GUARD_BYTES {0xf0,0x45,0x2d,0xe9,0x0c,0xd0,0x4d,0xe2,0x00,0x40,0xa0,0xe1,0x69,0x69,0x01,0xeb}
/* Firmware-1.26 CautionID_DOWN_StringID_Tbl and English string table.
   Complete strings only: parameterized messages are not guessed. */
static inline int xz_stock_counter_alert(uint32_t id,struct xz_shell_alert *out){
 struct xz_shell_alert a={0};
 switch(id){
 case 101:a=(struct xz_shell_alert){XZ_SHELL_ALERT_DEVICE_ERROR,"E-7206","AUTH CHIP ERROR"};break;
 case 102:a=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302","CANNOT PLAY TRACK"};break;
 case 103:a=(struct xz_shell_alert){XZ_SHELL_ALERT_UNSUPPORTED_FILE,"E-8304","UNSUPPORTED"};break;
 case 104:a=(struct xz_shell_alert){XZ_SHELL_ALERT_UNSUPPORTED_FILE,"E-8305","UNSUPPORTED"};break;
 case 105:a=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8306","NO FILE"};break;
 default:if(out)*out=a;return 0;
 }
 if(out)*out=a;return 1;
}
/* CLASSIC stock deck info/counter area. Covers the actual 304x60 caution
   windows at (10,409)/(410,409) without covering the other deck or wave. */
#define XZ_STOCK_COUNTER_Y 311
#define XZ_STOCK_COUNTER_H 169
#define XZ_STOCK_COUNTER_W 400
static inline int xz_stock_counter_hit(unsigned mask,unsigned x,unsigned y){
 return x<800&&y>=XZ_STOCK_COUNTER_Y&&y<480&&(mask&(1u<<(x/400)));
}
#endif
