"""The receiver's VJ.Tools client slot against reconnects and probes: compile the actual vjfs_client_thread and
vjfs_client_has_header from xz_directfb_hook.c against host fixtures and check what a dropped or probing client
leaves behind (2026-10-30: a reconnect cleared the takeover picture, so the screen blinked every few seconds)."""
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
source = (HERE.parents[1] / 'vendor/tools/xz_runtime/xz_directfb_hook.c').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#ifdef NDEBUG
#error assertions required
#endif
''' + extract('struct __attribute__((packed)) vjfs_header {') + r''';
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int vjfs_client_fd = -1;
static int latest_visible, latest_exclusive, preview_valid, tick_valid;
static volatile uint32_t last_activity_ms;
static int closed;
static void log_line(const char *s){(void)s;}
static void handle_vjfs_client(int c){(void)c;}
static int fake_close(int c){(void)c;closed++;return 0;}
#define close fake_close
/* A probe's socket: what recv(MSG_PEEK) would see within the 500 ms header window. */
static const void *peer_bytes;static size_t peer_length;
static int fake_setsockopt(int a,int b,int c,const void *d,socklen_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
static ssize_t fake_recv(int fd,void *out,size_t n,int flags){(void)fd;(void)flags;
 size_t k=peer_length<n?peer_length:n;if(k)memcpy(out,peer_bytes,k);return (ssize_t)k;}
#define setsockopt fake_setsockopt
#define recv fake_recv
''' + extract('static void *vjfs_client_thread(void *argument)') + '\n' + extract('static int vjfs_client_has_header(int client)') + r'''
static unsigned failures;
#define EXPECT(what,cond) do{if(!(cond)){failures++;printf("FAIL %s\n",what);}}while(0)
static void live_session(int fd){vjfs_client_fd=fd;latest_visible=latest_exclusive=preview_valid=tick_valid=1;last_activity_ms=123456u;}
int main(void){
 /* The session's own client goes (a reconnect is on its way): the slot empties, the picture stays for its age limits. */
 live_session(7);vjfs_client_thread((void*)(intptr_t)7);
 EXPECT("a dropped client empties the slot",vjfs_client_fd==-1);
 EXPECT("a dropped client leaves the last picture to its age limits",latest_visible&&preview_valid&&tick_valid&&last_activity_ms==123456u);
 /* An evicted older client (a newer one already holds the slot) changes nothing. */
 live_session(9);vjfs_client_thread((void*)(intptr_t)7);
 EXPECT("an evicted older client leaves the live session alone",vjfs_client_fd==9&&latest_visible&&last_activity_ms==123456u);
 /* A probe that sends nothing, or something that is not a VJ.Tools header, is never accepted into the slot. */
 peer_length=0;EXPECT("a silent probe is not a client",!vjfs_client_has_header(3));
 static const char junk[24]="GET / HTTP/1.1\r\nHost: x";peer_bytes=junk;peer_length=sizeof junk;
 EXPECT("a non-VJ.Tools connection is not a client",!vjfs_client_has_header(3));
 struct vjfs_header h;memset(&h,0,sizeof h);memcpy(h.magic,"VJFS",4);h.version=1;peer_bytes=&h;peer_length=sizeof h-1;
 EXPECT("half a header is not a client",!vjfs_client_has_header(3));
 peer_length=sizeof h;EXPECT("a whole VJFS header is a client",vjfs_client_has_header(3));
 if(failures){printf("%u receiver expectation(s) failed\n",failures);return 1;}
 puts("PASS receiver link: a dropped client empties the slot and leaves the picture to its age limits, an evicted one changes nothing, probes and partial headers are never clients");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xz-receiver-link-') as folder:
    c = Path(folder) / 'test_receiver_link.c'
    c.write_text(harness)
    binary = Path(folder) / 'receiver-link'
    subprocess.run(['gcc', '-std=c11', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(c), '-pthread', '-o', str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
