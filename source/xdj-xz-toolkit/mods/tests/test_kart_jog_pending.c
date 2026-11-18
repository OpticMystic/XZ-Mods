/* Cached ownership and inactive-contact tracking without threads, devices or file opens. */
#include "../kart/xz_kart_runtime.c"
#include <assert.h>
int main(void){
 xz_kart_host_init(&host,&inert);dispatch_enabled=1;
 pthread_mutex_lock(&mutex);unlock_published();assert(!xz_kart_runtime_input_pending());
 unsigned char event[24]={0};uint16_t code=0x4306;memcpy(event+8,&code,2);event[10]=1;
 assert(!xz_kart_runtime_observe_key(1,event));assert(host.physical_down[0]&XZ_KART_TOUCH);
 event[11]=2;assert(!xz_kart_runtime_observe_key(1,event));assert(!(host.physical_down[0]&XZ_KART_TOUCH));
 pthread_mutex_lock(&mutex);host.owned[0]=XZ_KART_TOUCH;unlock_published();assert(xz_kart_runtime_input_pending());
 /* An old game's owned release keeps the pending gate up after owner revocation. */
 assert(xz_kart_runtime_observe_key(1,event));assert(!xz_kart_runtime_input_pending());
 pthread_mutex_lock(&mutex);inert.owner=123;unlock_published();assert(xz_kart_runtime_input_pending());
 pthread_mutex_lock(&mutex);inert.owner=0;unlock_published();assert(!xz_kart_runtime_input_pending());
 return 0;
}
