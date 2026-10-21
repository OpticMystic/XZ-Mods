// SPDX-License-Identifier: GPL-2.0-only
#include "../usb_transport.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <sched.h>
#include <stdio.h>

static struct {
    struct urb *pending[128];
    unsigned read, write, calls, fail_at, opened, closed, claimed, alt_one, alt_zero, released;
    unsigned wrong_device, busy, preexisting_alt, malformed_input, disconnect, freeze, verify_pcm;
    unsigned input_packet, bad_order, verify_generation;
    uint64_t time, observed[2];
} f;
static atomic_uint hang_reap, entered_reap, producer_done;
static int fake_open(const char *p) { (void)p; ++f.opened; return 17; }
static int fake_close(int fd) { assert(fd == 17); ++f.closed; return 0; }
static int fake_wait(int fd, int ms) { assert(fd == 17); f.time += (unsigned)ms * 1000; return 0; }
static uint64_t fake_now(void) { return f.time; }
static int32_t unpack(const uint8_t *p) {
    uint32_t value = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    return (int32_t)(value ^ 0x800000) - 0x800000;
}
static int fake_request(int fd, unsigned long request, void *arg) {
    assert(fd == 17);
    if (++f.calls == f.fail_at) { errno = EIO; return -1; }
    if (request == CONTROL) {
        struct control *c = arg;
        if (c->type == 0x80 && c->request == 6) {
            assert(c->length == 18 && !f.claimed);
            uint8_t bytes[18] = {18, 1}; bytes[8] = 0x73; bytes[9] = 0x2b; bytes[10] = f.wrong_device ? 0x2c : 0x2d;
            memcpy(c->data, bytes, 18); return 18;
        }
        if (c->type == 0x81 && c->request == 10) { assert(f.claimed); *(uint8_t *)c->data = (uint8_t)f.preexisting_alt; return 1; }
        assert(c->type == 0x22 && c->request == 1 && c->value == 0x100 && c->index == 0x82 && c->length == 3);
        assert(f.claimed && f.alt_one && !memcmp(c->data, "\x44\xac\0", 3)); return 3;
    }
    if (request == CLAIM) {
        if (f.busy) { errno = EBUSY; return -1; }
        ++f.claimed; return 0;
    }
    if (request == SET_INTERFACE) {
        struct interface *s = arg; assert(s->number == 0 && f.claimed);
        if (s->alternate) ++f.alt_one; else ++f.alt_zero;
        return 0;
    }
    if (request == RELEASE) { assert(f.claimed); ++f.released; return 0; }
    if (request == DISCARD) { ((struct urb *)arg)->status = -ECONNRESET; return 0; }
    if (request == SUBMIT) {
        struct urb *u = arg;
        assert(f.write - f.read < 128);
        assert(u->packets == PACKETS && u->type == 0 && u->flags == 2 && !u->status);
        if (u->endpoint == 1 && (f.verify_pcm || f.verify_generation)) {
            const uint8_t *data = u->buffer;
            for (unsigned frame = 0; frame < (unsigned)u->buffer_length / OUT_BYTES; ++frame) {
                for (unsigned ch = 0; ch < 12; ++ch)
                    if (ch < 4 || ch > 7) assert(unpack(data + frame * OUT_BYTES + ch * 3) == 0);
                for (unsigned d = 0; d < 2; ++d) {
                    int32_t value = unpack(data + frame * OUT_BYTES + (4 + 2*d) * 3);
                    assert(value == unpack(data + frame * OUT_BYTES + (5 + 2*d) * 3));
                    if (value) {
                        if (f.verify_generation) assert(value == (int32_t)((struct slot *)u->context)->deck[d].generation);
                        else assert(value == (d ? -(int32_t)++f.observed[d] : (int32_t)++f.observed[d]));
                    }
                }
            }
        }
        f.pending[f.write++ % 128] = u; return 0;
    }
    assert(request == REAP);
    if (load32(&hang_reap)) {
        store32(&entered_reap, 1);
        while (load32(&hang_reap)) { struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL); }
    }
    if (f.disconnect) { errno = ENODEV; return -1; }
    if (f.freeze || f.read == f.write) { errno = EAGAIN; return -1; }
    struct urb *u = f.pending[f.read++ % 128];
    if (f.bad_order && u->endpoint == 1) ((struct slot *)u->context)->sequence += 1;
    struct iso *iso = (struct iso *)(u + 1);
    if (u->endpoint == 0x82 && !u->status) for (unsigned i = 0; i < PACKETS; ++i) {
        iso[i].actual_length = (f.input_packet++ % 40 ? 11 : 12) * IN_BYTES;
        if (f.malformed_input) iso[i].actual_length += 1;
    }
    *(void **)arg = u;
    f.time += 100;
    return 0;
}
static const struct io_ops fake_io = {fake_open, fake_request, fake_wait, fake_close, fake_now};
static xz_usb_transport *fixture(void) {
    memset(&f, 0, sizeof(f)); f.time = 1000000;
    xz_usb_transport *t = allocate_transport(&fake_io); assert(t);
    return t;
}
static void dispose(xz_usb_transport *t) {
    restore(t); pthread_mutex_destroy(&t->snapshot_lock); free(t);
}
static void step(xz_usb_transport *t, unsigned n) { for (unsigned i = 0; i < n; ++i) assert(service(t)); }
static void constant(float *pcm, unsigned frames, float value) { for (unsigned i = 0; i < frames * 2; ++i) pcm[i] = value; }
static void test_packing_and_backpressure(void) {
    xz_usb_transport *t = fixture(); store32(&t->state, XZ_USB_RUNNING);
    float pcm[512]; constant(pcm, 256, .5f);
    pcm[0] = -2; pcm[1] = 2;
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, 900) == XZ_USB_OK);
    assert(unpack(t->queue[0].block[0].pcm) == -8388608);
    assert(unpack(t->queue[0].block[0].pcm + 3) == 8388607);
    pcm[0] = NAN;
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, 901) == XZ_USB_INVALID);
    pcm[0] = .5f;
    for (unsigned n = 1; n < BLOCKS; ++n) assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, n) == XZ_USB_OK);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, 0) == XZ_USB_AGAIN);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_4, 1, pcm, 256, 42) == XZ_USB_OK);
    assert(xz_usb_transport_submit(t, (xz_usb_deck)1, 1, pcm, 256, 0) == XZ_USB_INVALID);
    uint32_t generation;
    store32(&t->queue[0].producer, 1);
    assert(xz_usb_transport_flush(t, XZ_USB_DECK_3, &generation) == XZ_USB_BUSY);
    store32(&t->queue[0].producer, 0);
    dispose(t);
}
static void test_flush_and_future_generation(void) {
    xz_usb_transport *t = fixture(); store32(&t->state, XZ_USB_RUNNING);
    float pcm[512]; constant(pcm, 256, .5f);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, 1000) == XZ_USB_OK);
    struct slot *s = &t->output[0];
    prepare(t, s, 0, 1);
    assert(t->queue[0].offset > 0 && t->queue[0].offset < 256);
    uint32_t generation;
    assert(xz_usb_transport_flush(t, XZ_USB_DECK_3, &generation) == XZ_USB_OK && generation == 2);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, 1000) == XZ_USB_STALE);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 2, pcm, 1, 2000) == XZ_USB_OK);
    assert(xz_usb_transport_flush(t, XZ_USB_DECK_3, &generation) == XZ_USB_OK && generation == 3);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 3, pcm, 1, 3000) == XZ_USB_OK);
    uint8_t wire[OUT_BYTES * 12] = {0};
    struct contribution old = {2, 0, 0, 0};
    take_frames(t, 0, &old, wire, 12);
    assert(old.frames == 1 && old.source_after == 2000);
    assert(load32(&t->queue[0].read) == 2); /* future block remains queued */
    prepare(t, s, 0, 1);
    assert(s->deck[0].generation == 3 && s->deck[0].frames == 1 && s->deck[0].source_after == 3000);
    assert(load32(&t->queue[0].read) == 3 && t->queue[0].offset == 0);
    dispose(t);
}
static void test_cursor_wrap(void) {
    xz_usb_transport *t = fixture(); store32(&t->state, XZ_USB_RUNNING);
    store32(&t->queue[0].read, UINT32_MAX); store32(&t->queue[0].write, UINT32_MAX);
    float pcm[2] = {.25f, -.25f};
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 1, UINT64_MAX) == XZ_USB_OK);
    prepare(t, &t->output[0], 0, 1);
    assert(load32(&t->queue[0].read) == 0 && load32(&t->queue[0].write) == 0);
    assert(t->output[0].deck[0].source_after == UINT64_MAX);
    dispose(t);
}
static void test_setup_rollback(void) {
    for (unsigned failure = 1; failure <= 37; ++failure) {
        xz_usb_transport *t = fixture(); f.fail_at = failure;
        assert(setup(t, "fake") != XZ_USB_OK);
        restore(t);
        assert(f.opened == 1 && f.closed == 1);
        if (f.claimed) assert(f.released == 1);
        if (failure >= 4) assert(f.alt_zero == (failure >= 4));
        assert(f.read == f.write);
        dispose(t);
    }
    xz_usb_transport *t = fixture(); f.preexisting_alt = 1;
    assert(setup(t, "fake") == XZ_USB_BUSY); restore(t);
    assert(!f.alt_one && !f.alt_zero && f.released == 1 && f.closed == 1); dispose(t);
    t = fixture(); f.busy = 1;
    assert(setup(t, "fake") == XZ_USB_BUSY); restore(t);
    assert(!f.released && !f.alt_zero && f.closed == 1); dispose(t);
    t = fixture(); f.wrong_device = 1;
    assert(setup(t, "fake") == XZ_USB_WRONG_DEVICE); restore(t);
    assert(!f.claimed && f.closed == 1); dispose(t);
}
static void test_clock_position_and_failure(void) {
    xz_usb_transport *t = fixture(); assert(setup(t, "fake") == XZ_USB_OK);
    assert(f.write == 32 && f.claimed && f.alt_one);
    step(t, 32); assert(load32(&t->state) == XZ_USB_RUNNING);
    assert(t->live.deck[0].completed_frames == 0 && t->live.wire_completed_frames > 0);
    float pcm[512]; constant(pcm, 256, .25f);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 1, pcm, 256, 48000) == XZ_USB_OK);
    step(t, 8); assert(t->live.deck[0].staged_frames == 256 && t->live.deck[0].completed_frames == 0);
    assert(t->live.deck[0].source_after_staged == 48000 && t->live.deck[0].source_valid == 1);
    uint64_t boundary = t->live.wire_submitted_frames; uint32_t generation;
    assert(xz_usb_transport_flush(t, XZ_USB_DECK_3, &generation) == XZ_USB_OK);
    step(t, 2);
    assert(t->live.deck[0].applied_generation == 2 && t->live.deck[0].prior_audio_through_wire_frame == boundary);
    step(t, 64); assert(t->live.deck[0].completed_frames == 0 && t->live.deck[0].source_valid == 0);
    assert(t->live.wire_completed_frames >= boundary && t->live.feedback_replaced == 0);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_3, 2, pcm, 256, 96000) == XZ_USB_OK);
    step(t, 80);
    assert(t->live.deck[0].completed_frames == 256 && t->live.deck[0].source_after_completed == 96000);
    publish(t, 0); xz_usb_position position;
    assert(xz_usb_transport_position(t, &position) == XZ_USB_OK && position.deck[0].completed_frames == 256);
    pthread_mutex_lock(&t->snapshot_lock);
    assert(xz_usb_transport_position(t, &position) == XZ_USB_AGAIN && position.state == XZ_USB_RUNNING);
    publish(t, 0); /* writer also never waits for this lock */
    pthread_mutex_unlock(&t->snapshot_lock);
    f.disconnect = 1; assert(!service(t) && load32(&t->state) == XZ_USB_FAULTED);
    assert(xz_usb_transport_submit(t, XZ_USB_DECK_4, 1, pcm, 256, 0) == XZ_USB_STOPPED);
    dispose(t); assert(f.alt_zero == 1 && f.released == 1 && f.closed == 1);
    t = fixture(); assert(setup(t, "fake") == XZ_USB_OK); f.malformed_input = 1;
    assert(!service(t) && load32(&t->fault) == XZ_USB_IO); dispose(t);
    t = fixture(); assert(setup(t, "fake") == XZ_USB_OK); f.freeze = 1;
    for (unsigned i = 0; i < 21; ++i) assert(service(t));
    assert(!service(t) && load32(&t->fault) == XZ_USB_TIMEOUT); dispose(t);
    t = fixture(); assert(setup(t, "fake") == XZ_USB_OK); f.bad_order = 1;
    assert(service(t)); assert(!service(t)); dispose(t);
}
static void test_threaded_stream(void) {
    memset(&f, 0, sizeof(f)); f.time = 1000000; f.verify_pcm = 1;
    xz_usb_transport *t = NULL;
    assert(open_with_io("fake", &t, &fake_io) == XZ_USB_OK && t);
    unsigned sent[2] = {0}; const unsigned total = 1000 * XZ_USB_BLOCK_FRAMES;
    uint64_t deadline = monotonic_us() + 10000000;
    while ((sent[0] < total || sent[1] < total) && monotonic_us() < deadline) {
        for (unsigned d = 0; d < 2; ++d) if (sent[d] < total) {
            float pcm[512];
            for (unsigned n = 0; n < 256; ++n) pcm[2*n] = pcm[2*n+1] = (d ? -1.0f : 1.0f) * (float)(sent[d] + n + 1) / 8388608.0f;
            xz_usb_result result = xz_usb_transport_submit(t, d ? XZ_USB_DECK_4 : XZ_USB_DECK_3, 1, pcm, 256, sent[d] + 256);
            if (result == XZ_USB_OK) sent[d] += 256;
            else assert(result == XZ_USB_AGAIN);
        }
        sched_yield();
    }
    assert(sent[0] == total && sent[1] == total);
    xz_usb_position p;
    do {
        xz_usb_result result = xz_usb_transport_position(t, &p);
        if (result == XZ_USB_OK && p.deck[0].completed_frames == total && p.deck[1].completed_frames == total) break;
        assert(p.state == XZ_USB_RUNNING || p.state == XZ_USB_STARTING);
        sched_yield();
    } while (monotonic_us() < deadline);
    assert(p.deck[0].completed_frames == total && p.deck[1].completed_frames == total);
    assert(xz_usb_transport_close(&t) == XZ_USB_OK && !t);
    assert(f.observed[0] == total && f.observed[1] == total && f.closed == 1 && f.alt_zero == 1 && f.released == 1);
    assert(xz_usb_transport_close(&t) == XZ_USB_OK);
}
static void *racing_producer(void *arg) {
    xz_usb_transport *t = arg;
    while (!load32(&producer_done)) {
        unsigned generation = load32(&t->queue[0].generation);
        float pcm[512]; constant(pcm, 256, (float)generation / 8388608.0f);
        xz_usb_result result = xz_usb_transport_submit(t, XZ_USB_DECK_3, generation, pcm, 256, generation);
        assert(result == XZ_USB_OK || result == XZ_USB_AGAIN || result == XZ_USB_BUSY || result == XZ_USB_STALE);
        sched_yield();
    }
    return NULL;
}
static void test_concurrent_seek(void) {
    memset(&f, 0, sizeof(f)); f.time = 1000000; f.verify_generation = 1;
    xz_usb_transport *t = NULL;
    assert(open_with_io("fake", &t, &fake_io) == XZ_USB_OK);
    store32(&producer_done, 0); pthread_t producer;
    assert(!pthread_create(&producer, NULL, racing_producer, t));
    uint32_t generation = 1;
    for (unsigned i = 0; i < 2000; ) {
        xz_usb_result result = xz_usb_transport_flush(t, XZ_USB_DECK_3, &generation);
        if (result == XZ_USB_OK) ++i;
        else assert(result == XZ_USB_BUSY);
        sched_yield();
    }
    assert(generation == 2001);
    store32(&producer_done, 1); assert(!pthread_join(producer, NULL));
    assert(xz_usb_transport_close(&t) == XZ_USB_OK && !t);
}
static void test_close_timeout_retains_worker(void) {
    memset(&f, 0, sizeof(f)); f.time = 1000000;
    store32(&hang_reap, 1); store32(&entered_reap, 0);
    xz_usb_transport *t = NULL;
    assert(open_with_io("fake", &t, &fake_io) == XZ_USB_OK);
    uint64_t deadline = monotonic_us() + 3000000;
    while (!load32(&entered_reap) && monotonic_us() < deadline) sched_yield();
    assert(load32(&entered_reap));
    assert(xz_usb_transport_close(&t) == XZ_USB_TIMEOUT && t);
    assert(!f.closed && !f.alt_zero && !f.released);
    store32(&hang_reap, 0);
    assert(xz_usb_transport_close(&t) == XZ_USB_OK && !t);
    assert(f.closed == 1 && f.alt_zero == 1 && f.released == 1);
}
static void test_atomic_verify_slot(const struct slot *s,unsigned d,int32_t value) {
 unsigned frames=(unsigned)s->u.buffer_length/OUT_BYTES;
 for(unsigned i=0;i<frames;i++)for(unsigned ch=0;ch<12;ch++) {
  int32_t got=unpack(s->data+i*OUT_BYTES+ch*3);
  if(ch==4+2*d||ch==5+2*d)assert(got==value);
  else if(ch<4||ch>7)assert(!got);
 }
}

