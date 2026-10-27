from pathlib import Path
import argparse,json,hashlib
ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
src=args.source.resolve();OUT=out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
def block(s,start):
 at=s.index(start);end=s.index('{',at)+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[at:end]
def put(p,s):
 p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8',newline='\n')
s=(src/'shell_runtime.c').read_text()
native=(src/'tests/test_sync_native.c').read_text().split('int main(void){')[0]
native=native.replace('#include "../arcade/test_native_clock.c"','#include "arcade/test_native_clock.c"').replace('#include "../sync_native.h"','#include "sync_native.h"')
put(OUT/'native_fixture.inc',native)
cache=s[s.index('static pthread_mutex_t native_sync_mutex='):s.index('/* ---- model refresh (model thread) ---- */')]
frame=block(s,'int xz_shell_runtime_frame(')
# The retained scheduler ends after ownership is decided. Pixel drawing is irrelevant to this oracle.
frame=frame[:frame.index(' set_owns(1);')+len(' set_owns(1);')]+'\n result=-1;last_drawn_ms=now;dirty_flag=0;\ndone:\n roll_second(now);pthread_mutex_unlock(&mutex);return result;\n}\n'
owner=block(s,'void xz_shell_runtime_owner_tick(')
owner=owner[:owner.index('  if(debug){')]+'  }\n}\n'
wave=s[s.index(' for(int d=0;d<XZ_SHELL_NATIVE_DECKS;d++){\n  uint32_t id=loaded[d].known?'):s.index(' /* Playhead. The position comes')]
follow=block(s,'int xz_shell_runtime_follow_interface(')
wave_struct=s[s.index('struct wave_request {'):s.index('static struct {\n uint32_t gen; unsigned parts;')]
prefix=r'''#define _GNU_SOURCE
#include "shell_runtime.h"
#include "shell_media.h"
#include "sync_native.h"
#include "sync_observation.h"
#include "ui/themes.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>
extern uint32_t fixture_now_ms(void);
extern uint64_t fixture_mono_us(void);
extern int fixture_memory_read(void *,uint32_t,void *,size_t);
extern int xz_extra_decks_needs_native_sync(void);
static uint64_t mono_checked_us(void){return fixture_mono_us();}
static uint64_t now_us(void){return mono_checked_us();}
static uint32_t now_ms(void){return fixture_now_ms();}
static uint64_t mono_us(void){return mono_checked_us();}
static int plain_read(void *c,uint32_t a,void *o,size_t n){return fixture_memory_read(c,a,o,n);}
static int model_read(uintptr_t a,void *o,size_t n){return fixture_memory_read(NULL,(uint32_t)a,o,n);}
int xz_shell_model_quantize_grid(xz_shell_read_fn read,unsigned *den){(void)read;*den=1;return 1;}
extern void xz_extra_decks_set_quantize_grid(unsigned,int);
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static int enabled,owns_flag,refreshed,dirty_flag;
static int interface_modern=1;
int xz_shell_runtime_owns(void){return __atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE);}
static unsigned follow_bits;
static uint32_t last_call_ms,last_drawn_ms,last_refresh_ms,last_pump_ms,period_ms=100;
static uint32_t native_input_revision,shown_native_input,shown_extra_revision,shown_extra_control;
static struct xz_shell_runtime_frame_input last_input;
static struct xz_shell_model model;
static struct xz_shell shell;
enum xz_shell_screen xz_shell_screen(const struct xz_shell *s){(void)s;return XZ_SHELL_PLAY;}
static struct xz_shrt_handoff handoff;
static struct xz_shrt_queue queue;
static struct {int pending,deck,tries;uint32_t id,before,at;uint8_t device_kind;} load_check;
static const char *lock_holder="fixture";
static struct {uint32_t busy,lock_waits,calls,ui_cached,flips,skipped,tick_busy,ticks,pump;} sec;
FIXTURE_PROOF_DECLARATION
struct worst_frame {uint32_t total_us,lock_wait_us,refresh_us,render_us;const char *holder,*cause;int busy;};
static struct worst_frame worst_pending;
#define FRAME_LOCK_US 4000u
#define K(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
struct loaded {int known;uint32_t state,id;uint8_t device_kind;};
static struct loaded loaded[2],loaded_fixture[2];
static int read_loaded_changed;
static struct loaded read_loaded(int d){struct loaded l=loaded_fixture[d];if(read_loaded_changed&&d==0){read_loaded_changed=0;loaded_fixture[d].id++;}return l;}
static struct {uint32_t generation;struct xz_shmedia media;} usb[2];
static uint64_t published_generation[2];
uint64_t xz_extra_source_generation(int s){return (unsigned)(s-1)<2?published_generation[s-1]:0;}
static int memory_count_cache[2];
static struct xz_shrt_position position[2];
static struct xz_shrt_motion motion[2];
static unsigned grid_count[2];
static void *storage;
static int fixture_wave_requests,fixture_refreshes;
static void fixture_wave_clear(void){fixture_wave_requests++;}
#define xz_shell_model_set_wave(...) fixture_wave_clear()
#define xz_shell_model_set_stems(...) ((void)0)
static void set_owns(int on){owns_flag=on;}
static void roll_second(uint32_t now){(void)now;}
static int xz_native_skin_on_owner_thread(void){return 1;}
static void extra_refresh_locked(uint32_t now){(void)now;}
/* Display-only dependencies added since this scheduling fixture was written. */
static int apply_display_input(const struct xz_shell_runtime_frame_input *in){(void)in;return 0;}
static void extra_source_look_locked(void){}
uint32_t xz_band_palette_color(unsigned id,uint32_t fallback){(void)id;return fallback;}
uint32_t xz_shell_motion_ms(const struct xz_shell *a,const struct xz_shell_model *b){(void)a;(void)b;return 0;}

'''
prefix=prefix.replace('FIXTURE_PROOF_DECLARATION',block(s,'struct xz_shell_proof {')+';\nstatic struct xz_shell_proof xz_mods_shell_proof_v1;')
prefix+='#include "arcade/native_clock.h"\n#include "shell_utility_table.h"\n#include <stdio.h>\n'
prefix+=r'''
static int fixture_run_rc,fixture_run_calls,fixture_code_ok=1,fixture_guard_token_change;
static void (*fixture_execute_hook)(void);
static int fixture_native_load(int deck,void *id,int mode){
 (void)deck;(void)id;assert(mode==3);fixture_run_calls++;
 if(fixture_execute_hook)fixture_execute_hook();
 return fixture_run_rc==0;
}
static int xz_read_memory(uint32_t address,void *out,size_t n);
static void lock_as(const char *name){(void)name;pthread_mutex_lock(&mutex);}
static struct {int pending,slot;uint32_t id;uint64_t posted_us;struct xz_shmedia media;} hint[2];
static struct {unsigned set_fails,sets;int poll_now;} util;
static void notice_locked(uint32_t now,const char *text){(void)now;(void)text;}
'''
prefix=prefix.replace('static int fixture_run_rc,',s[s.index('#define TOTAL_WK '):s.index('\n',s.index('#define TOTAL_WK '))]+'\nstatic int fixture_run_rc,')
for name in ('set_time_mode_code','set_quantize_code','load_code','music_id_code'):
 at=s.index('static const unsigned char '+name+'[');prefix+=s[at:s.index('\n',at)]+'\n'
