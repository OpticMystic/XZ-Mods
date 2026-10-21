#ifdef NDEBUG
#error Native LOAD generation tests require assertions
#endif
#include "shell_runtime.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 struct xz_shrt_queue queue={0};struct xz_shrt_command out;
 struct xz_shrt_command a={.kind=XZ_SHRT_CMD_LOAD,.deck=0,.track_id=42,.device_kind=2,.media_generation=7};
 assert(xz_shrt_load_generation_current(&a,7));assert(!xz_shrt_load_generation_current(&a,0));assert(!xz_shrt_load_generation_current(&a,8));
 assert(xz_shrt_queue_push(&queue,&a));assert(xz_shrt_queue_pop(&queue,&out));
 assert(out.media_generation==7&&!xz_shrt_load_generation_current(&out,8));
 struct xz_shrt_command b=a;b.deck=1;b.device_kind=3;b.media_generation=8;
 struct xz_shrt_command setting={.kind=XZ_SHRT_CMD_TIME_MODE,.deck=0,.value=1};
 queue.head=XZ_SHRT_QUEUE-2;
 assert(xz_shrt_queue_push(&queue,&a)&&xz_shrt_queue_push(&queue,&setting)&&xz_shrt_queue_push(&queue,&b)&&xz_shrt_queue_push(&queue,&a));
 assert(xz_shrt_queue_drop_source(&queue,2)==2&&queue.count==2&&queue.dropped==2);
 assert(xz_shrt_queue_pop(&queue,&out)&&out.kind==XZ_SHRT_CMD_TIME_MODE);
 assert(xz_shrt_queue_pop(&queue,&out)&&out.device_kind==3&&out.media_generation==8);
 assert(!xz_shrt_queue_drop_source(&queue,2));
 a.media_generation=0;assert(!xz_shrt_load_generation_current(&a,7));assert(!xz_shrt_queue_push(&queue,&a));
 puts("PASS native LOAD generation: exact admission/dequeue token, wrapped selective source cancellation, settings/other USB preserved");
 return 0;
}