void test_atomic_packing_and_first_block(void) {
 xz_usb_transport *t=fixture();store32(&t->state,XZ_USB_RUNNING);
 float pcm[512];constant(pcm,256,.25f);uint32_t gen=999;
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,1234,&gen)==XZ_USB_OK&&gen==2);
 assert(load32(&t->queue[0].generation)==2&&load32(&t->queue[0].write)==1);
 struct slot *s=&t->output[0];prepare(t,s,0,1);assert(s->deck[0].generation==2&&s->deck[0].frames==(unsigned)s->u.buffer_length/OUT_BYTES);test_atomic_verify_slot(s,0,2097152);
 assert(load32(&t->queue[1].generation)==1&&load32(&t->queue[1].write)==0);
 dispose(t);
}
void test_atomic_failures_and_full(void) {
 xz_usb_transport *t=fixture();float pcm[512];constant(pcm,256,.25f);uint32_t out=777;
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,256,&out)==XZ_USB_AGAIN&&out==777);
 store32(&t->state,XZ_USB_RUNNING);store32(&t->queue[0].producer,1);
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,256,&out)==XZ_USB_BUSY&&out==777);
 store32(&t->queue[0].producer,0);
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,2,pcm,256,256,&out)==XZ_USB_STALE&&out==777);
 pcm[8]=NAN;assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,256,&out)==XZ_USB_INVALID&&out==777);pcm[8]=.25f;
 assert(load32(&t->queue[0].generation)==1&&load32(&t->queue[0].write)==0);
 dispose(t);
}
void test_atomic_full_queue_retry(void) {
 xz_usb_transport *t=fixture();store32(&t->state,XZ_USB_RUNNING);float pcm[512];constant(pcm,256,.25f);uint32_t out=777;
 for(unsigned i=0;i<BLOCKS;i++)assert(xz_usb_transport_submit(t,XZ_USB_DECK_3,1,pcm,256,256)==XZ_USB_OK);
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,9999,&out)==XZ_USB_AGAIN&&out==777);
 assert(load32(&t->queue[0].generation)==1&&load32(&t->queue[0].write)==32);
 for(unsigned i=0;i<3;i++)prepare(t,&t->output[0],0,1);
 constant(pcm,256,.5f);assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,9999,&out)==XZ_USB_OK&&out==2);
 prepare(t,&t->output[0],0,1);test_atomic_verify_slot(&t->output[0],0,4194304);assert(t->output[0].deck[0].frames==(unsigned)t->output[0].u.buffer_length/OUT_BYTES);
 dispose(t);
}
void test_atomic_wrap_cancel_and_loss(void) {
 xz_usb_transport *t=fixture();store32(&t->state,XZ_USB_RUNNING);float pcm[512];constant(pcm,256,.25f);uint32_t gen=0;
 store32(&t->queue[0].read,UINT32_MAX);store32(&t->queue[0].write,UINT32_MAX);
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,256,&gen)==XZ_USB_OK&&gen==2&&load32(&t->queue[0].write)==0);
 assert(xz_usb_transport_flush(t,XZ_USB_DECK_3,&gen)==XZ_USB_OK&&gen==3);
 constant(pcm,256,.5f);assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,3,pcm,256,512,&gen)==XZ_USB_OK&&gen==4);
 prepare(t,&t->output[0],0,1);test_atomic_verify_slot(&t->output[0],0,4194304);
 store32(&t->state,XZ_USB_FAULTED);unsigned write=load32(&t->queue[0].write);uint32_t out=777;
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,4,pcm,256,512,&out)==XZ_USB_STOPPED&&out==777&&load32(&t->queue[0].write)==write);
 store32(&t->state,XZ_USB_RUNNING);store32(&t->queue[0].generation,UINT32_MAX);
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,UINT32_MAX,pcm,256,512,&out)==XZ_USB_INVALID&&out==777);
 dispose(t);
}
void test_atomic_old_generation_in_progress(void) {
 xz_usb_transport *t=fixture();store32(&t->state,XZ_USB_RUNNING);float pcm[512];constant(pcm,256,.25f);uint32_t gen;
 assert(xz_usb_transport_submit(t,XZ_USB_DECK_3,1,pcm,256,256)==XZ_USB_OK);
 struct contribution old={1,0,0,0};uint8_t wire[OUT_BYTES*512]={0};take_frames(t,0,&old,wire,11);assert(old.frames==11);
 constant(pcm,256,.5f);assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,1000,&gen)==XZ_USB_OK);
 take_frames(t,0,&old,wire+11*OUT_BYTES,300);assert(old.frames==256&&load32(&t->queue[0].read)==1);
 assert(t->queue[0].block[1].generation==2&&t->queue[0].offset==0);
 for(unsigned i=0;i<256;i++)assert(unpack(wire+i*OUT_BYTES+12)==2097152);
 prepare(t,&t->output[0],0,1);test_atomic_verify_slot(&t->output[0],0,4194304);assert(t->live.deck[0].applied_generation==2);
 dispose(t);
}
void test_atomic_completed_prefix_and_other_deck(void) {
 xz_usb_transport *t=fixture();assert(setup(t,"fake")==XZ_USB_OK);step(t,32);
 float pcm[512];constant(pcm,256,.25f);
 assert(xz_usb_transport_submit(t,XZ_USB_DECK_3,1,pcm,256,1000)==XZ_USB_OK);
 assert(xz_usb_transport_submit(t,XZ_USB_DECK_4,1,pcm,256,2000)==XZ_USB_OK);step(t,8);
 uint64_t boundary=t->live.wire_submitted_frames;uint32_t gen=0;constant(pcm,256,.5f);
 assert(xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,1,pcm,256,9999,&gen)==XZ_USB_OK&&gen==2);
 step(t,2);assert(t->live.deck[0].applied_generation==2&&t->live.deck[0].completed_frames==0);
 assert(t->live.deck[0].prior_audio_through_wire_frame==boundary);
 step(t,80);assert(t->live.deck[0].staged_frames==256&&t->live.deck[0].completed_frames==256);
 assert(t->live.deck[0].source_after_completed==9999&&t->live.deck[0].source_valid==3);
 assert(t->live.deck[1].applied_generation==1&&t->live.deck[1].completed_frames==256&&t->live.deck[1].source_after_completed==2000);
 assert(t->live.deck[0].underrun_frames>0); /* Producer stopped: silence counters remain honest. */
 dispose(t);
}
void test_atomic_concurrent_transitions(void) {
 memset(&f,0,sizeof f);f.time=1000000;f.verify_generation=1;xz_usb_transport *t=NULL;
 assert(open_with_io("fake",&t,&fake_io)==XZ_USB_OK);
 store32(&producer_done,0);pthread_t producer;assert(!pthread_create(&producer,NULL,racing_producer,t));
 uint32_t generation=1;unsigned again=0,busy=0;
 for(unsigned i=0;i<2000;) {
  float pcm[512];constant(pcm,256,(float)(generation+1)/8388608.0f);uint32_t next=777;
  xz_usb_result r=xz_usb_transport_flush_and_submit(t,XZ_USB_DECK_3,generation,pcm,256,generation+1,&next);
  if(r==XZ_USB_OK){generation=next;i++;}else{assert(next==777);assert(r==XZ_USB_AGAIN||r==XZ_USB_BUSY);if(r==XZ_USB_AGAIN)again++;else busy++;sched_yield();}
  xz_usb_position pos;if(xz_usb_transport_position(t,&pos)==XZ_USB_OK){assert(pos.deck[0].completed_frames<=pos.deck[0].staged_frames);assert(pos.deck[0].staged_frames<=pos.wire_submitted_frames);}
 }
 store32(&producer_done,1);assert(!pthread_join(producer,NULL));assert(xz_usb_transport_close(&t)==XZ_USB_OK&&!t);
 printf("{\"case\":\"concurrent_2000_atomic_transitions\",\"generation\":%u,\"again\":%u,\"busy\":%u}\n",generation,again,busy);
}

int main(void) {
    atomic_init(&hang_reap, 0); atomic_init(&entered_reap, 0); atomic_init(&producer_done, 0);
    test_packing_and_backpressure(); test_flush_and_future_generation(); test_cursor_wrap();
    test_setup_rollback(); test_clock_position_and_failure(); test_threaded_stream();
    test_concurrent_seek(); test_close_timeout_retains_worker();
    xz_usb_transport *t = NULL;
    assert(xz_usb_transport_open("/tmp/not-a-ram-node", &t) == XZ_USB_INVALID && !t);
    test_atomic_packing_and_first_block();test_atomic_failures_and_full();test_atomic_full_queue_retry();
    test_atomic_wrap_cancel_and_loss();test_atomic_old_generation_in_progress();test_atomic_completed_prefix_and_other_deck();test_atomic_concurrent_transitions();
    puts("PASS packing, isolation, queue pressure, generations, wrap, setup rollback, feedback, position, failures, 512000-frame stream, 2000 concurrent seeks, close timeout/retry");
    return 0;
}