prefix+=block(s,'static int word(')+'\n'+block(s,'static int code_is(')+'\n'
prefix+=r'''
static int xz_read_memory(uint32_t address,void *out,size_t n){
 for(int d=0;d<2;d++)if(address==TOTAL_WK+0x1610u+(uint32_t)d*0x60u){
  assert(n==4);memcpy(out,&loaded_fixture[d].state,n);return !loaded_fixture[d].known;
 }
 const struct {uint32_t address;const unsigned char *bytes;size_t n;} code[]={
  {0xca308,set_time_mode_code,sizeof set_time_mode_code},{0xc9304,set_quantize_code,sizeof set_quantize_code},
  {0x135728,load_code,sizeof load_code},{0x13ba38,music_id_code,sizeof music_id_code}
 };
 for(unsigned i=0;i<sizeof code/sizeof code[0];i++)if(address==code[i].address){
  if(fixture_guard_token_change){extern void fixture_native_token(int);fixture_guard_token_change=0;fixture_native_token(1);}
  assert(n==code[i].n);memcpy(out,code[i].bytes,n);if(!fixture_code_ok)((unsigned char *)out)[0]^=1;return 0;
 }
 assert(0);return -1;
}
'''
at=s.index('enum { LOAD_OK=');prefix+=s[at:s.index('\n',at)]+'\n'
prefix+=block(s,'static int load_refused(')+'\n'
command=block(s,'static int run_command(')
native='((int(*)(int,void *,int))(uintptr_t)0x135728)(c->deck,id,3)'
assert command.count(native)==1
command=command.replace(native,'fixture_native_load(c->deck,id,3)')
prefix+=block(s,'static int settled(')+'\n'+wave_struct+'\n'
prefix+=block(s,'static int candidate(')+'\n'+block(s,'static int owns_now(')+'\n'
prefix+=cache+'\n'+command+'\n'
prefix+='static void refresh_locked(uint32_t now,const struct xz_shell_runtime_frame_input *in){\n (void)in;fixture_refreshes++;for(int d=0;d<2;d++)loaded[d]=read_loaded(d);unsigned changed=0;\n'+wave+'\nrefreshed=1;last_refresh_ms=now;}\n'
prefix+=frame+'\n'+owner+'\n'
# Run the original lifecycle decision deterministically, without starting a real worker/thread.
prefix+='static int fixture_start_calls,fixture_stop_calls;\nint xz_shell_runtime_start(void){enabled=1;fixture_start_calls++;return 0;}\nvoid xz_shell_runtime_stop(void){enabled=0;owns_flag=0;refreshed=0;native_sync_clear();fixture_stop_calls++;}\nstatic void *stop_thread(void *p){(void)p;xz_shell_runtime_stop();return NULL;}\nstatic void xz_log(const char *p){(void)p;}\nstatic int fixture_create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void*),void *p){(void)t;(void)a;fn(p);return 0;}\n#define pthread_create fixture_create\n'
prefix+=follow+'\n#undef pthread_create\n'
prefix+=r'''
void fixture_shell_reset(void){
 enabled=owns_flag=refreshed=dirty_flag=0;fixture_wave_requests=fixture_refreshes=0;
 fixture_start_calls=fixture_stop_calls=0;last_refresh_ms=last_drawn_ms=0;
 memset(&model,0,sizeof model);memset(&handoff,0,sizeof handoff);
 memset(wave_job,0,sizeof wave_job);memset(usb,0,sizeof usb);native_sync_clear();
 for(int d=0;d<2;d++)loaded_fixture[d]=(struct loaded){1,0,10u+(unsigned)d,2};
 usb[0].generation=42;usb[0].media.generation=42;usb[0].media.mount_id=7;published_generation[0]=42;published_generation[1]=0;
 usb[0].media.root_inode=91;usb[0].media.root_device=92;
 read_loaded_changed=0;memset(native_sync_source,0,sizeof native_sync_source);memset(&queue,0,sizeof queue);memset(&load_check,0,sizeof load_check);fixture_run_rc=fixture_run_calls=fixture_guard_token_change=0;fixture_code_ok=1;fixture_execute_hook=NULL;
}
int fixture_frame(int eligible,int theme){
 struct xz_shell_runtime_frame_input in={.eligible=eligible,.theme=theme};uint16_t pixels[XZ_SHELL_WIDTH]={0};
 return xz_shell_runtime_frame(pixels,XZ_SHELL_WIDTH,&in);
}
int fixture_waves(void){return fixture_wave_requests;}
int fixture_refresh_count(void){return fixture_refreshes;}
unsigned fixture_wave_revision(void){return wave_job[0].wanted;}
void fixture_load_state(unsigned state){loaded_fixture[0].state=state;}
void fixture_load_id(unsigned id){loaded_fixture[0].id=id;}
void fixture_media_generation(unsigned n){usb[0].generation=n;usb[0].media.generation=n;published_generation[0]=n;}
void fixture_media_publication(unsigned n){published_generation[0]=n;}
void fixture_media_reuse(void){usb[0].media.attachment_inode++;}
void fixture_torn_load(void){read_loaded_changed=1;}
void fixture_handoff(void){handoff.handed=1;}
int fixture_stops(void){return fixture_stop_calls;}
void fixture_known_load(int deck){struct xz_shrt_command c={.kind=XZ_SHRT_CMD_LOAD,.deck=deck,.track_id=10u+(unsigned)deck,.device_kind=2,.at_ms=now_ms(),.media_generation=usb[0].generation};assert(xz_shrt_queue_push(&queue,&c));}
'''
prefix+=r'''
void review_command_result(int rc){fixture_run_rc=rc;fixture_run_calls=0;}
int review_command_calls(void){return fixture_run_calls;}
void review_load_generation(int deck,unsigned generation,int device){struct xz_shrt_command c={.kind=XZ_SHRT_CMD_LOAD,.deck=deck,.track_id=10u+(unsigned)deck,.device_kind=(unsigned char)device,.at_ms=now_ms(),.media_generation=generation};assert(xz_shrt_queue_push(&queue,&c));}
void review_load_id(int deck,unsigned id){loaded_fixture[deck].id=id;}
void review_reset_fence(void){memset(native_sync_source,0,sizeof native_sync_source);memset(&queue,0,sizeof queue);memset(&load_check,0,sizeof load_check);}
void review_execute_hook(void (*fn)(void)){fixture_execute_hook=fn;}
void review_code_guard(int ok){fixture_code_ok=ok;}
int review_command_rc(void){return (int32_t)xz_mods_shell_proof_v1.last_command_result;}
void review_load_known(int deck,int known){loaded_fixture[deck].known=known;}
void review_guard_token_change(void){fixture_guard_token_change=1;}
'''
if 'native_sync_load_locked' in s:
    prefix+='void fixture_revision_limit(void){native_sync_revision=UINT32_MAX;}\n'
