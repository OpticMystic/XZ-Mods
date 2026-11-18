#ifdef NDEBUG
#error Layered ownership requires active assertions
#endif
#include "../ui/layered_wave_runtime.c"
#include <assert.h>
static int owns=1,panel,loads,reads;
int xz_ui_runtime_native_wave_visible(void){return !owns&&!panel;}
int xz_shell_runtime_owns(void){return owns;}
int xz_read_memory(uint32_t at,void *out,size_t n){
 reads++;if(at!=XZ126_PLAY_ENGINE_CELL||n!=4)return -1;
 *(uint32_t*)out=0x10000;return 0;
}
int xz126_deck_source_read(xz126_read_fn read,void *context,uint32_t engine,unsigned deck,struct xz126_deck_source *out){
 (void)read;(void)context;(void)engine;memset(out,0,sizeof *out);
 out->player=0x20000+deck*0x1000;out->file_frames=44100;
 snprintf(out->path,sizeof out->path,"/media/usb2/sda2/Contents/track-%u.wav",deck);return 0;
}
int xz_pc_stems_native_source(unsigned d,struct xz_pc_stem_source *out){(void)d;memset(out,0,sizeof *out);return 0;}
void xz_pc_wave_state(void){}
int xz_pc_wave_native_stamp(unsigned s,struct xz_pc_wave_stamp *out){(void)s;(void)out;return 0;}
int xz_pc_wave_load(unsigned a,struct xz_wave_load *b,unsigned *c,uint32_t *d,uint32_t *e){(void)a;(void)b;(void)c;(void)d;(void)e;return -1;}
int xz_pc_wave_current(unsigned a,unsigned b,uint32_t c,uint32_t d,uint32_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
int xz_wave_load_stems(const char *p,struct xz_wave_load *out){(void)p;(void)out;loads++;return -1;}
int xz_wave_load_three_band(const char *p,struct xz_wave_load *out){
 (void)p;loads++;out->bands=calloc(3,1);assert(out->bands);out->count=out->normalization=1;out->kind=XZ_LAYERED_THREE_BAND;return 0;
}
void xz_wave_load_free(struct xz_wave_load *out){free(out->bands);memset(out,0,sizeof *out);}
void xz_log(const char *message){(void)message;}
int main(void){
 started=1;mode=XZ_WAVE_STEMS;
 for(int i=0;i<100;i++)worker_step(1000+i*250);
 assert(loads==0&&reads==0);assert(!xz_layered_wave_active());
 owns=0;worker_step(30000);assert(loads==4&&live[0]&&live[1]);assert(xz_layered_wave_active());
 int before=loads;owns=1;
 for(int i=0;i<100;i++)worker_step(31000+i*250);
 assert(loads==before);assert(live[0]&&live[1]);
 owns=0;panel=1;worker_step(59000);assert(loads==before&&!xz_layered_wave_active());
 panel=0;mode=XZ_WAVE_STOCK;worker_step(60000);assert(!live[0]&&!live[1]);
 for(unsigned i=0;i<RETIRE_SLOTS;i++)if(retired[i].p)prepared_free(retired[i].p);
 puts("PASS MODERN ownership prevents legacy waveform I/O, preserves handback cache and resumes native loading");return 0;
}
