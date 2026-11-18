/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* xz_wake: no lost wake-up under the mutex protocol, a stale ticket never sleeps, a timeout is honoured.
 * A lost wake-up shows as one round trip that lasts as long as the wait limit (5 s); the test fails on any round
 * trip over 4 s. Run natively, under ThreadSanitizer, and as the ARM build under qemu. */
#define _GNU_SOURCE
#include "../xz_wake.h"
#include "../clock_compat.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now_us(void) {
    struct timespec t = {0, 0}; xz_clock_gettime_compat(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

/* Ping-pong: the producer posts one job under the mutex and sets the worker's wake; the worker takes it under the
 * mutex and sets the producer's wake. Each side waits only through xz_wake_wait_locked. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_wake to_worker, to_producer;
static unsigned posted, taken, quit;
enum { ROUNDS = 100000, LIMIT_MS = 5000 };

static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&mutex);
    for (;;) {
        while (posted == taken && !quit) xz_wake_wait_locked(&to_worker, &mutex, LIMIT_MS);
        if (posted == taken && quit) break;
        taken++;
        /* Half the time the wake goes out under the mutex, half after it, as the converted sites do. */
        if (taken & 1) xz_wake_set(&to_producer);
        else { pthread_mutex_unlock(&mutex); xz_wake_set(&to_producer); pthread_mutex_lock(&mutex); }
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

static void ping_pong(void) {
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    uint64_t worst = 0;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        uint64_t began = now_us();
        pthread_mutex_lock(&mutex);
        posted++;
        if (round & 2) xz_wake_set(&to_worker);
        pthread_mutex_unlock(&mutex);
        if (!(round & 2)) xz_wake_set(&to_worker);
        pthread_mutex_lock(&mutex);
        while (taken != posted) xz_wake_wait_locked(&to_producer, &mutex, LIMIT_MS);
        pthread_mutex_unlock(&mutex);
        uint64_t took = now_us() - began;
        if (took > worst) worst = took;
        if (took > 4000000u) { fprintf(stderr, "round %u took %llu us: lost wake-up\n", round, (unsigned long long)took); abort(); }
    }
    pthread_mutex_lock(&mutex); quit = 1; pthread_mutex_unlock(&mutex); xz_wake_set(&to_worker);
    assert(!pthread_join(thread, NULL));
    printf("ping-pong %u rounds, worst round trip %llu us\n", (unsigned)ROUNDS, (unsigned long long)worst);
}

static void stale_ticket_returns_at_once(void) {
    struct xz_wake w = {0};
    uint32_t ticket = xz_wake_ticket(&w);
    xz_wake_set(&w);
    uint64_t began = now_us();
    xz_wake_wait(&w, ticket, 5000);
    assert(now_us() - began < 1000000u);
}

static void timeout_is_honoured(void) {
    struct xz_wake w = {0};
    uint64_t began = now_us();
    xz_wake_wait(&w, xz_wake_ticket(&w), 50);
    uint64_t took = now_us() - began;
    assert(took >= 40000u && took < 2000000u);
}

/* Many setters, one waiter: the waiter must see every posted item within the limit. */
static pthread_mutex_t many_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_wake many_wake;
static unsigned many_posted, many_done;
enum { SETTERS = 4, EACH = 20000 };
static void *setter(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < EACH; ++i) {
        pthread_mutex_lock(&many_mutex); many_posted++; pthread_mutex_unlock(&many_mutex);
        xz_wake_set(&many_wake);
    }
    pthread_mutex_lock(&many_mutex); many_done++; pthread_mutex_unlock(&many_mutex);
    xz_wake_set(&many_wake);
    return NULL;
}
static void many_setters(void) {
    pthread_t threads[SETTERS];
    for (unsigned i = 0; i < SETTERS; ++i) assert(!pthread_create(&threads[i], NULL, setter, NULL));
    unsigned seen = 0;
    pthread_mutex_lock(&many_mutex);
    while (many_done < SETTERS || seen < many_posted) {
        if (seen == many_posted) {
            uint64_t began = now_us();
            xz_wake_wait_locked(&many_wake, &many_mutex, LIMIT_MS);
            if (now_us() - began > 4000000u && seen == many_posted && many_done < SETTERS) {
                fprintf(stderr, "waiter slept through a set\n"); abort();
            }
            continue;
        }
        seen = many_posted;
    }
    pthread_mutex_unlock(&many_mutex);
    for (unsigned i = 0; i < SETTERS; ++i) assert(!pthread_join(threads[i], NULL));
    assert(seen == SETTERS * EACH);
    printf("many setters: %u items seen\n", seen);
}

int main(void) {
    stale_ticket_returns_at_once();
    timeout_is_honoured();
    ping_pong();
    many_setters();
    printf("xz_wake: all checks passed\n");
    return 0;
}
