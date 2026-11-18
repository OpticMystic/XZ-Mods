#include "stems.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct capture {unsigned count,channel[16],key[16];int value[16],fail_release;};
static int note(void *ctx,unsigned channel,unsigned key,int value){
 struct capture *c=ctx;unsigned i=c->count++;assert(i<16);c->channel[i]=channel;c->key[i]=key;c->value[i]=value;
 return value||!c->fail_release;
}
int main(void){
 struct xz_pc_stems s;struct capture c={0};xz_pc_stems_init(&s,(struct xz_pc_stem_sink){&c,note});
 assert(!xz_pc_stems_toggle(&s,0,XZ_PC_DRUMS,1));
 xz_pc_stems_session(&s,0,1,10);assert(!xz_pc_stems_toggle(&s,0,XZ_PC_DRUMS,1));
 xz_pc_stems_verify(&s,0);assert(xz_pc_stems_toggle(&s,0,XZ_PC_BASS,100));
 assert(c.count==2&&c.channel[0]==12&&c.key[0]==0x72&&c.value[0]==1&&c.value[1]==0);
 assert(s.deck[0].known==0&&s.deck[0].pending==4);
 assert(!xz_pc_stems_toggle(&s,0,XZ_PC_BASS,101)&&c.count==2);
 uint8_t reply[3]={0x9c,0x72,0};assert(xz_pc_stems_feedback(&s,reply,3));
 assert(s.deck[0].known==4&&s.deck[0].active==0&&s.deck[0].pending==0);
 reply[2]=127;assert(xz_pc_stems_feedback(&s,reply,3)&&s.deck[0].active==4);
 reply[0]=0x8c;assert(xz_pc_stems_feedback(&s,reply,3)&&s.deck[0].active==0);reply[0]=0x9c;
 assert(!s.deck[1].known);
 xz_pc_stems_session(&s,1,1,20);xz_pc_stems_verify(&s,1);c.fail_release=1;
 assert(!xz_pc_stems_toggle(&s,1,XZ_PC_VOCAL,200));assert(c.count==4&&c.channel[2]==13&&c.key[2]==0x73);
 assert(s.deck[1].pending==8&&!s.deck[1].known);
 xz_pc_stems_tick(&s,799);assert(s.deck[1].pending==8);
 xz_pc_stems_tick(&s,800);assert(!s.deck[1].pending&&!s.deck[1].known&&c.count==4);
 xz_pc_stems_session(&s,0,1,11);assert(!s.deck[0].verified&&!s.deck[0].known&&!s.deck[0].active);
 xz_pc_stems_session(&s,1,0,20);reply[0]=0x9d;assert(!xz_pc_stems_feedback(&s,reply,3));
 xz_pc_stems_session(&s,1,1,21);assert(!s.deck[1].verified&&c.count==4);
 c.fail_release=0;xz_pc_stems_session(&s,3,1,30);xz_pc_stems_verify(&s,3);
 assert(xz_pc_stems_toggle(&s,3,XZ_PC_INST,900)&&c.channel[4]==15&&c.key[4]==0x71);
 reply[0]=0x9f;reply[1]=0x71;assert(xz_pc_stems_feedback(&s,reply,3)&&s.deck[3].known==2);
 reply[2]=127;assert(xz_pc_stems_feedback(&s,reply,3)&&s.deck[3].active==2);
 assert(xz_pc_stems_toggle(&s,3,XZ_PC_INST,901));
 assert(s.deck[3].pending==2&&s.deck[3].known==2&&s.deck[3].active==2);
 reply[2]=0;assert(xz_pc_stems_feedback(&s,reply,3)&&!s.deck[3].pending&&s.deck[3].known==2&&!s.deck[3].active);
 assert(!xz_pc_stems_toggle(&s,4,0,900)&&!xz_pc_stems_toggle(&s,0,4,900));
 reply[0]=0x90;assert(!xz_pc_stems_feedback(&s,reply,3));
 reply[0]=0x9c;reply[1]=0x74;assert(!xz_pc_stems_feedback(&s,reply,3));
 puts("PASS PC stems: four roles, one press/release, feedback, pending timeout, generation reset, disconnect, no replay");
 return 0;
}
