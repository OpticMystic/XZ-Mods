/* SPDX-License-Identifier: MIT */
#include "stems.h"
#include <string.h>

void xz_pc_stems_init(struct xz_pc_stems *s,struct xz_pc_stem_sink sink){
 memset(s,0,sizeof(*s));s->sink=sink;
}
void xz_pc_stems_session(struct xz_pc_stems *s,unsigned deck,int connected,uint64_t generation){
 if(deck>=XZ_PC_DECKS)return;
 struct xz_pc_stem_state *d=&s->deck[deck];
 if(d->generation!=generation||d->connected!=!!connected){
  memset(d,0,sizeof(*d));d->generation=generation;d->connected=!!connected;
 }
}
void xz_pc_stems_verify(struct xz_pc_stems *s,unsigned deck){
 if(deck>=XZ_PC_DECKS||!s->deck[deck].connected||!s->sink.note)return;
 s->deck[deck].verified=1;s->deck[deck].caps|=XZ_PC_TOGGLE;
}
int xz_pc_stems_toggle(struct xz_pc_stems *s,unsigned deck,unsigned role,uint64_t now){
 if(deck>=XZ_PC_DECKS||role>=XZ_PC_ROLES||!s->sink.note)return 0;
 struct xz_pc_stem_state *d=&s->deck[deck];unsigned bit=1u<<role;
 if(!d->connected||!d->verified||d->pending&bit)return 0;
 /* A gesture is sent once. A failed release does not justify another press:
  * another toggle would undo the first if the PC already received it. */
 d->pending|=bit;d->sent_at[role]=now;
 int press=s->sink.note(s->sink.context,12u+deck,0x70u+role,1);
 int release=s->sink.note(s->sink.context,12u+deck,0x70u+role,0);
 return press&&release;
}
int xz_pc_stems_feedback(struct xz_pc_stems *s,const uint8_t *m,size_t size){
 if(!m||size!=3||((m[0]&0xf0)!=0x90&&(m[0]&0xf0)!=0x80)||(m[0]&15)<12||m[1]<0x70||m[1]>0x73||m[2]>127)return 0;
 unsigned deck=(m[0]&15)-12,role=m[1]-0x70,bit=1u<<role;
 struct xz_pc_stem_state *d=&s->deck[deck];
 if(!d->connected)return 0;
 d->known|=bit;if((m[0]&0xf0)==0x90&&m[2])d->active|=bit;else d->active&=~bit;
 d->pending&=~bit;d->caps|=XZ_PC_FEEDBACK;
 return 1;
}
void xz_pc_stems_tick(struct xz_pc_stems *s,uint64_t now){
 for(unsigned deck=0;deck<XZ_PC_DECKS;deck++)for(unsigned role=0;role<4;role++){
  struct xz_pc_stem_state *d=&s->deck[deck];unsigned bit=1u<<role;
  if(d->pending&bit&&now>=d->sent_at[role]&&now-d->sent_at[role]>=600){
   d->pending&=~bit;d->known&=~bit;
  }
 }
}
