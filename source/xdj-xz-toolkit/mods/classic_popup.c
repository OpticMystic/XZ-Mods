#include "classic_popup.h"
#include <string.h>
static void row(struct xz_cp_row *out, unsigned kind, unsigned deck) {
 static const uint16_t prefix[]={0xfffa,'L','O','A','D',' ','T','O',' ','D','E','C','K'};
 memset(out,0,sizeof(*out));out->kind=kind;out->text_tag=0x63;
 memcpy(out->text,prefix,sizeof(prefix));out->text[13]=(uint16_t)('0'+deck);out->text[14]=0xfffb;
}
int xz_cp_extend(struct xz_cp_row *rows, uint32_t *count) {
 if(!rows||!count||(*count!=3&&*count!=4)||rows[0].kind!=1||rows[1].kind!=2||rows[*count-1].kind!=4||
    (*count==4&&rows[2].kind!=3))return -1;
 for(unsigned i=0;i<*count;i++)if(rows[i].text_tag!=0x63)return -1;
 memmove(rows+4,rows+2,(*count-2)*sizeof(*rows));row(rows+2,5,3);row(rows+3,6,4);*count+=2;return 0;
}
int xz_cp_target(const struct xz_cp_row *rows,uint32_t count,uint32_t index) {
 if(!rows||count>XZ_CP_CAPACITY||index>=count)return -2;
 if(rows[index].kind==5||rows[index].kind==6) {
  if((count!=5&&count!=6)||rows[0].kind!=1||rows[1].kind!=2||rows[2].kind!=5||rows[3].kind!=6||rows[count-1].kind!=4||(count==6&&rows[4].kind!=3))return -2;
 }
 switch(rows[index].kind){case 1:return 0;case 2:return 1;case 5:return 2;case 6:return 3;case 3:case 4:return -1;default:return -2;}
}
uint32_t xz_cp_begin(struct xz_cp_state *s,unsigned extra,uint32_t epoch,const uint64_t media[2],uint32_t now) {
 if(!s||extra>1||!epoch||!media||(!media[0]&&!media[1])||s->serial==65535)return 0;
 uint32_t token=XZ_CP_TOKEN_PREFIX|++s->serial;
 s->pending[extra]=(struct xz_cp_pending){token,now+XZ_CP_TIMEOUT_MS,epoch,{media[0],media[1]}};
 return token;
}
int xz_cp_reply(struct xz_cp_state *s,uint32_t token,uint32_t epoch,uint32_t hi,uint32_t lo,uint32_t now,struct xz_cp_load *out) {
 if((token&0xffff0000u)!=XZ_CP_TOKEN_PREFIX)return 0;
 if(!s||!out)return 3;
 for(unsigned d=0;d<2;d++)if(s->pending[d].token==token){
  struct xz_cp_pending p=s->pending[d];s->pending[d].token=0;
  unsigned device=(hi>>8)&255,source=device==2?1:device==3?2:0;
  if(!epoch||epoch!=p.route_epoch||(int32_t)(now-p.until_ms)>=0||(hi&255)||!source||!lo||lo==UINT32_MAX||!p.media_generation[source-1])return 1;
  *out=(struct xz_cp_load){d,source,lo,p.route_epoch,p.media_generation[source-1]};return 2;
 }
 return 3;
}
