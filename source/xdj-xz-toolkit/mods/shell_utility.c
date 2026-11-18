/* SPDX-License-Identifier: MIT */
#include "shell_utility.h"
#include <stddef.h>

int xz_shut_index(const struct xz_shut_def *d, int32_t raw) {
 if(!d||!d->args)return -1;
 for(int i=0;i<d->count;i++)if(d->args[i]==raw)return i;
 return -1;
}
int xz_shut_arg(const struct xz_shut_def *d, int index, int32_t *arg) {
 if(!d||!d->args||index<0||index>=d->count||!arg)return 0;
 *arg=d->args[index];return 1;
}
int xz_shut_trial(int settings_readonly, int utility_writes) {
 return settings_readonly&&!utility_writes;
}
const char *xz_shut_reason(const struct xz_shut_def *d, int getter_ok, int setter_ok, int trial) {
 if(!d)return XZ_SHUT_NOTE_FIRMWARE;
 if(!d->set||!d->get)return XZ_SHUT_NOTE_STOCK;
 if(!d->mapped||!getter_ok||!setter_ok)return XZ_SHUT_NOTE_FIRMWARE;
 if(trial)return XZ_SHUT_NOTE_TRIAL;
 return NULL;
}
void xz_shut_items(const struct xz_shut_def *defs, int n, const int32_t *raw, const uint8_t *known,
                   const uint8_t *getter_ok, const uint8_t *setter_ok, int trial,
                   struct xz_shell_setting_item *items, const char **notes) {
 for(int i=0;i<n;i++){
  const struct xz_shut_def *d=&defs[i];
  int index=known&&known[i]?xz_shut_index(d,raw[i]):-1;
  const char *why=xz_shut_reason(d,getter_ok&&getter_ok[i],setter_ok&&setter_ok[i],trial);
  items[i]=(struct xz_shell_setting_item){d->id,d->category,d->label,d->choices,d->count,index<0?0:index,index>=0,!why};
  if(!d->get){items[i].current=0;items[i].known=1;}  /* stock-only: its one choice names where it lives */
  if(notes)notes[i]=why;
 }
}
int xz_shut_find(const struct xz_shut_def *defs, int n, int id) {
 for(int i=0;i<n;i++)if(defs[i].id==id)return i;
 return -1;
}
