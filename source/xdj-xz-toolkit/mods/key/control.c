/* SPDX-License-Identifier: MIT */
#include "control.h"
#include <string.h>
int xz_key_owner_equal(int deck,struct xz_key_owner a,struct xz_key_owner b){
 if((unsigned)deck>=4)return 0;
 if(deck<2)return a.native_epoch&&a.native_epoch==b.native_epoch;
 return a.route_epoch&&a.load_revision&&a.route_epoch==b.route_epoch&&
  a.load_revision==b.load_revision&&a.media_generation==b.media_generation;
}
enum xz_key_refusal xz_key_plan_sync(int deck,const struct xz_deck_key *s,
 const struct xz_deck_key *t,const struct xz_master_observation *m,struct xz_key_command *out){
 if(!s||!out||(unsigned)deck>=4||!s->engine_ready||!xz_key_owner_equal(deck,s->owner,s->owner))return XZ_KEY_NO_SOURCE;
 if(s->audio_state==XZ_KEY_AUDIO_EMERGENCY)return XZ_KEY_EMERGENCY;
 struct xz_key_command c={.kind=XZ_KEY_SYNC_TOGGLE,.source_owner=s->owner,.target_deck=-1};
 if(s->engaged){*out=c;return XZ_KEY_OK;}
 if(!s->metadata_valid)return XZ_KEY_NO_SOURCE;
 if(!m||!m->revision||!m->operator_epoch||(unsigned)m->deck>=4)return XZ_KEY_NO_MASTER;
 if(m->deck==deck)return XZ_KEY_IS_MASTER;
 if(!t||!t->metadata_valid||!t->playing_valid||
    !xz_key_owner_equal(m->deck,t->owner,t->owner)||
    (t->audio_state!=XZ_KEY_AUDIO_APPLIED&&t->audio_state!=XZ_KEY_AUDIO_PREPARED))return XZ_KEY_NO_TARGET;
 struct xz_key_snapshot source={.original=s->original,.loaded=1,.tempo_ratio=1,.master_tempo=1};int cents;
 if(!xz_key_sync_cents(&source,&t->playing,&cents))return XZ_KEY_RANGE;
 c.target=t->playing;c.target_owner=t->owner;c.target_deck=m->deck;c.master=*m;c.target_valid=1;
 *out=c;return XZ_KEY_OK;
}
const char *xz_key_refusal_text(enum xz_key_refusal r){
 switch(r){
 case XZ_KEY_OK:return "KEY REQUEST QUEUED";
 case XZ_KEY_NO_SOURCE:return "LOAD AN ANALYZED TRACK / SET UP KEYS";
 case XZ_KEY_NO_MASTER:return "SELECT ONE LOCAL MASTER / SET UP KEYS";
 case XZ_KEY_IS_MASTER:return "THIS DECK IS MASTER";
 case XZ_KEY_NO_TARGET:return "VERIFY MASTER PLAYING KEY / SET UP KEYS";
 case XZ_KEY_EMERGENCY:return "EMERGENCY LOOP / LOAD NEXT TRACK";
 default:return "KEY RANGE IS -12 TO +12 SEMITONES";
 }
}
