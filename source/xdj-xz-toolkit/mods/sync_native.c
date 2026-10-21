/* SPDX-License-Identifier: MIT */
#include "sync_native.h"
#include "arcade/native_clock.h"
#include <math.h>
#include <string.h>
struct reads {xz_sync_native_read_fn read;void *context;unsigned count;};
static int counted(void *ctx,uint32_t address,void *out,size_t n){
 struct reads *r=ctx;r->count++;return r->read(r->context,address,out,n);
}
static uint32_t word(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static int pointer(uint32_t base,unsigned offset,size_t n){
 return base&&!(base&3)&&base>=0x10000&&(uint64_t)base+offset+n<=UINT32_MAX;
}
struct master {uint32_t engine,sync;int32_t id;};
static int master_read(struct reads *r,struct master *m){
 unsigned char state[20];
 if(counted(r,0x010e9688,&m->engine,4)||!pointer(m->engine,0x24,4)||
    counted(r,m->engine+0x24,&m->sync,4)||!pointer(m->sync,8,sizeof state)||
    counted(r,m->sync+8,state,sizeof state))return 0;
 uint32_t id=word(state+(state[0x15-8]?0x18-8:0));memcpy(&m->id,&id,4);return 1;
}
int xz_sync_native_read(xz_sync_native_read_fn read,void *context,xz_sync_native_now_us now,
 void *clock_context,struct xz_sync_native_snapshot *out){
 if(!out)return 0;
 memset(out,0,sizeof *out);out->native_deck=-1;out->master_id=-2;out->reason=XZ_SYNC_NATIVE_NO_READER;
 if(!read||!now)return 0;
 struct reads r={read,context,0};uint64_t began=now(clock_context);
 static const unsigned char guard[32]={0x24,0x30,0x90,0xe5,0x00,0x00,0x53,0xe3,0x04,0x00,0x00,0x0a,0x15,0x20,0xd3,0xe5,0x00,0x00,0x52,0xe3,0x18,0x00,0x93,0x15,0x08,0x00,0x93,0x05,0x1e,0xff,0x2f,0xe1};
 unsigned char bytes[32];struct master before,after;struct xz_arcade_track_clock a={0},b={0};
 out->reason=XZ_SYNC_NATIVE_BAD_ABI;
 if(counted(&r,0x4bc28,bytes,sizeof bytes)||memcmp(bytes,guard,sizeof guard)||!xz_arcade_native_clock_valid(counted,&r))goto done;
 out->reason=XZ_SYNC_NATIVE_MASTER_UNAVAILABLE;
 if(!master_read(&r,&before))goto done;
 out->master_id=before.id;
 if(before.id>=2&&before.id<=5){out->reason=XZ_SYNC_NATIVE_EXTERNAL_MASTER;goto done;}
 if(before.id<0||before.id>1)goto done;
 out->reason=XZ_SYNC_NATIVE_CLOCK_UNAVAILABLE;
 if(!xz_music_native_clock_read(counted,&r,(unsigned)before.id,&a)||
    !xz_music_native_clock_read(counted,&r,(unsigned)before.id,&b))goto done;
 out->reason=XZ_SYNC_NATIVE_MASTER_CHANGED;
 if(!master_read(&r,&after)||before.engine!=after.engine||before.sync!=after.sync||before.id!=after.id)goto done;
 out->reason=XZ_SYNC_NATIVE_SOURCE_CHANGED;
 if(!a.track||a.track!=b.track)goto done;
 out->reason=XZ_SYNC_NATIVE_STATE_CHANGED;
 double dphase=fmod((double)b.bar_phase-(double)a.bar_phase+4.5,1.)-.5;
 if(a.playing!=b.playing||a.manual!=b.manual||a.bpm!=b.bpm||a.speed!=b.speed||
    (a.playing&&(dphase<-.02||dphase>.25))||
    (!a.playing&&(a.position_ms!=b.position_ms||a.bar_phase!=b.bar_phase)))goto done;
 out->native_deck=before.id;out->track_identity=b.track;out->position_ms=b.position_ms;
 out->position_frames=b.position_frames;out->bpm=b.bpm;out->nominal_bpm=b.nominal_bpm;
 out->bar_phase=b.bar_phase;out->speed=b.speed;out->playing=!!b.playing;out->manual=!!b.manual;
 out->reason=XZ_SYNC_NATIVE_OK;out->available=1;
done:
 out->read_count=r.count;out->earliest_us=began;out->latest_us=now(clock_context);
 if(out->latest_us<began||out->latest_us-began>XZ_SYNC_NATIVE_MAX_READ_US){
  out->available=0;out->phase_available=0;out->reason=XZ_SYNC_NATIVE_TIME_INVALID;out->native_deck=-1;return 0;
 }
 out->uncertainty_us=(uint32_t)((out->latest_us-began+1)/2);
 out->captured_us=began+(out->latest_us-began)/2;out->captured_ms=(uint32_t)(out->captured_us/1000);
 out->phase_available=out->available&&b.phase_valid&&out->uncertainty_us<=XZ_SYNC_NATIVE_MAX_PHASE_UNCERTAINTY_US;
 return out->available;
}
