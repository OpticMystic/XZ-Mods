/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_NOTATION_H
#define XZ_KEY_NOTATION_H
#include <stdio.h>
/* Canonical spelling shared by the native key formatter and every MODERN lane/browser. */
static inline void xz_key_notation_format(int root,int minor,int wheel,char out[8]){
 static const char *const names[]={"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
 if((unsigned)root>=12||(unsigned)minor>1){snprintf(out,8,"--");return;}
 if(wheel)snprintf(out,8,"%d%c",(root*7+(minor?4:7))%12+1,minor?'A':'B');
 else snprintf(out,8,"%s%s",names[root],minor?"m":"");
}
#endif
