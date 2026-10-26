/* SPDX-License-Identifier: MIT */
/* Reuse the exact guarded native-memory fixture, not firmware or a live device. */
#define main existing_shell_model_checks
#include "../test_shell_model.c"
#undef main
#include "classic_mixer_runtime.c"
static struct xz_extra_decks fixture_extra;
static unsigned repaints,acquires,releases,active_holds;
static int fixture_layout=XZ_LAYOUT_MIXER;
int xz_deck_layout_get(void){return fixture_layout;}
static struct xz_extra_column wave_columns[188];
int xz_read_memory(uint32_t address,void *out,size_t count){return read_memory(address,out,count);}
int xz_extra_decks_snapshot_held(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 assert(!active_holds&&!hold->analysis[0]&&!hold->analysis[1]);
 *out=fixture_extra;hold->analysis[0]=wave_columns;active_holds=1;acquires++;return out->route_up;
}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 if(hold->analysis[0]){assert(active_holds);active_holds=0;releases++;hold->analysis[0]=NULL;}
 for(int i=0;i<2;i++){out->deck[i].wave.columns=NULL;out->deck[i].grid=NULL;}
}
void xz_native_skin_request_repaint(void){repaints++;}
static uint16_t pixels[800*480],footer_pixels[300*34];
int main(void){
 fixture();put32(0x3eaff7c,0);fixture_extra.route_up=1;
 for(unsigned i=0;i<188;i++)wave_columns[i]=(struct xz_extra_column){150,80,40};
 fixture_extra.deck[0].loaded=1;fixture_extra.deck[0].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
 fixture_extra.deck[0].wave=(struct xz_extra_wave){wave_columns,188,300000,270};
 assert(!xz_classic_mixer_runtime_start());
 const char *keys[2]={"4A","9B"};
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1000,keys));
 assert(xz_classic_mixer_runtime_visible());
 assert(!active_holds&&acquires==releases&&xz_classic_mixer_runtime_layout()==XZ_LAYOUT_MIXER);
 unsigned wave_pixels=0;for(int y=441;y<473;y++)for(int x=6;x<194;x++)if(pixels[y*800+x]!=pixels[y*800])wave_pixels++;
 assert(wave_pixels>188);
 assert(native.deck[0].auto_cue&&(native.deck[0].known&K(AUTO_CUE)));
 assert(!strcmp(native.deck[0].key,"4A"));
 unsigned reads=read_calls;
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1050,keys)&&read_calls==reads);
 for(unsigned i=0;i<300*34;i++)footer_pixels[i]=(uint16_t)(i+1);
 xz_classic_mixer_native_overview(0,footer_pixels,300*34,7,423);
 xz_classic_mixer_native_overview(1,footer_pixels,300*34,407,423);
 int x,y;
 assert(xz_classic_mixer_runtime_native_point(0,299,33,&x,&y)&&x==306&&y==456);
 assert(xz_classic_mixer_runtime_native_point(1,0,0,&x,&y)&&x==407&&y==423);
 assert(!xz_classic_mixer_runtime_native_point(2,0,0,&x,&y));
 xz_classic_mixer_native_overview(0,footer_pixels,300*34,407,423);
 xz_classic_mixer_native_overview(0,footer_pixels,300*34-1,7,423);
 assert(xz_classic_mixer_runtime_native_point(0,299,33,&x,&y)&&x==306);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1100,keys)&&read_calls>reads);
 assert(overview_sequence[0]&&overview_sequence[1]);
 assert(!memcmp(overview[0],footer_pixels,sizeof footer_pixels));
 /* A load invalidates old native overview pixels even if the capture lock is busy. */
 put16(title_record(0)+0x22,'X');put32(total(0)+0xc,999);
 pthread_mutex_lock(&overview_mutex);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1200,keys));
 assert(!overview_sequence[0]&&(pending_invalidation&1));
 pthread_mutex_unlock(&overview_mutex);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1210,keys));
 assert(!overview_sequence[0]&&!pending_invalidation);
 xz_classic_mixer_native_overview(0,footer_pixels,300*34,7,423);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1220,keys)&&overview_sequence[0]);
 unsigned before=repaints;
 fixture_extra.route_up=0;
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1230,keys));
 assert(!xz_classic_mixer_runtime_visible()&&repaints==before+1);
 xz_classic_mixer_runtime_hide();assert(repaints==before+1);
 fixture_extra.route_up=1;
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1300,keys));
 put32(0x3eaff7c,1);
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1400,keys));
 assert(!xz_classic_mixer_runtime_visible());
 assert(!active_holds&&acquires==releases);
 fixture_layout=XZ_LAYOUT_TWO;unsigned held_before=acquires;
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1450,keys)&&held_before==acquires);
 fixture_layout=XZ_LAYOUT_FOUR;
 xz_classic_mixer_runtime_stop();
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1500,keys));
 assert(!active_holds&&acquires==releases);
 puts("PASS held analysis through drawing, route-down/caution/TWO/shutdown releases; actual Classic mixer runtime: guarded native fields, 10Hz refresh, captured geometry, overview copy/load invalidation under contention, route loss and caution repaint, shutdown");
 return 0;
}
