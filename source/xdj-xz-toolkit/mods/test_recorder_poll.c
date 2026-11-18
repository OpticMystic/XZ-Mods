/* SPDX-License-Identifier: MIT */
/* The shared recorder poller (recorder_poll.h): the seam is read at most every XZ_REC_POLL_MS whoever asks,
   and two callers in different clock domains see the same session time at the same instant. */
#ifdef NDEBUG
#error Recorder poll acceptance requires assertions
#endif
#include "recorder_poll.h"
#include "recorder_state.h"
#include <assert.h>
#include <stdio.h>

static int reads;
static struct xz_recorder_state seam;
static int fake_read(struct xz_recorder_state *out) { reads++;*out=seam;return seam.known?0:-1; }

int main(void) {
 struct xz_recorder_poller p={0};
 /* Idle, then REC pressed: the first recording read gives the file's elapsed. */
 seam=(struct xz_recorder_state){1,0,0,0,{0}};
 assert(!xz_recorder_poll_at(&p,fake_read,1000,1000).recording&&reads==1);
 seam=(struct xz_recorder_state){1,1,120,1,{0}};
 /* Asked 60 times in 250 ms by two runtimes: one more seam read. */
 for(uint32_t t=1000;t<1250;t+=4){xz_recorder_poll_at(&p,fake_read,t,t);xz_recorder_poll_at(&p,fake_read,t,t+777777u);}
 assert(reads==1);
 struct xz_rec_reading a=xz_recorder_poll_at(&p,fake_read,1250,1250);assert(reads==2&&a.recording&&a.elapsed_ms==120);
 /* The shell's wall clock and the panel's monotonic clock: different numbers, one instant, one session time. */
 uint32_t mono=1400,wall=4000000000u,panel=1400;
 struct xz_rec_reading s=xz_recorder_poll_at(&p,fake_read,mono,wall),m=xz_recorder_poll_at(&p,fake_read,mono,panel);
 assert(reads==2);
 struct xz_rec_view vs=xz_rec_view_at(&s,wall),vm=xz_rec_view_at(&m,panel);
 assert(vs.elapsed_ms==vm.elapsed_ms&&vs.elapsed_ms==120+150);
 /* An unknown seam: nothing for the renderers once the hold runs out. */
 seam=(struct xz_recorder_state){0,0,0,0,{0}};
 struct xz_rec_reading u=xz_recorder_poll_at(&p,fake_read,1500,1500);assert(u.recording);       /* held */
 u=xz_recorder_poll_at(&p,fake_read,1250+XZ_REC_HOLD_MS,1250+XZ_REC_HOLD_MS);assert(!u.known);
 /* No reader, no poller. */
 assert(!xz_recorder_poll_at(NULL,fake_read,0,0).known&&!xz_recorder_poll_at(&p,NULL,0,0).known);
 puts("PASS recorder poll: one seam read per 250 ms for every caller, one session time across clock domains");
 return 0;
}
