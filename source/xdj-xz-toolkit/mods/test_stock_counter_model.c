/* SPDX-License-Identifier: MIT */
#define main old_model_main
#include "test_shell_model.c"
#undef main
#include "stock_counter.h"
#include "shell/fixtures.h"
#include "shell/shell_browse.h"
static uint32_t counter_ids[2];static int counter_failed,counter_torn,counter_guard_bad;static unsigned counter_reads;
static int counter_read(uintptr_t address,void *out,size_t n){
 static const unsigned char g[16]=XZ_STOCK_COUNTER_GUARD_BYTES;
 if(address==0xe1d80&&n==16){memcpy(out,g,16);if(counter_guard_bad)((unsigned char*)out)[0]^=1;return 0;}
 if(address==0x222c668&&n==8){if(counter_failed)return -1;memcpy(out,counter_ids,8);if(counter_torn&&++counter_reads%2)((unsigned char*)out)[0]^=1;return 0;}
 return read_memory(address,out,n);
}
static uint16_t pixels[800*480];static struct fx_track track;
static void shot(const char *dir,const char *name){
 if(!dir)return;char p[1024];snprintf(p,sizeof p,"%s/%s.ppm",dir,name);FILE *f=fopen(p,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(unsigned i=0;i<800*480;i++){uint16_t v=pixels[i];unsigned char rgb[3]={(unsigned char)((v>>11)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}fclose(f);
}
int main(int argc,char **argv){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
 const char *codes[]={"E-7206","E-8302","E-8304","E-8305","E-8306"};
 for(unsigned d=0;d<2;d++)for(uint32_t id=101;id<=105;id++){
  counter_ids[d]=id;xz_shell_model_refresh(&m,s.storage,counter_read,1000);
  assert(m.deck[d].known&K(ALERT));assert(!strcmp(m.deck[d].alert.code,codes[id-101]));
  assert(!(m.deck[1-d].known&K(ALERT)));counter_ids[d]=0;
 }
 counter_ids[0]=103;counter_ids[1]=102;xz_shell_model_refresh(&m,s.storage,counter_read,1001);
 assert((m.deck[0].known&K(ALERT))&&(m.deck[1].known&K(ALERT)));
 struct xz_shell_model preview;fx_track_build(&track,12);fx_model_full(&preview,&track);preview.capabilities|=XZ_SHELL_CAP_BROWSE;
 for(unsigned d=0;d<2;d++){preview.deck[d].alert=m.deck[d].alert;preview.deck[d].known|=K(ALERT);}
 struct xz_shell_browse_model browse={.screen=XZ_SHELL_BROWSE};preview.browse=&browse;
 struct xz_shell u;xz_shell_init(&u);assert(xz_shell_render(&u,&preview,pixels,800));shot(argc>1?argv[1]:NULL,"modern-counter-E8304");
 struct xz_shell_rect b=xz_shell_play_rect(&preview,XZ_SHELL_R_ALERT_BUTTON,0);struct xz_shell_action out[XZ_SHELL_ACTIONS];
 struct xz_shell_event e={XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,b.y+b.h/2,0,0,-1};xz_shell_input(&u,&preview,&e,out);
 e.kind=XZ_SHELL_TOUCH_UP;xz_shell_input(&u,&preview,&e,out);assert(u.deck==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 counter_ids[0]=7777;counter_ids[1]=125;xz_shell_model_refresh(&m,s.storage,counter_read,1002);
 assert(!(m.deck[0].known&K(ALERT))&&!(m.deck[1].known&K(ALERT)));
 counter_ids[0]=103;counter_failed=1;xz_shell_model_refresh(&m,s.storage,counter_read,1003);assert(!(m.deck[0].known&K(ALERT)));counter_failed=0;
 counter_guard_bad=1;xz_shell_model_refresh(&m,s.storage,counter_read,1004);assert(!(m.deck[0].known&K(ALERT)));counter_guard_bad=0;
 counter_torn=1;xz_shell_model_refresh(&m,s.storage,counter_read,1005);assert(!(m.deck[0].known&K(ALERT)));counter_torn=0;
 counter_ids[0]=0;xz_shell_model_refresh(&m,s.storage,counter_read,1006);assert(!(m.deck[0].known&K(ALERT)));
 release(&s);puts("PASS actual model both decks exact errors, clear/failure/unknown, real alert renderer and CHOOSE TRACK");return 0;
}
