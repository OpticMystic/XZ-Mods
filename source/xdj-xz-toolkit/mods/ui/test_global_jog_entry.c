#define _POSIX_C_SOURCE 200809L
/* Actual ui_runtime global entry: instrument guards without native address dereferences. */
#include <pthread.h>
static int tracked_lock(pthread_mutex_t *mutex);
#define pthread_mutex_lock tracked_lock
#define main unused_existing_controls
#include "test_runtime_controls.c"
#undef main
#undef pthread_mutex_lock
static unsigned entry_preads,entry_ui_locks,entry_calls,entry_stock;
static int entry_take=1,entry_memory_ok;
static void *entry_self;
static int tracked_lock(pthread_mutex_t *mutex){if(mutex==&ui_mutex)entry_ui_locks++;return pthread_mutex_lock(mutex);}
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){
 entry_preads++;if(!entry_memory_ok)return -1;
 uint32_t value=0;
 if(address==0x1c8a308)value=0x100000;
 else if(address==0x100040)value=0x110000;
 else if(address==0x100048)value=2;
 else if(address==0x110000||address==0x110004)value=0x120000+(address==0x110004?0x1000:0);
 else if(address==0x120138||address==0x121138)value=0x130000+(address==0x121138?0x1000:0);
 else if(address==0x130026||address==0x131026){assert(n==1);*(unsigned char *)out=address==0x130026?1:2;return 0;}
 else return -1;
 assert(n==4);memcpy(out,&value,4);return 0;
}
int xz_runtime_deck_early_key(uint16_t code){return code==0x4305||code==0x4306||code==0x4101||code==0x4102;}
int xz_runtime_deck_games_pending(void){return xz_ui_runtime_arcade_input_pending();}
int xz_runtime_deck_global_input(void *self,const void *input){(void)input;entry_calls++;entry_self=self;return xz_runtime_deck_games_pending()&&!self?0:entry_take;}
static void stock_entry(void *self,const void *input){(void)self;(void)input;entry_stock++;}
int main(void){
 unsigned char input[24]={0};uint16_t code=0x4305;memcpy(input+8,&code,2);input[10]=1;input[11]=4;
 xz_ui_init(&ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);started=1;stock_global_key=stock_entry;entry_ui_locks=0;
 for(int i=0;i<1000;i++)menu_back_key_hook(NULL,input);
 assert(entry_calls==1000&&entry_preads==0&&entry_ui_locks==0&&entry_stock==0&&entry_self==NULL);
 for(int control=0;control<2;control++){
  code=control?0x4102:0x4101;memcpy(input+8,&code,2);input[11]=0;
  unsigned before=entry_calls;for(int i=0;i<1000;i++)menu_back_key_hook(NULL,input);
  assert(entry_calls==before+1000&&entry_preads==0&&entry_ui_locks==0&&entry_self==NULL);
 }
 code=0x4305;memcpy(input+8,&code,2);input[11]=4;
 entry_take=0;menu_back_key_hook(NULL,input);assert(entry_stock==1&&entry_preads==0&&entry_ui_locks==0);entry_take=1;
 /* Actual game open/close functions publish availability before the next input event. */
 struct xz_ui_action a={.kind=XZ_UI_ARCADE_OPEN};pthread_mutex_lock(&ui_mutex);apply(NULL,&a,1);pthread_mutex_unlock(&ui_mutex);
 assert(xz_ui_runtime_arcade_input_pending());entry_preads=entry_ui_locks=0;
 menu_back_key_hook(NULL,input);assert(entry_preads==1&&entry_stock==2&&entry_self==NULL);
 entry_memory_ok=1;entry_preads=entry_ui_locks=0;menu_back_key_hook(NULL,input);
 assert(entry_preads==6&&entry_self==(void *)(uintptr_t)0x130000&&entry_stock==2);
 pthread_mutex_lock(&ui_mutex);arcade_to_dj();pthread_mutex_unlock(&ui_mutex);assert(!xz_ui_runtime_arcade_input_pending());
 entry_preads=entry_ui_locks=0;menu_back_key_hook(NULL,input);assert(entry_preads==0&&entry_ui_locks==0&&entry_self==NULL);
 unsigned char innards[0x100]={0};innards[0x26]=1;
 assert(!xz_ui_runtime_arcade_key(innards,input));assert(entry_ui_locks==0);
 pthread_mutex_lock(&ui_mutex);apply(NULL,&a,1);pthread_mutex_unlock(&ui_mutex);
 code=0x4306;memcpy(input+8,&code,2);input[11]=0;
 assert(xz_ui_runtime_arcade_key(innards,input));
 pthread_mutex_lock(&ui_mutex);arcade_to_dj();pthread_mutex_unlock(&ui_mutex);
 assert(xz_ui_runtime_arcade_input_pending());input[11]=2;
 assert(xz_ui_runtime_arcade_key(innards,input));assert(!xz_ui_runtime_arcade_input_pending());
 entry_ui_locks=entry_preads=0;code=0x4305;memcpy(input+8,&code,2);input[11]=4;
 menu_back_key_hook(NULL,input);assert(entry_ui_locks==0&&entry_preads==0);
 for(int side=0;side<2;side++)for(int which=0;which<2;which++){
  pthread_mutex_lock(&ui_mutex);apply(NULL,&a,1);pthread_mutex_unlock(&ui_mutex);
  code=which?0x4119:0x4102;memcpy(input+8,&code,2);input[10]=(unsigned char)(side+1);input[11]=0;innards[0x26]=(unsigned char)(side+1);
  assert(xz_ui_runtime_arcade_key(innards,input));
  pthread_mutex_lock(&ui_mutex);arcade_to_dj();pthread_mutex_unlock(&ui_mutex);
  assert(xz_ui_runtime_arcade_input_pending());input[11]=2;
  assert(xz_ui_runtime_arcade_key(innards,input));assert(!xz_ui_runtime_arcade_input_pending());
 }
 puts("PASS actual UI global jog entry: 1000 each music JOG/PLAY/CUE, zero native reads/UI locks; native pass; game open/guarded read/close");
 return 0;
}


