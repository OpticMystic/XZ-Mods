/* SPDX-License-Identifier: MIT */
/* Bench ops 24..25 (bench_lights.h): the read-out's wire layout and the presses' checks and key sequences. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../bench_lights.h"

static void encode(void){
 struct xz_bl_view v;struct xz_bench_result r;memset(&v,0,sizeof v);
 v.lamps_known=1;
 v.lamp[0]=(struct xz_bl_lamp){1,0,1,1,0,0xffffffu};      /* L-MASTER lit */
 v.lamp[3]=(struct xz_bl_lamp){1,1,2,1,250,0x00ff00u};    /* R-SYNC flashing, forced */
 v.ui_known=1;v.ui_master=4;v.engine_known=1;v.engine_master=3;
 v.sentinel_known=1;v.sentinel_master[0]=1;v.sentinel_sync[1]=1;
 v.side_deck[0]=1;v.side_deck[1]=4;
 v.engine_sync_known=1;v.engine_sync[0]=1;
 v.stat_known=1;v.stat_master[0]=1;v.stat_sync[0]=1;v.stat_flash[1]=1;
 v.bridge_actual=3;  /* XZ_MASTER_DECK4 */
 v.soft_known[1]=1;v.soft_loaded[1]=1;v.soft_master[1]=1;v.soft_sync[1]=1;v.soft_offset[1]=1;
 v.ml_ui_ready=v.ml_insert_ready=v.ml_owned=1;v.ml_written=4;v.ml_inserted=0x12345;
 xz_bl_encode(&v,0,&r);
 assert(!r.status);
 assert((uint32_t)r.args[0]==(0x83u|(0x80u<<8)|(0x80u<<16)|(0x95u<<24)));
 assert((uint32_t)r.args[1]==(4u|3u<<8|(0x80u|1u|8u)<<16|(1u|4u<<4)<<24));
 assert((uint32_t)r.args[2]==(0x81u|(0x80u|1u|4u|32u)<<8|7u<<16|(7u|6u<<4)<<24));
 assert((uint32_t)r.args[3]==(0u|(0x80u|15u)<<8|0x2345u<<16));
 /* Unknown values: 0x7f numbers, no read bits. */
 memset(&v,0,sizeof v);v.ui_master=-1;v.bridge_actual=-3;v.ml_written=-1;
 xz_bl_encode(&v,0,&r);
 assert(!r.status&&r.args[0]==0&&(uint32_t)r.args[1]==(0x7fu|0x7fu<<8)&&(uint32_t)r.args[2]==(1u<<16|1u<<28)&&r.args[3]==0);
 v.ui_known=1;xz_bl_encode(&v,0,&r);assert(((uint32_t)r.args[1]&0xff)==0xffu);   /* -1: none */
 /* One lamp. */
 memset(&v,0,sizeof v);v.lamps_known=1;v.lamp[3]=(struct xz_bl_lamp){1,1,2,1,250,0x00ff00u};
 xz_bl_encode(&v,4,&r);
 assert(!r.status&&r.args[0]==2&&r.args[1]==1&&r.args[2]==250&&(uint32_t)r.args[3]==(0x00ff00u|1u<<24|1u<<28|1u<<30));
 v.lamps_known=0;xz_bl_encode(&v,4,&r);assert(r.status==-EAGAIN);
 xz_bl_encode(&v,5,&r);assert(r.status==-EINVAL);xz_bl_encode(&v,-1,&r);assert(r.status==-EINVAL);
}

