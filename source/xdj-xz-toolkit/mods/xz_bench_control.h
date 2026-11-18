#ifndef XZ_BENCH_CONTROL_H
#define XZ_BENCH_CONTROL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define XZ_BENCH_VERSION 1u
#ifndef XZ_BENCH_SOCKET_PATH
#define XZ_BENCH_SOCKET_PATH "/dev/shm/xz-bench-control.sock"
#endif
#ifndef XZ_BENCH_TIMEOUT_MS
#define XZ_BENCH_TIMEOUT_MS 1000u
#endif
struct xz_bench_request { uint32_t version, request_id, expected_pid, op, deck; int32_t args[4]; };
struct xz_bench_result { int32_t status, args[4]; };
struct xz_bench_reply { uint32_t version, request_id, pid, status, state; int32_t operation_status, args[4]; };
enum xz_bench_status { XZ_BENCH_OK, XZ_BENCH_BAD_VERSION, XZ_BENCH_WRONG_PID,
 XZ_BENCH_BAD_OP, XZ_BENCH_STALE, XZ_BENCH_CONFLICT, XZ_BENCH_EXPIRED,
 XZ_BENCH_DISCONNECTED, XZ_BENCH_STOPPED, XZ_BENCH_MALFORMED };
enum xz_bench_state { XZ_BENCH_NOT_STARTED, XZ_BENCH_EXECUTED, XZ_BENCH_CANCELLED };
/* Callback runs synchronously ONLY from tick's caller. It must validate its own
   enum/argument domains before performing a bounded native operation. */
typedef void (*xz_bench_callback)(const struct xz_bench_request *, struct xz_bench_result *, void *);
/* start/stop are serialized lifecycle calls, with no concurrent tick. enabled=0
   creates nothing. op_mask admits op 1..31; deck is constrained to 0..4. Startup
   refuses ALL existing paths, including stale sockets; never takes them over. */
int xz_bench_control_start(int enabled, uint32_t op_mask);
void xz_bench_control_stop(void);
/* Nonblocking trylock, clock read, bounded callback; no I/O, allocation or wait.
   Returns 1 iff callback began, otherwise 0. Call only from one owner/UI thread. */
int xz_bench_control_tick(xz_bench_callback callback, void *opaque);
/* Fixed-size little-endian words, no pointers/addresses/paths/strings on wire. */
static inline void xz_bench_encode(void *wire, const void *object, size_t words) {
 unsigned char *b=wire;uint32_t v[10];memcpy(v,object,words*4);
 for(size_t i=0;i<words;i++)for(unsigned j=0;j<4;j++)b[i*4+j]=(unsigned char)(v[i]>>(j*8));
}
static inline void xz_bench_decode(void *object, const void *wire, size_t words) {
 const unsigned char *b=wire;uint32_t v[10];
 for(size_t i=0;i<words;i++){v[i]=0;for(unsigned j=0;j<4;j++)v[i]|=(uint32_t)b[i*4+j]<<(j*8);}
 memcpy(object,v,words*4);
}
_Static_assert(sizeof(struct xz_bench_request)==36,"request wire size");
_Static_assert(sizeof(struct xz_bench_reply)==40,"reply wire size");
#endif
