/* SPDX-License-Identifier: MIT */
static struct xz_pc_stem_source fixture_pc_source[2]={{.slot=-1,.epoch=1},{.slot=-1,.epoch=1}};
static int fixture_pc_valid[2]={1,1},fixture_pc_ready=1,fixture_pc_observer=1;
static unsigned fixture_pc_notes,fixture_pc_channel[128],fixture_pc_note[128];
static int fixture_pc_pressed[128],fixture_pc_sink_ok=1,fixture_pc_stock;
static int fixture_pc_sink(void *context,unsigned channel,unsigned note,int pressed){
    (void)context;assert(fixture_pc_notes<128);
    unsigned n=fixture_pc_notes++;fixture_pc_channel[n]=channel;fixture_pc_note[n]=note;fixture_pc_pressed[n]=pressed;
    return fixture_pc_sink_ok;
}
int xz_pc_stems_native_ready(void){return fixture_pc_ready;}
int xz_pc_stems_native_observe(void){return fixture_pc_observer?0:-1;}
static unsigned fixture_pc_roles=11,fixture_pc_role_reads;
unsigned xz_pc_wave_available_roles(void){fixture_pc_role_reads++;return fixture_pc_roles;}
static unsigned fixture_pc_redraws;
static unsigned fixture_pc_metadata_redraws;
int xz_pc_stems_native_redraw(void){
    assert(!pthread_mutex_trylock(&pc_mutex));pthread_mutex_unlock(&pc_mutex);
    assert(!pthread_mutex_trylock(&ui_mutex));pthread_mutex_unlock(&ui_mutex);
    fixture_pc_metadata_redraws++;return 1;
}
int xz_pc_stems_native_source(unsigned physical,struct xz_pc_stem_source *out){
    if(physical>=2||!fixture_pc_valid[physical])return 0;
    *out=fixture_pc_source[physical];return 1;
}
struct xz_pc_stem_sink xz_pc_stems_native_sink(void){return (struct xz_pc_stem_sink){NULL,fixture_pc_sink};}
int xz_pc_stems_native_feedback(struct xz_pc_stems *stems,const void *message){return xz_pc_stems_feedback(stems,message,3);}
static __attribute__((unused)) void fixture_pc_stock_callback(void *self,void *input,const void *message){
    (void)self;(void)input;(void)message;fixture_pc_stock++;
}
