/* SPDX-License-Identifier: MIT */
#ifndef XZ_TEST_STOCK_LINK_BEAT_GATE_H
#define XZ_TEST_STOCK_LINK_BEAT_GATE_H
#include <stdint.h>
/* The stock player's gate for lining a synced deck up to the beat of a Link master (virtual player 3/4,
   engine master ID 2/3), modelled from the stock binary. SyncMaster::updateMaster (rbp 0x59b4c) copies the
   master's status from LinkInfoMgr::getLinkPlayerInfo into SyncMaster+0x34; the bridge serves that 56-byte
   status inside SyncMgr::sync. Three stock paths read it with the same test, and give up when it fails:
   BeatSync::checkStartCondition (0x4cb0c, branch 0x4cbc8: the snap at SYNC on), syncBeatLinkMaster
   (0x4dc8c, branch 0x4e14c: the jump itself) and checkPrecision (0x4e328: the later beat check).
   Field names from LinkPlayerInfo::debug_print (0x52608):
     +0  devid, the player number (engine master ID + 1)
     +4  sync_stat_: 0x80 SYNC_SUPPORTED, 0x20 MASTER_ON, 0x04 NOT_OUT_OF_RANGE (all three needed)
     +5  enable_:    0x08 BEAT_SYNC_AVAIL (needed), 0x02 SLIP_ON (selects the background lane),
                     0x04 USE_PLAY_SPEED (selects the play speed), 0x01 BPM_SYNC_AVAIL
     +0x18/+0x28 foreground/background lane: analyzed byte, BPM (0x7fffffff unknown), slider speed, play speed
   Without BEAT_SYNC_AVAIL the stock deck takes the master's tempo (BpmSync) but never moves to its beat. */
static inline uint32_t stock_gate_le32(const unsigned char *p){
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline int stock_link_beat_sync_allowed(const unsigned char status[56],int32_t engine_master_id){
 uint32_t player=stock_gate_le32(status);
 if(engine_master_id<2||engine_master_id>3||player<1||player>4||player-1!=(uint32_t)engine_master_id)return 0;
 unsigned char sync=status[4],enable=status[5];
 if(!(sync&0x80)||(sync&0x24)!=0x24||!(enable&0x08))return 0;
 const unsigned char *lane=status+((enable&0x02)?0x28:0x18);
 if(!lane[0]||stock_gate_le32(lane+4)==0x7fffffffu)return 0;
 return stock_gate_le32(lane+((enable&0x04)?12:8))!=0;
}
#endif
