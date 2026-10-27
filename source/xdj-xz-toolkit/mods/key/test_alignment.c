/* SPDX-License-Identifier: MPL-2.0
 * The scalar oracle retains the alignment from sealed f72d37e. */
#ifdef NDEBUG
#error Alignment acceptance requires active assertions
#endif
#include "keyshift.h"
#include <assert.h>
#include <stdio.h>
static float compare_align(const struct xz_key_shift *,int64_t,int64_t);
#define XZ_KEY_SHIFT_ALIGN compare_align
#include "keyshift.c"
static float mono(const struct xz_key_shift *state, int64_t position)
{
    uint32_t index = (uint32_t)position & XZ_KEY_MASK;
    return state->history[index * 2u] + state->history[index * 2u + 1u];
}

static float reference_align(const struct xz_key_shift *state, int64_t outgoing,
                        int64_t incoming)
{
    int64_t best_lag = 0;
    float best_score = -1.0e30f;
    unsigned lag;
    for (lag = 0; lag < XZ_KEY_ALIGN_MAX; lag += XZ_KEY_ALIGN_STRIDE) {
        float score = 0.0f;
        unsigned i;
        for (i = 0; i < XZ_KEY_ALIGN_WINDOW; i += XZ_KEY_ALIGN_STRIDE)
            score += mono(state, outgoing + (int64_t)i) *
                     mono(state, incoming - (int64_t)lag + (int64_t)i);
        if (score > best_score) {
            best_score = score;
            best_lag = (int64_t)lag;
        }
    }
    if (best_score <= 0.0f) return 0.0f;
    {
        int64_t low = best_lag - (int64_t)XZ_KEY_ALIGN_STRIDE;
        int64_t high = best_lag + (int64_t)XZ_KEY_ALIGN_STRIDE;
        int64_t fine_lag = best_lag;
        float fine_score = -1.0e30f;
        int64_t candidate;
        if (low < 0) low = 0;
        if (high > (int64_t)XZ_KEY_ALIGN_MAX) high = XZ_KEY_ALIGN_MAX;
        for (candidate = low; candidate <= high; ++candidate) {
            float score = 0.0f;
            unsigned i;
            for (i = 0; i < XZ_KEY_ALIGN_WINDOW; ++i)
                score += mono(state, outgoing + (int64_t)i) *
                         mono(state, incoming - candidate + (int64_t)i);
            if (score > fine_score) {
                fine_score = score;
                fine_lag = candidate;
            }
        }
        return (float)fine_lag;
    }
}


static unsigned wraps;
static float compare_align(const struct xz_key_shift *s,int64_t out,int64_t in) {
 float a=align_head(s,out,in),b=reference_align(s,out,in);
 assert(a==b);++wraps;return a;
}
static uint32_t noise=1;
static float audio(unsigned n,int kind,unsigned c){
 double t=n/44100.,beat=fmod(t,.5),env=exp(-beat*40),x=0;
 noise=noise*1664525u+1013904223u;double r=(int32_t)noise/2147483648.;
 if(kind==0||kind==4)x+=.45*env*sin(6.28318530718*(55*beat+1.5*(1-exp(-beat*100))))+.12*r*exp(-fmod(t,.125)*180);
 if(kind==1||kind==4)for(int h=1;h<=8;h++)x+=.19/h*sin(6.28318530718*73.416*h*t)*(1-exp(-beat*120));
 if(kind==2||kind==4){double notes[]={220,277.183,329.628};for(int j=0;j<3;j++)for(int h=1;h<=4;h++)x+=.07/h*sin(6.28318530718*(notes[j]+c*.7)*h*t);}
 if(kind==3||kind==4)for(int h=1;h<=18;h++)x+=.018*(1+sin(6.28318530718*3*t))*sin(6.28318530718*(145*h*t+sin(6.28318530718*5*t)*.04*h));
 if(kind==5)x=n%11025<4?.5:0;
 return (float)(x/(kind==4?2:1));
}

int main(void) {
 static struct xz_key_shift state;
 int cents[]={-1200,-600,-100,-137,100,123,600,1200};
 for(int kind=0;kind<6;kind++)for(unsigned c=0;c<8;c++) {
  xz_key_shift_init(&state);noise=1;
  assert(!xz_key_shift_set_cents(&state,cents[c]));
  unsigned first=wraps;
  for(unsigned n=0;n<44100*4;n+=256) {
   float pcm[512];unsigned frames=44100*4-n;if(frames>256)frames=256;
   for(unsigned i=0;i<frames;i++)for(unsigned ch=0;ch<2;ch++)pcm[2*i+ch]=audio(n+i,kind,ch);
   assert(!xz_key_shift_process(&state,pcm,frames));
   for(unsigned i=0;i<frames*2;i++)assert(isfinite(pcm[i])&&fabsf(pcm[i])<2);
  }
  assert(wraps>first);
  printf("PASS material=%d cents=%d wraps=%u exact reference offsets\n",kind,cents[c],wraps-first);
 }
 /* Ring wrap, long-running lifetime counter, negative correlations and ties. */
 for(unsigned trial=0;trial<1000;trial++) {
  xz_key_shift_init(&state);state.written=(UINT64_C(1)<<40)+trial*127;
  for(unsigned i=0;i<4096;i++) {
   noise=noise*1664525u+1013904223u;float x=(int32_t)noise/2147483648.f;
   state.history[i*2]=x;state.history[i*2+1]=trial%7?x:-x;
   state.mono_history[i]=state.mono_history[i+4096]=state.history[i*2]+state.history[i*2+1];
  }
  compare_align(&state,(int64_t)state.written-240,(int64_t)state.written-513);
 }
 printf("PASS %u exact full-search offsets; drums/bass/chords/vocals/mix/impulses and ring boundaries\n",wraps);
 return 0;
}