struct fake { int shows[2],keys,fail_release_once,fail_press,available; unsigned code[16],op[16],ch[16]; };
static int fake_send(void *ctx,unsigned code,unsigned op,unsigned ch){
 struct fake *f=ctx;
 if(op==XZ_BL_OP_KEY_PRESS&&f->fail_press)return -EAGAIN;
 if(op==XZ_BL_OP_KEY_RELEASE&&f->fail_release_once){f->fail_release_once--;return -EAGAIN;}
 assert(f->keys<16);f->code[f->keys]=code;f->op[f->keys]=op;f->ch[f->keys]=ch;f->keys++;return 0;
}
static int fake_side(void *ctx,int side){return ((struct fake *)ctx)->shows[side];}
static int fake_available(void *ctx){return ((struct fake *)ctx)->available;}
static struct xz_bench_result press(struct fake *f,unsigned side,int what,int expect){
 struct xz_bl_env e={f,fake_send,fake_side,fake_available};
 struct xz_bench_request q;memset(&q,0,sizeof q);q.op=XZ_BL_OP_PRESS;q.deck=side;q.args[0]=what;q.args[1]=expect;
 struct xz_bench_result r;memset(&r,0,sizeof r);
 xz_bl_press(&e,&q,&r);return r;
}
static void presses(void){
 struct fake f;memset(&f,0,sizeof f);f.shows[0]=1;f.shows[1]=4;f.available=1;
 struct xz_bench_result r=press(&f,1,XZ_BL_PRESS_MASTER,4);
 assert(!r.status&&r.args[0]==1&&r.args[1]==4&&f.keys==2);
 assert(f.code[0]==XZ_BL_KEY_MASTER&&f.op[0]==0&&f.op[1]==2&&f.ch[0]==2&&f.ch[1]==2);
 f.keys=0;r=press(&f,0,XZ_BL_PRESS_SYNC,0);
 assert(!r.status&&f.keys==2&&f.code[0]==XZ_BL_KEY_SYNC&&f.ch[0]==1);
 /* The gesture: SHIFT press, release, press, release on the side's channel. */
 f.keys=0;r=press(&f,0,XZ_BL_PRESS_DECK_SELECT,1);
 assert(!r.status&&r.args[0]==2&&f.keys==4);
 for(int i=0;i<4;i++)assert(f.code[i]==XZ_BL_KEY_SHIFT&&f.ch[i]==1&&f.op[i]==(i&1?2u:0u));
 /* Refusals: wrong deck expected, unknown side, a side showing another side's deck, bad args, no keys. */
 f.keys=0;r=press(&f,1,XZ_BL_PRESS_MASTER,2);assert(r.status==-ESTALE&&!f.keys&&r.args[1]==4);
 f.shows[1]=-1;r=press(&f,1,XZ_BL_PRESS_MASTER,0);assert(r.status==-EAGAIN&&!f.keys);
 f.shows[1]=3;r=press(&f,1,XZ_BL_PRESS_MASTER,0);assert(r.status==-EAGAIN&&!f.keys);
 f.shows[1]=2;
 r=press(&f,2,XZ_BL_PRESS_MASTER,0);assert(r.status==-EINVAL);
 r=press(&f,1,0,0);assert(r.status==-EINVAL);r=press(&f,1,4,0);assert(r.status==-EINVAL);
 r=press(&f,1,XZ_BL_PRESS_MASTER,5);assert(r.status==-EINVAL);
 f.available=0;r=press(&f,1,XZ_BL_PRESS_MASTER,0);assert(r.status==-EOPNOTSUPP&&!f.keys);f.available=1;
 /* A refused press sends nothing; a refused release is retried once, then reported. */
 f.fail_press=1;r=press(&f,1,XZ_BL_PRESS_MASTER,0);assert(r.status==-EAGAIN&&!f.keys);f.fail_press=0;
 f.fail_release_once=1;r=press(&f,1,XZ_BL_PRESS_MASTER,0);assert(!r.status&&f.keys==2);
 f.keys=0;f.fail_release_once=2;r=press(&f,1,XZ_BL_PRESS_MASTER,0);assert(r.status==-EIO&&r.args[3]==1&&f.keys==1);
 /* The gesture stops after a failed first tap. */
 f.keys=0;f.fail_release_once=2;r=press(&f,0,XZ_BL_PRESS_DECK_SELECT,0);assert(r.status==-EIO&&f.keys==1);
}
int main(void){
 encode();presses();
 assert(XZ_BL_OP_MASK==((1u<<24)|(1u<<25)));
 puts("bench lights: ok");
 return 0;
}
