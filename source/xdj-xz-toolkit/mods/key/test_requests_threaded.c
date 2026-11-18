/* Atomic native request APIs, concurrent writers/readers. */
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
static void *requests(void *arg){
 int d=(int)(intptr_t)arg;
 for(int n=0;n<100000;n++){
  int cents=(n%25-12)*100,out;
  assert(!xz_key_set_desired_cents(d,1,cents));
  assert(!xz_key_get_desired_cents(d,1,&out)&&out>=-1200&&out<=1200);
  (void)xz_key_clear_desired_cents(d,1,cents);
  (void)xz_key_has_applied(d,1);
 }
 return NULL;
}
int main(void){
 pthread_t threads[4];for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,requests,(void *)(intptr_t)(i%2)));
 for(int i=0;i<4;i++)pthread_join(threads[i],NULL);
 puts("PASS four concurrent native request writers/readers, conditional clear and applied-proof reads");
}
