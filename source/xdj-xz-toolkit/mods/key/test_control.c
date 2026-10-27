/* SPDX-License-Identifier: MIT */
#include "control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 unsigned pairs=0;
 for(int source=0;source<4;source++)for(int target=0;target<4;target++)if(source!=target)
 for(int a=0;a<24;a++)for(int b=0;b<24;b++){
  struct xz_deck_key s={.owner={1,2,3,4},.original={a%12,a/12},.engine_ready=1,.metadata_valid=1};
  struct xz_deck_key t={.owner={2,2,5,6},.playing={.key={b%12,b/12},.cents=23},
   .metadata_valid=1,.playing_valid=1,.audio_state=XZ_KEY_AUDIO_APPLIED};
  struct xz_master_observation m={.deck=target,.revision=3,.operator_epoch=1};struct xz_key_command c;
  assert(xz_key_plan_sync(source,&s,&t,&m,&c)==XZ_KEY_OK&&c.target_deck==target&&c.target_valid);
  struct xz_key_snapshot snapshot={.original=s.original,.loaded=1,.master_tempo=1,.tempo_ratio=1};
  int cents;struct xz_playing_key playing;
  assert(xz_key_sync_cents(&snapshot,&c.target,&cents)&&cents>=-600&&cents<=600);
  assert(xz_key_playing(&snapshot,cents,&playing)&&xz_key_match(playing,c.target)==XZ_KEY_COMPATIBLE);
  m.deck=source;assert(xz_key_plan_sync(source,&s,&t,&m,&c)==XZ_KEY_IS_MASTER);
  for(int invalid=-3;invalid<0;invalid++){m.deck=invalid;assert(xz_key_plan_sync(source,&s,&t,&m,&c)==XZ_KEY_NO_MASTER);}
  s.engaged=1;assert(xz_key_plan_sync(source,&s,NULL,NULL,&c)==XZ_KEY_OK&&!c.target_valid);
  s.audio_state=XZ_KEY_AUDIO_EMERGENCY;assert(xz_key_plan_sync(source,&s,&t,&m,&c)==XZ_KEY_EMERGENCY);
  pairs++;
 }
 const double rates[]={.1,.25,.84,1,1.16,2,3,8};
 for(unsigned r=0;r<sizeof rates/sizeof *rates;r++)for(int mt=0;mt<2;mt++)for(int cents=-1200;cents<=1200;cents+=137){
  struct xz_key_snapshot s={.original={0,0},.loaded=1,.tempo_ratio=rates[r],.master_tempo=mt};
  struct xz_playing_key p;assert(xz_key_playing(&s,cents,&p));
  assert(p.shift_cents==(int)round(cents+(mt?0:1200*log2(rates[r]))));
  assert(!xz_key_playing(&s,1201,&p)&&!xz_key_playing(&s,-1201,&p));
  s.tempo_ratio=0;assert(!xz_key_playing(&s,0,&p));s.tempo_ratio=NAN;assert(!xz_key_playing(&s,0,&p));
 }
 struct xz_key_owner a={1,2,3,4},b=a;assert(xz_key_owner_equal(2,a,b));
 b.load_revision++;assert(!xz_key_owner_equal(2,a,b));b=a;b.route_epoch++;assert(!xz_key_owner_equal(2,a,b));
 b=a;b.media_generation++;assert(!xz_key_owner_equal(2,a,b));b=a;b.native_epoch++;assert(!xz_key_owner_equal(0,a,b));
 printf("PASS %u four-deck musical directions, refusal, owner and effective-rate cases\n",pairs);
}
