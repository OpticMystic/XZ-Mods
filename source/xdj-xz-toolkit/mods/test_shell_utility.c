/* SPDX-License-Identifier: MIT */
/* Host test: the generated stock UTILITY table and the pure mapping in shell_utility.c. */
#ifdef NDEBUG
#error "Shell utility acceptance requires active assertions"
#endif
#include "shell_utility_table.h"
#include "shell_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct xz_shut_def *by_key(const char *key) {
 for(int i=0;i<XZ_SHUT_DEFS;i++)if(!strcmp(xz_shut_defs[i].key,key))return &xz_shut_defs[i];
 return NULL;
}
static int ascii(const char *s) { for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)return 0;return 1; }

int main(void) {
 assert(XZ_SHUT_DEFS>=34&&XZ_SHUT_DEFS<=40);
 int writable=0;
 for(int i=0;i<XZ_SHUT_DEFS;i++){
  const struct xz_shut_def *d=&xz_shut_defs[i];
  assert(d->key&&d->label&&ascii(d->label)&&d->count>=1&&d->choices&&d->args);
  assert(!strstr(d->label,"rekordbox"));
  for(int k=0;k<d->count;k++){
   assert(d->choices[k]&&ascii(d->choices[k])&&!strstr(d->choices[k],"rekordbox"));
   /* Every choice has its own setter argument, and the reading maps back to it. */
   int32_t arg=-99;assert(xz_shut_arg(d,k,&arg));assert(xz_shut_index(d,arg)==k);
   for(int j=0;j<k;j++)assert(d->args[j]!=d->args[k]);
  }
  int32_t arg;assert(!xz_shut_arg(d,-1,&arg)&&!xz_shut_arg(d,d->count,&arg));
  assert(d->category>=0&&d->category<XZ_SHELL_SETTING_CAT_COUNT);
  /* One id per item; never a shell-owned display id. */
  for(int j=0;j<i;j++)assert(xz_shut_defs[j].id!=d->id);
  assert(!xz_shrt_shell_owned_setting(d->id));
  assert(xz_shut_find(xz_shut_defs,XZ_SHUT_DEFS,d->id)==i);
  if(d->get&&d->set){assert(d->get_code[0]||d->get_code[1]);assert(d->set_code[0]||d->set_code[1]);}
  writable+=d->mapped&&d->get&&d->set;
 }
 assert(writable>=33);
 assert(xz_shut_find(xz_shut_defs,XZ_SHUT_DEFS,XZ_SHELL_ITEM_WAVE_COLOR)<0);

 /* The forms the firmware uses. */
 const struct xz_shut_def *q=by_key("QuantizeValue"),*h=by_key("HotCueAutoLoad"),*l=by_key("LcdBrightness"),
  *lang=by_key("Language"),*lock=by_key("EjectLoadLock"),*touch=by_key("TouchPanelCalibration"),*fader=by_key("ChFaderCurve");
 assert(q&&h&&l&&lang&&lock&&touch&&fader);
 assert(q->id==XZ_SHELL_ITEM_QUANTIZE_BEAT&&q->count==4);
 /* The same encoding as the existing QUANTIZE path: choices 1/8 .. 1, UiSetUtilQuantizeValue 3 .. 0. */
 for(int k=0;k<4;k++)assert(q->args[k]==3-k);
 assert(!strncmp(q->choices[0],"1/8",3)&&!strncmp(q->choices[3],"1 ",2));
 assert(h->count==3&&h->args[0]==0&&h->args[1]==2&&h->args[2]==1&&!strcmp(h->choices[1],"LIBRARY SETTING"));
 assert(l->count==5&&l->args[0]==1&&l->args[4]==5&&l->apply);
 assert(lang->count==18&&lang->args[0]==1&&!strcmp(lang->choices[0],"ENGLISH"));
 assert(lock->id==XZ_SHELL_ITEM_LOAD_LOCK&&lock->args[1]==1&&!lock->apply);
 assert(fader->apply&&fader->apply_code[0]|fader->apply_code[1]);
 assert(touch->id==XZ_SHRT_UTIL_ID(16)&&!touch->get&&!touch->set&&touch->count==1);
 assert(by_key("NeedleLock")->id==XZ_SHELL_ITEM_NEEDLE_LOCK); /* the shell item; runtime id 0 stays reserved */

 /* Reasons: stock-only, guards, trial. */
 assert(xz_shut_reason(touch,1,1,0)&&!strcmp(xz_shut_reason(touch,1,1,0),XZ_SHUT_NOTE_STOCK));
 assert(!xz_shut_reason(lock,1,1,0));
 assert(!strcmp(xz_shut_reason(lock,0,1,0),XZ_SHUT_NOTE_FIRMWARE));
 assert(!strcmp(xz_shut_reason(lock,1,0,0),XZ_SHUT_NOTE_FIRMWARE));
 assert(!strcmp(xz_shut_reason(lock,1,1,1),XZ_SHUT_NOTE_TRIAL));
 assert(strlen(XZ_SHUT_NOTE_TRIAL)<=40&&strlen(XZ_SHUT_NOTE_FIRMWARE)<=40&&strlen(XZ_SHUT_NOTE_STOCK)<=40);

 /* Published rows. */
 int32_t raw[XZ_SHUT_DEFS];uint8_t known[XZ_SHUT_DEFS],ok[XZ_SHUT_DEFS];
 struct xz_shell_setting_item items[XZ_SHUT_DEFS];const char *notes[XZ_SHUT_DEFS];
 for(int i=0;i<XZ_SHUT_DEFS;i++){raw[i]=xz_shut_defs[i].args[xz_shut_defs[i].count-1];known[i]=1;ok[i]=1;}
 int qi=(int)(q-xz_shut_defs),li=(int)(lock-xz_shut_defs),ti=(int)(touch-xz_shut_defs),hi=(int)(h-xz_shut_defs);
 raw[qi]=0;   /* 1 beat */
 raw[hi]=77;  /* not a choice: unknown, never a wrong value */
 known[li]=0;
 xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,ok,ok,0,items,notes);
 assert(items[qi].known&&items[qi].current==3&&items[qi].writable&&!notes[qi]);
 assert(!items[hi].known);
 assert(!items[li].known&&items[li].writable);
 assert(items[ti].known&&items[ti].current==0&&!items[ti].writable&&!strcmp(notes[ti],XZ_SHUT_NOTE_STOCK));
 xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,ok,ok,1,items,notes);
 for(int i=0;i<XZ_SHUT_DEFS;i++){assert(!items[i].writable);assert(notes[i]);}
 assert(!strcmp(notes[qi],XZ_SHUT_NOTE_TRIAL));
 puts("PASS stock UTILITY table: ids, choices, setter arguments, reasons and rows");
 return 0;
}