else:
    prefix+='void fixture_revision_limit(void){}\n'
prefix+='void review_observation_limit(void){'+('native_sync_source[0].observation=UINT32_MAX;' if 'uint32_t observation,observed_track;' in s else '')+'}\n'
put(out/'shell_adapter.c',prefix)
put(out/'extracts.json',json.dumps({k:{'sha256':hashlib.sha256(v.encode()).hexdigest(),'text':v} for k,v in {'cache':cache,'frame_scheduler':frame,'owner_scheduler':owner,'wave_identity':wave,'follow_interface':follow}.items()},indent=2)+'\n')
print('PASS extracted production scheduling/cache/lifecycle')


runtime=(src/'tests/test_global_jog_runtime.c').read_text()
prefix=runtime[:runtime.index('static int stock_contacts;')]
prefix=prefix.replace('#include "../runtime.c"','#define xz_hook_arm fixture_unused_runtime_hook\n#include "runtime.c"\n#undef xz_hook_arm')
prefix=prefix.replace('extern void test_player_init(void);','extern void test_player_init(void);\nextern uint32_t fixture_now_ms(void);\nextern int fixture_snapshot(struct xz_extra_decks *);\nextern void fixture_sides(int,int);\nextern int fixture_native_master(void);')
prefix=prefix.replace('native_master_requests++;return 1;','native_master_requests++;return fixture_native_master();')
prefix=prefix.replace('memset(out,0,sizeof *out);return 0;','return fixture_snapshot(out);')
prefix=prefix.replace('(void)left;(void)right;','fixture_sides(left,right);')
prefix=prefix.replace('return test_clock++;','return fixture_now_ms();')
put(out/'runtime_adapter.c',prefix+"""
void fixture_runtime_reset(void){installed=1;observer=0;route_live=1;game_owner=0;delivered=0;native_master_requests=0;test_clock=1000;xz_deck_select_init(&deck_select);xz_deck_select_route(&deck_select,1);}
int fixture_global_key(unsigned code,unsigned channel,unsigned op){return xz_runtime_deck_global_key(code,channel,op);}
int fixture_global_input(const void *p){return xz_runtime_deck_global_input(NULL,p);}
int fixture_toggle(int side){return xz_runtime_deck_select_toggle(side);}
int fixture_delivered(void){return delivered;}
void fixture_route(int live){route_live=live;}
""")
ui=(src/'ui_runtime.c').read_text();dispatcher=block(ui,'static void menu_back_key_hook(')
dispatcher=dispatcher[:dispatcher.index('    /* REKORDBOX: decided once')]+'    fixture_stock_global++;\n}\n'
put(out/'dispatcher.inc',dispatcher)
