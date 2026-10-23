/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static void *startup_dlsym(void *,const char *);
static int startup_thread(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
static char *startup_env(const char *);
static int startup_key_browser(void);
static int startup_key_traffic(void);
#define dlsym startup_dlsym
#define pthread_create startup_thread
#define getenv startup_env
#define xz_key_browser_start startup_key_browser
#define xz_key_traffic_start startup_key_traffic
#define main preserved_runtime_main
#ifndef XZ_RUNTIME_CONTROLS_FIXTURE
#define XZ_RUNTIME_CONTROLS_FIXTURE "test_runtime_controls.c"
#endif
#include XZ_RUNTIME_CONTROLS_FIXTURE
#undef main
#undef dlsym
#undef pthread_create
#undef getenv
#undef xz_key_browser_start
#undef xz_key_traffic_start
static int thread_attempts,touches;
static void startup_forward(int x,int y,int down){(void)x;(void)y;(void)down;touches++;}
static void *startup_dlsym(void *scope,const char *name){(void)scope;return !strcmp(name,"xz_vj_touch_v1")?(void *)startup_forward:NULL;}
static int startup_thread(pthread_t *t,const pthread_attr_t *a,void *(*f)(void *),void *ctx){(void)t;(void)a;(void)f;(void)ctx;thread_attempts++;return EAGAIN;}
static char *startup_env(const char *name){(void)name;return NULL;}
static int startup_key_browser(void){return 0;}
static int startup_key_traffic(void){return 0;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}

int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"gesture")){
  unsigned char self[32]={0};started=1;stock_touch=stock;native_play=native_active=1;native_four=-1;
  xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);
  for(int invalid=0;invalid<3;invalid++){
   inline_contact=1;inline_contact_deck=0;inline_cancelled=0;inline_ui.down=1;
   inline_gesture_layout=invalid==0?XZ_STEM_LAYOUT_COUNT:invalid==1?-1:XZ_STEM_LAYOUT_HIDDEN;
   model.stems_overlay=inline_gesture_layout;
   int before=stock_calls;contact(self,200,130,1);
   assert(inline_cancelled&&!inline_ui.down&&stock_calls==before);
   contact(self,0,0,0);assert(!inline_contact&&!inline_cancelled);
  }
  puts("PASS actual touch_hook: invalid/empty captured layout cancels and owns the release without touching stock");return 0;
 }
 _Static_assert(sizeof model.deck/sizeof model.deck[0]==4,"four UI decks");
 _Static_assert(sizeof pads.count/sizeof pads.count[0]==2,"two native pad decks");
 started=0;memset(&pads,0,sizeof pads);memset(&model,0x55,sizeof model);
 assert(xz_ui_runtime_start(0,0,0)==0&&started);
 for(unsigned d=0;d<sizeof pads.count/sizeof pads.count[0];d++)assert(pads.count[d]==3);
 for(unsigned d=0;d<sizeof model.deck/sizeof model.deck[0];d++)assert(model.deck[d].stem_count==3);
 assert(thread_attempts==1&&!settings_created&&!touches);
 puts("PASS actual xz_ui_runtime_start: four UI decks, two pad decks, clean bounds; external threads/hooks/environment are host stubs");
 return 0;
}
