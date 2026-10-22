/* SPDX-License-Identifier: MIT */
/* White-box boundary injection in actual own C; no new production test API. */
#include "../sync_link_runtime.c"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error Lifecycle tests require assertions
#endif
int xz_hook_arm(uint32_t a,const unsigned char e[8],void *r,void **o){(void)a;(void)e;(void)r;(void)o;return -1;}
int xz_extra_decks_use_native_master(void){return 0;}
static int lifecycle_clock(uint64_t *out){*out=1000000;return 0;}
static struct xz_sync_link_test_ops lifecycle_hooks;
static int legacy_calls;
static void legacy_callback(void){legacy_calls++;}
static void install(void){
 struct xz_sync_link_test_ops ops={.monotonic_us=lifecycle_clock,.on_use_native_master=legacy_callback};
 xz_sync_link_runtime_test_install(&ops,&lifecycle_hooks);
}
static uint32_t token(void){
 struct xz_master_observation o;
 assert(xz_sync_link_runtime_master_observe(&o));assert(o.deck==XZ_MASTER_UNKNOWN&&!o.revision&&!o.request_epoch&&!o.lease_generation);
 assert(o.operator_epoch);return o.operator_epoch;
}
static void failed(void){
 struct xz_master_observation o,empty={.deck=XZ_MASTER_UNKNOWN};memset(&o,0xa5,sizeof o);
 assert(!xz_sync_link_runtime_master_observe(&o));assert(!memcmp(&o,&empty,sizeof o));
}
static void stop_start(void){
 install();assert(token()==1);
 unsigned char sm[64]={0};int native=1;lifecycle_hooks.set_master_on(sm,&native);
 uint32_t before=token();assert(before==2);
 xz_sync_link_runtime_stop();failed();install();assert(token()>before);
 struct xz_extra_decks d={.route_up=1,.master_intent={.deck=2,.epoch=1,.explicit_choice=1,.operator_epoch=before}};
 xz_sync_link_runtime_step(&d,NULL);assert(!xz_sync_link_runtime_master_committed(0));
 puts("PASS operator token starts1, stop/start advances fencing, surviving old request never becomes current");
}
static void exhaustion(void){
 install();assert(token()==1);atomic_store(&rt_va.operator_epoch,UINT32_MAX);
 unsigned char sm[64]={0};int native=1;lifecycle_hooks.set_master_on(sm,&native);failed();
 assert(!atomic_load(&rt_va.operator_epoch));xz_sync_link_runtime_stop();install();failed();
 assert(!atomic_load(&rt_va.operator_epoch));
 puts("PASS actual operator exhaustion clears observation and remains sticky across stop/start");
}
static void cache_exhaustion(void){
 install();atomic_store(&rt_actual_serial,UINT32_MAX-1);
 struct rt_master_sample s={.actual={.deck=XZ_MASTER_NONE,.operator_epoch=1},.at_us=1000000,.sequence=1,.native_id=-1};
 publish_master_sample(&s);failed();assert(atomic_load(&rt_actual_exhausted));
 xz_sync_link_runtime_stop();install();failed();assert(atomic_load(&rt_actual_exhausted));
 puts("PASS actual publication counter exhaustion remains sticky across stop/start");
}
static void pinned_restart(void){
 install();uint32_t op=token();
 assert(xz_va_claim(&rt_va,3,1,xz_va_probe_epoch(&rt_va,3)));
 struct xz_va_bundle b={.source_id=3,.request_epoch=1,.captured_mono_us=1000000};put_le32(b.status,3);
 uint32_t first=xz_va_ticket(&rt_va);assert(xz_va_offer(&rt_va,&b,first));
 struct xz_va_frame borrowed={0};assert(xz_va_begin(&rt_va,&borrowed)==1);
 unsigned slot=borrowed.slot;xz_sync_link_runtime_stop();install();assert(token()>op);
 assert(atomic_load(&rt_va.slots[slot].refs)==1);
 assert(xz_va_claim(&rt_va,3,1,xz_va_probe_epoch(&rt_va,3)));
 uint32_t next=xz_va_ticket(&rt_va);assert(next>first);assert(!runtime_frame_fresh(&borrowed));
 assert(xz_va_offer(&rt_va,&b,next));xz_va_end(&rt_va,&borrowed);assert(!atomic_load(&rt_va.slots[slot].refs));
 puts("PASS stop/start preserves borrowed slot lifetime and advances lease identity");
}
static void no_legacy_fallback(void){
 install();unsigned char sm[64]={0};int native=1;lifecycle_hooks.set_master_on(sm,&native);
 xz_sync_link_runtime_step(NULL,NULL);struct xz_extra_decks empty={0};
 xz_sync_link_runtime_step(&empty,NULL);assert(!legacy_calls);
 puts("PASS initial missing/zero snapshots never infer legacy fixture mode");
}
static void busy_authority_token(void){
 install();atomic_store(&rt_actual_serial,1);
 struct xz_master_observation a,expected={.deck=XZ_MASTER_UNKNOWN,.operator_epoch=1};
 assert(xz_sync_link_runtime_master_observe(&a)&&!memcmp(&a,&expected,sizeof a));
 unsigned char sm[64]={0};int native=1;lifecycle_hooks.set_master_on(sm,&native);
 expected.operator_epoch=2;
 assert(xz_sync_link_runtime_master_observe(&a)&&!memcmp(&a,&expected,sizeof a));
 atomic_store(&rt_va.operator_epoch,0);failed();
 atomic_store(&rt_va.operator_epoch,2);atomic_store(&rt_actual_exhausted,1);failed();
 puts("PASS busy authority returns only a coherent current control token; unavailable/exhausted token fails cleared");
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"stop-start"))stop_start();else if(!strcmp(argv[1],"exhaustion"))exhaustion();
 else if(!strcmp(argv[1],"cache-exhaustion"))cache_exhaustion();else if(!strcmp(argv[1],"pinned-restart"))pinned_restart();
 else if(!strcmp(argv[1],"no-legacy-fallback"))no_legacy_fallback();
 else if(!strcmp(argv[1],"busy-authority-token"))busy_authority_token();else assert(0);
 return 0;
}
