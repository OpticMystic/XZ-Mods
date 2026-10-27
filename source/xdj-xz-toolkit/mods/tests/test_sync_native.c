/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Sync native fixtures require assertions
#endif
#define main original_native_clock_fixture
#include "../arcade/test_native_clock.c"
#undef main
#include "../sync_native.h"
static unsigned char master_state[32];
static const unsigned char master_guard[32]={0x24,0x30,0x90,0xe5,0x00,0x00,0x53,0xe3,0x04,0x00,0x00,0x0a,0x15,0x20,0xd3,0xe5,0x00,0x00,0x52,0xe3,0x18,0x00,0x93,0x15,0x08,0x00,0x93,0x05,0x1e,0xff,0x2f,0xe1};
static unsigned read_calls,master_reads,master_pointer_reads,info_reads,state_reads,time_calls;
static int bad_guard,change_master,change_source,change_state,change_pointer,bad_time,change_paused_position,change_paused_phase,change_playing_phase;
static unsigned unit_reads;
static int fixture_read(void *ctx,uint32_t address,void *out,size_t n){
 (void)ctx;read_calls++;
 if(address==0x4bc28){assert(n==32);memcpy(out,master_guard,n);if(bad_guard)((unsigned char *)out)[0]^=1;return 0;}
 if(address==engine_ptr+0x24){assert(n==4);uint32_t p=change_pointer&&++master_pointer_reads>1?0x37100:0x37000;memcpy(out,&p,4);return 0;}
 if(address==0x37008||address==0x37108){assert(n==20);memcpy(out,master_state+8,n);if(change_master&&++master_reads>1)put(out,0,1);return 0;}
 if(address==0x316a0){info_reads++;if(change_source&&info_reads>=3){uint32_t p=0x32100;memcpy(out,&p,4);return 0;}}
 if(address>=0x32100&&address+n<=0x32100+sizeof grid){memcpy(out,grid+(address-0x32100),n);return 0;}
 int rc=read_memory(NULL,address,out,n);
 if(!rc&&address==0x20e64&&change_paused_position&&++state_reads>1)put(player,0xedc,57330);
 if(!rc&&address==0x35000&&n==8&&(change_paused_phase||change_playing_phase)&&++unit_reads>1)put(out,4,change_playing_phase?1250:1001);
 if(!rc&&address==0x20e64&&change_state&&++state_reads>1)((unsigned char *)out)[0x13]=0;
 return rc;
}
static uint64_t fixture_time(void *ctx){(void)ctx;return !time_calls++?1000000:bad_time==1?999999:bad_time==2?1020001:bad_time==3?1003000:1000200;}
static void reset(void){
 read_calls=master_reads=master_pointer_reads=info_reads=state_reads=time_calls=0;
 bad_guard=change_master=change_source=change_state=change_pointer=bad_time=change_paused_position=change_paused_phase=change_playing_phase=0;unit_reads=0;
 memset(master_state,0,sizeof master_state);engine_ptr=0x10000;
 put(engine,16,0x10108);put(players,4,0x20000);put(player,0xedc,55125);player[0xe77]=1;player[0xea4]=0;put(pause_queue,8,5);
 double zero_bend=0.;memcpy(tempo+0x38,&zero_bend,8);
}
int main(void){
 original_native_clock_fixture();reset();struct xz_sync_native_snapshot out;
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out));
 assert(out.available&&out.phase_available&&out.reason==XZ_SYNC_NATIVE_OK&&out.native_deck==0&&out.track_identity==0x32000);
 assert(out.earliest_us==1000000&&out.latest_us==1000200&&out.captured_us==1000100&&out.captured_ms==1000&&out.uncertainty_us==100);
 assert(out.position_frames==60637&&fabsf(out.nominal_bpm-120.f)<.001f&&!out.manual);
 assert(out.read_count==read_calls&&fabsf(out.bpm-132)<.001f&&fabsf(out.bar_phase-.75f)<.001f&&out.playing);
 printf("PASS native sync snapshot: %u provided reads, interval200us, source/grid token coherent; phase_available=%d\n",out.read_count,out.phase_available);
 reset();bad_time=3;assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out));
 assert(out.available&&!out.phase_available&&out.uncertainty_us==1500);
 reset();double bend=.02;memcpy(tempo+0x38,&bend,8);
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.phase_available&&out.manual&&fabsf(out.bpm-132)<.001f);
 for(int paused=0;paused<2;paused++)for(int mode=0;mode<3;mode++){
  reset();player[0xe77]=(unsigned char)!paused;put(pause_queue,8,paused?4:5);
  if(mode==0)put(grid,0x24,0);
  if(mode==1)put(player,paused?0xedc:0xe78,441000);
  if(mode==2)units[0]=9;
  assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out));
  assert(out.available&&!out.phase_available&&fabsf(out.bpm-132)<.001f&&out.track_identity==0x32000);
  put(grid,0x24,1);put(player,0xe78,60637);units[0]=1;
 }
 reset();put(manager,0x10,0x35001);
 assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&!out.bpm);
 put(manager,0x10,0x35000);
 reset();double pitch=.5;memcpy(tempo+0x30,&pitch,8);
 for(unsigned i=0;i<4;i++){units[i*8+2]=0x50;units[i*8+3]=0x46;put(units,i*8+4,1000+i*333);}
 put(player,0xe78,55125);
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&fabsf(out.bpm-270)<.001f);
 pitch=.1;memcpy(tempo+0x30,&pitch,8);put(player,0xe78,60637);
 for(unsigned i=0;i<4;i++){units[i*8+2]=0xe0;units[i*8+3]=0x2e;put(units,i*8+4,1000+i*500);}
 reset();player[0xe77]=0;put(pause_queue,8,4);
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&!out.playing&&out.position_ms==1250);
 reset();player[0xe77]=0;put(pause_queue,8,4);change_paused_position=1;
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.available&&!out.phase_available&&fabsf(out.bpm-132)<.001f);
 reset();player[0xe77]=0;put(pause_queue,8,4);change_paused_phase=1;
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.available&&!out.phase_available&&fabsf(out.bpm-132)<.001f);
 reset();change_playing_phase=1;
 assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.available&&!out.phase_available&&fabsf(out.bpm-132)<.001f);
 puts("PASS music270BPM independent of arcade cap; paused/playing phase jumps and wide read window phase gate verified");
 reset();put(master_state,8,1);assert(xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.native_deck==1);
 reset();put(master_state,8,2);assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_EXTERNAL_MASTER&&out.playing_native_mask==3);
 reset();put(master_state,8,UINT32_MAX);assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_MASTER_UNAVAILABLE);
 reset();bad_guard=1;assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_BAD_ABI);
 reset();change_master=1;assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_MASTER_CHANGED);
 reset();change_pointer=1;assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_MASTER_CHANGED);
 reset();change_source=1;assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_SOURCE_CHANGED);
 reset();change_state=1;assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_STATE_CHANGED);
 for(int mode=1;mode<=2;mode++){reset();bad_time=mode;assert(!xz_sync_native_read(fixture_read,NULL,fixture_time,NULL,&out)&&out.reason==XZ_SYNC_NATIVE_TIME_INVALID);}
 reset();assert(!xz_sync_native_read(NULL,NULL,fixture_time,NULL,&out)&&!read_calls);
 puts("PASS native sync unavailable guards: external/absent master, ABI drift, changed master/pointer/source/playing, invalid capture bounds");return 0;
}
