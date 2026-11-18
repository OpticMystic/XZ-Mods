#ifdef NDEBUG
#error Native popup tests require assertions
#endif
#include "classic_popup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void stock(struct xz_cp_row *r,unsigned count){memset(r,0,20*sizeof(*r));for(unsigned i=0;i<count;i++){r[i].kind=i+1;r[i].text_tag=0x63;r[i].text[0]=(uint16_t)('A'+i);}r[count-1].kind=4;}
int main(void){
 struct xz_cp_row rows[20],before[20];
 for(uint32_t n=3;n<=4;n++){
  stock(rows,n);memcpy(before,rows,sizeof rows);uint32_t count=n;assert(!xz_cp_extend(rows,&count)&&count==n+2);
  assert(!memcmp(rows,before,2*sizeof(*rows)));assert(!memcmp(rows+4,before+2,(n-2)*sizeof(*rows)));
  assert(rows[2].kind==5&&rows[3].kind==6&&rows[2].text[13]=='3'&&rows[3].text[13]=='4');
  assert(rows[2].text[0]==0xfffa&&rows[2].text[14]==0xfffb&&!rows[2].text[15]);
  for(unsigned i=0;i<4;i++)assert(xz_cp_target(rows,count,i)==(int)i);
  assert(xz_cp_target(rows,count,count-1)==-1&&xz_cp_target(rows,count,count)==-2);
 }
 for(unsigned bad=0;bad<4;bad++){uint32_t n=4;stock(rows,n);if(bad==0)n=20;if(bad==1)rows[0].kind=5;if(bad==2)rows[2].kind=77;if(bad==3)rows[1].text_tag=0;
  memcpy(before,rows,sizeof rows);uint32_t old=n;assert(xz_cp_extend(rows,&n)==-1&&n==old&&!memcmp(rows,before,sizeof rows));}
 struct xz_cp_state s={0};const uint64_t media[2]={11,22};struct xz_cp_load load;
 uint32_t a=xz_cp_begin(&s,0,7,media,100),b=xz_cp_begin(&s,1,7,media,100);assert(a&&b&&a!=b);
 assert(xz_cp_reply(&s,0,7,0x10200,9,110,&load)==0);
 assert(xz_cp_reply(&s,a,7,0x10200,9,110,&load)==2&&load.deck==0&&load.source==1&&load.media_generation==11);
 assert(xz_cp_reply(&s,a,7,0x10200,9,111,&load)==3); /* duplicate remains owned */
 assert(xz_cp_reply(&s,b,8,0x10300,10,111,&load)==1); /* route replaced */
 for(unsigned which=0;which<5;which++){a=xz_cp_begin(&s,0,7,media,200);assert(a);uint32_t hi=which==0?0x10400:which==1?0x10201:0x10200;uint32_t lo=which==2?0:which==3?UINT32_MAX:3;
  assert(xz_cp_reply(&s,a,7,hi,lo,which==4?1200:201,&load)==1);}
 a=xz_cp_begin(&s,0,7,media,0xffffff00);assert(xz_cp_reply(&s,a,7,0x10300,10,0x10,&load)==2&&load.media_generation==22);
 a=xz_cp_begin(&s,0,7,media,300);b=xz_cp_begin(&s,0,7,media,301);assert(xz_cp_reply(&s,a,7,0x10200,8,302,&load)==3);assert(xz_cp_reply(&s,b,7,0x10200,9,302,&load)==2);
 s.serial=65535;assert(!xz_cp_begin(&s,0,7,media,1));assert(xz_cp_reply(&s,XZ_CP_TOKEN_PREFIX|65534,7,0x10200,1,1,&load)==3);
 puts("PASS native popup shape/UTF16/capacity/native rows, unique reply ownership, duplicate/late/route/non-USB refusal and counter exhaustion");
}
