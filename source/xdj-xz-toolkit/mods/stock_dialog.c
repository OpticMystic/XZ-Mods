/* SPDX-License-Identifier: MIT */
#include "stock_dialog.h"
#include "runtime.h"
#include <string.h>
#include "ui/native_skin_runtime.h"
#pragma weak xz_native_skin_request_repaint

static int stable(xz_stock_dialog_read read,uint32_t at,void *out,size_t n){
 unsigned char again[64];
 return n<=sizeof again&&!read(at,out,n)&&!read(at,again,n)&&!memcmp(out,again,n);
}
static int guard(xz_stock_dialog_read read,uint32_t at,const unsigned char expected[16]){
 unsigned char actual[16];return !read(at,actual,sizeof actual)&&!memcmp(actual,expected,sizeof actual);
}
static int counter_warning(uint32_t id){
 /* Exact DOWN caution table, excluding normal state messages (END, MEMORY,
    DELETED, SYNC, NOW LOADING, Not Loaded., INPUT and the empty string). */
 switch(id){
 case 0:case 99:case 106:case 108:case 109:case 110:case 111:
 case 123:case 124:case 125:case 131:case 132:case 133:
 case 136:case 137:case 138:case 139:return 0;
 default:return 1; /* unknown native ids also remain visible */
 }
}
int xz_stock_dialog_observe(xz_stock_dialog_read read){
 static const unsigned char browse[16]={0xf0,0x4f,0x2d,0xe9,0x04,0x8b,0x2d,0xed,0xfc,0xd0,0x4d,0xe2,0x59,0x6b,0xfe,0xeb};
 static const unsigned char status[16]={0xf0,0x4f,0x2d,0xe9,0xa4,0xd0,0x4d,0xe2,0x55,0x67,0xfe,0xeb,0x00,0x00,0x50,0xe3};
 static const unsigned char counter[16]={0xf0,0x45,0x2d,0xe9,0x0c,0xd0,0x4d,0xe2,0x00,0x40,0xa0,0xe1,0x69,0x69,0x01,0xeb};
 uint32_t windows[2],ids[2];
 if(!read||!guard(read,0x146fcc,browse)||!guard(read,0x147f08,status)||!guard(read,0xe1d80,counter))return -1;
 /* Live handles, not the latched browse-message id. DrawCautionBrowse/Status
    destroy and zero these when the dialog clears. Leave stock's whole screen
    intact: its text rows, icons and dismissal controls can vary by language. */
 if(!stable(read,0x1adae30,windows,4)||!stable(read,0x1adaea4,windows+1,4))return -1;
 if(windows[0]||windows[1])return 1;
 /* Ui_CautionIDCheck_Counter reads current ids from g_CmnSystemInfo. Do not
    use nShowCounterErrorCautionId: it is a retained draw cache. */
 if(!stable(read,0x222c668,ids,sizeof ids))return -1;
 return counter_warning(ids[0])||counter_warning(ids[1]);
}
int xz_stock_dialog_active(void){
 static int previous;
 int active=xz_stock_dialog_observe(xz_read_memory)!=0;
 /* The native owner thread rebuilds its screen on entry and on return. Never
    attempt to restore a saved mod framebuffer over a live native window. */
 void (*repaint)(void)=xz_native_skin_request_repaint;
 if(__atomic_exchange_n(&previous,active,__ATOMIC_ACQ_REL)!=active&&repaint)repaint();
 return active;
}
