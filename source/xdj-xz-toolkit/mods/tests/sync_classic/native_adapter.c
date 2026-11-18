#include "native_fixture.inc"
extern uint32_t fixture_now_ms(void);
static uint32_t native_epoch;
static unsigned native_polls;
static unsigned read_window_us;
static int read_started;
void fixture_native_init(void){
 memset(engine,0,sizeof engine);memset(players,0,sizeof players);memset(player,0,sizeof player);
 memset(reader,0,sizeof reader);memset(impl,0,sizeof impl);memset(grid,0,sizeof grid);
 memset(manager,0,sizeof manager);memset(tempo,0,sizeof tempo);memset(units,0,sizeof units);
 memset(pause_queue,0,sizeof pause_queue);memset(ui,0,sizeof ui);memset(mixer,0,sizeof mixer);
 original_native_clock_fixture();reset();native_epoch=fixture_now_ms();native_polls=0;read_window_us=0;read_started=0;
 double pitch=.05;memcpy(tempo+0x30,&pitch,8);
}
void fixture_native_update(void){
 read_started=0;
 double ms=1125.+(fixture_now_ms()-native_epoch)*1.05;
 uint32_t frames=(uint32_t)(ms*44.1+.5),beat=(uint32_t)(ms/500.);
 put(player,0xe78,frames);put(player,0xedc,frames);
 put(units,4,beat*500u);units[0]=(unsigned char)(beat%4u+1u);
}
int fixture_memory_read(void *ctx,uint32_t a,void *out,size_t n){
 if(a==0x4bc28){native_polls++;read_started=1;}
 return fixture_read(ctx,a,out,n);
}
unsigned fixture_native_polls(void){return native_polls;}
void fixture_native_bad_guard(int v){bad_guard=v;}
void fixture_native_change_master(int v){master_reads=0;change_master=v;}
void fixture_native_change_source(int v){info_reads=0;change_source=v;}
void fixture_native_manual(int v){double bend=v?.02:0.;memcpy(tempo+0x38,&bend,8);}
void fixture_native_token(int v){put(impl,0x6a0,v?0x32100:0x32000);}
void fixture_native_external(int v){put(master_state,8,(uint32_t)v);}
void fixture_native_nominal(void){double pitch=0.;memcpy(tempo+0x30,&pitch,8);}
void fixture_native_read_window(unsigned us){read_window_us=us;}
uint64_t fixture_mono_us(void){return (uint64_t)fixture_now_ms()*1000u-(!read_started?read_window_us:0);}
