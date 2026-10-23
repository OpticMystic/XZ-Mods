/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "network.h"
#include "../clock_compat.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct xz_net_service {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    struct xz_net_backend backend;
    struct xz_net_command command;
    struct xz_net_view view;
    int queued, active, stopping, done, cancel;
    unsigned stop_attempts;
    enum xz_net_result stop_result;
};
static uint64_t now(void *p) { struct xz_net_service *s=p;return s->backend.now_ms(s->backend.context); }
static int cancelled(void *p) { return __atomic_load_n(&((struct xz_net_service *)p)->cancel,__ATOMIC_ACQUIRE); }
static struct xz_net_control control(struct xz_net_service *s,unsigned ms,int can_cancel) {
    return (struct xz_net_control){s,now(s)+ms,now,can_cancel?cancelled:NULL};
}
static struct timespec until(unsigned ms) {
    struct timespec t={0};if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return t;t.tv_sec+=ms/1000;t.tv_nsec+=(long)(ms%1000)*1000000;
    if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}return t;
}
static void publish(struct xz_net_service *s,struct xz_net_view *v) {
    pthread_mutex_lock(&s->mutex);s->view=*v;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->mutex);
}
static void failed(struct xz_net_view *v,enum xz_net_result r,const struct xz_net_error *e) {
    v->result=r;v->phase=(r==XZ_NET_NEEDS_DHCP_CLIENT||r==XZ_NET_NEEDS_BACKEND||r==XZ_NET_NEEDS_DETECT)?XZ_NET_NEEDS_SETUP:XZ_NET_FAILED;
    snprintf(v->detail,sizeof v->detail,"%s",e&&e->detail[0]?e->detail:xz_net_result_text(r));
}
static enum xz_net_result restore(struct xz_net_service *s,struct xz_net_backend_ticket *ticket,struct xz_net_view *v) {
    v->phase=XZ_NET_RESTORING;publish(s,v);struct xz_net_error e={0};
    struct xz_net_control c=control(s,5000,0);
    enum xz_net_result r=s->backend.restore(s->backend.context,ticket,&c,&v->actual,&e);
    v->applied_known=0;v->advertised_known=0;
    if(r==XZ_NET_CONFLICT){failed(v,XZ_NET_CONFLICT,&e);v->rollback_available=0;return XZ_NET_OK;}
    if(r!=XZ_NET_OK){failed(v,XZ_NET_ROLLBACK_FAILED,&e);v->rollback_available=1;}
    else{v->rollback_available=0;v->result=XZ_NET_ROLLED_BACK;v->phase=XZ_NET_IDLE;snprintf(v->detail,sizeof v->detail,"Owned trial removed; native DHCP remains unknown");}
    return r;
}
static void *worker(void *p) {
    struct xz_net_service *s=p;struct xz_net_backend_ticket *ticket=NULL;
    struct xz_net_view v={.phase=XZ_NET_IDLE,.result=XZ_NET_NEEDS_DETECT};
    snprintf(v.detail,sizeof v.detail,"Set up network to detect actual adapters");publish(s,&v);
    for(;;){
        pthread_mutex_lock(&s->mutex);
        while(!s->queued&&!s->stopping){
            struct timespec t=until(100);pthread_cond_timedwait(&s->changed,&s->mutex,&t);
            if(ticket&&s->backend.maintain&&!s->queued&&!s->stopping){
                pthread_mutex_unlock(&s->mutex);struct xz_net_control c=control(s,250,0);
                enum xz_net_result r=s->backend.maintain(s->backend.context,ticket,&c,&v);
                if(r!=XZ_NET_OK&&r!=XZ_NET_PENDING){struct xz_net_error e={.code=r};snprintf(e.detail,sizeof e.detail,"%s",v.detail);v.applied_known=0;
                    if(restore(s,ticket,&v)==XZ_NET_OK){s->backend.release(s->backend.context,ticket);ticket=NULL;failed(&v,r,&e);}}
                publish(s,&v);pthread_mutex_lock(&s->mutex);
            }
        }
        if(s->stopping){
            pthread_mutex_unlock(&s->mutex);enum xz_net_result stopped=ticket?restore(s,ticket,&v):XZ_NET_OK;
            if(stopped==XZ_NET_OK){if(ticket)s->backend.release(s->backend.context,ticket);ticket=NULL;break;}
            pthread_mutex_lock(&s->mutex);s->stop_result=stopped;s->stop_attempts++;s->stopping=0;__atomic_store_n(&s->cancel,0,__ATOMIC_RELEASE);pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->mutex);continue;
        }
        struct xz_net_command cmd=s->command;s->queued=0;pthread_mutex_unlock(&s->mutex);
        v.request_id=cmd.request_id;struct xz_net_error e={0};enum xz_net_result r=XZ_NET_OK;
        if(cmd.kind==XZ_NET_CMD_DETECT){
            v.phase=XZ_NET_DETECTING;v.result=XZ_NET_PENDING;publish(s,&v);struct xz_net_control c=control(s,2000,1);
            r=s->backend.detect(s->backend.context,&c,&v.actual,&e);
            if(r==XZ_NET_OK){v.phase=ticket?XZ_NET_TRIAL_READY:XZ_NET_IDLE;v.result=XZ_NET_OK;snprintf(v.detail,sizeof v.detail,"%u adapter(s); native DHCP ownership is unknown",v.actual.count);}
            else{struct xz_net_error original=e;if(!ticket)failed(&v,r,&original);else if(restore(s,ticket,&v)==XZ_NET_OK){s->backend.release(s->backend.context,ticket);ticket=NULL;failed(&v,r,&original);}}
        }else if(cmd.kind==XZ_NET_CMD_APPLY_TRIAL){
            if(ticket){failed(&v,XZ_NET_BUSY,NULL);snprintf(v.detail,sizeof v.detail,"Revert the current trial before a new Apply");}
            else{
                v.trial_id=cmd.request_id;v.phase=XZ_NET_PREFLIGHT;v.result=XZ_NET_PENDING;publish(s,&v);
                struct xz_net_control c=control(s,2000,1);struct xz_net_backend_ticket *next=NULL;
                r=s->backend.prepare(s->backend.context,&cmd.body.apply,&c,&next,&e);
                if(r==XZ_NET_OK){
                    unsigned budget=2000;for(unsigned k=0;k<cmd.body.apply.policy.count;k++)if(xz_net_identity_equal(&cmd.body.apply.policy.order[k].adapter,&cmd.body.apply.selected)&&cmd.body.apply.policy.order[k].mode==XZ_NET_DHCP)budget=15000;
                    v.phase=XZ_NET_APPLYING;publish(s,&v);c=control(s,budget,1);
                    r=s->backend.apply(s->backend.context,next,&c,&e);
                    if(r==XZ_NET_OK){v.phase=XZ_NET_VERIFYING;publish(s,&v);c=control(s,3000,1);r=s->backend.verify(s->backend.context,next,&c,&v.actual,&e);}
                    if(r==XZ_NET_OK&&cancelled(s))r=XZ_NET_CANCELLED;
                    if(r==XZ_NET_OK){
                        ticket=next;v.applied=cmd.body.apply.policy;v.applied_known=1;v.rollback_available=1;v.result=XZ_NET_OK;v.phase=XZ_NET_TRIAL_READY;
                        s->backend.describe(s->backend.context,ticket,&v);
                    }else{
                        struct xz_net_error original=e;enum xz_net_result original_r=r;
                        if(restore(s,next,&v)==XZ_NET_OK){v.result=original_r;v.phase=XZ_NET_FAILED;snprintf(v.detail,sizeof v.detail,"%.78s; owned changes restored",original.detail[0]?original.detail:xz_net_result_text(original_r));s->backend.release(s->backend.context,next);}
                        else ticket=next;
                    }
                }else failed(&v,r,&e);
            }
        }else if(cmd.kind==XZ_NET_CMD_VERIFY){
            if(!ticket||cmd.body.trial_id!=v.trial_id)failed(&v,XZ_NET_STALE,NULL);
            else{v.phase=XZ_NET_VERIFYING;publish(s,&v);struct xz_net_control c=control(s,3000,1);r=s->backend.verify(s->backend.context,ticket,&c,&v.actual,&e);
                if(r==XZ_NET_OK){v.phase=XZ_NET_TRIAL_READY;v.result=XZ_NET_OK;v.applied_known=1;s->backend.describe(s->backend.context,ticket,&v);}else{struct xz_net_error original=e;v.applied_known=0;if(restore(s,ticket,&v)==XZ_NET_OK){s->backend.release(s->backend.context,ticket);ticket=NULL;failed(&v,r,&original);}}}
        }else if(cmd.kind==XZ_NET_CMD_REVERT){
            if(!ticket||cmd.body.trial_id!=v.trial_id)failed(&v,XZ_NET_STALE,NULL);
            else if(restore(s,ticket,&v)==XZ_NET_OK){s->backend.release(s->backend.context,ticket);ticket=NULL;}
        }
        publish(s,&v);pthread_mutex_lock(&s->mutex);s->active=0;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->mutex);
    }
    publish(s,&v);pthread_mutex_lock(&s->mutex);s->stop_result=XZ_NET_OK;s->done=1;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->mutex);return NULL;
}
enum xz_net_result xz_net_service_start(struct xz_net_service **out,const struct xz_net_backend *b) {
    if(!out||!b||!b->now_ms||!b->detect||!b->prepare||!b->apply||!b->verify||!b->restore||!b->release||!b->describe)return XZ_NET_INVALID;
    struct timespec probe;if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&probe))return XZ_NET_NEEDS_BACKEND;
    struct xz_net_service *s=calloc(1,sizeof *s);if(!s)return XZ_NET_NEEDS_BACKEND;s->backend=*b;
    if(pthread_mutex_init(&s->mutex,NULL)){free(s);return XZ_NET_NEEDS_BACKEND;}
    pthread_condattr_t attributes;if(pthread_condattr_init(&attributes)){pthread_mutex_destroy(&s->mutex);free(s);return XZ_NET_NEEDS_BACKEND;}
    int condition_error=pthread_condattr_setclock(&attributes,CLOCK_MONOTONIC);
    if(!condition_error)condition_error=pthread_cond_init(&s->changed,&attributes);
    pthread_condattr_destroy(&attributes);
    if(condition_error){pthread_mutex_destroy(&s->mutex);free(s);return XZ_NET_NEEDS_BACKEND;}
    s->view.result=XZ_NET_NEEDS_DETECT;
    if(pthread_create(&s->thread,NULL,worker,s)){pthread_cond_destroy(&s->changed);pthread_mutex_destroy(&s->mutex);free(s);return XZ_NET_NEEDS_BACKEND;}
    *out=s;return XZ_NET_OK;
}
enum xz_net_result xz_net_service_submit(struct xz_net_service *s,const struct xz_net_command *c) {
    if(!s||!c||!c->request_id||c->kind<XZ_NET_CMD_DETECT||c->kind>XZ_NET_CMD_CANCEL)return XZ_NET_INVALID;
    if(pthread_mutex_trylock(&s->mutex))return XZ_NET_BUSY;
    enum xz_net_result r=XZ_NET_OK;
    if(s->stopping)r=XZ_NET_BUSY;
    else if(c->kind==XZ_NET_CMD_CANCEL){
        uint64_t id=s->active&&s->command.kind==XZ_NET_CMD_APPLY_TRIAL?s->command.request_id:s->view.trial_id;
        if(!s->active||c->body.trial_id!=id)r=XZ_NET_STALE;
        else __atomic_store_n(&s->cancel,1,__ATOMIC_RELEASE);
    }else if(s->active||s->queued)r=XZ_NET_BUSY;
    else{s->command=*c;s->queued=s->active=1;__atomic_store_n(&s->cancel,0,__ATOMIC_RELEASE);pthread_cond_signal(&s->changed);}
    pthread_mutex_unlock(&s->mutex);return r;
}
enum xz_net_result xz_net_service_try_read(struct xz_net_service *s,struct xz_net_view *v) {
    if(!s||!v)return XZ_NET_INVALID;if(pthread_mutex_trylock(&s->mutex))return XZ_NET_BUSY;*v=s->view;pthread_mutex_unlock(&s->mutex);return XZ_NET_OK;
}
enum xz_net_result xz_net_service_stop(struct xz_net_service *s,uint64_t deadline) {
    if(!s)return XZ_NET_OK;pthread_mutex_lock(&s->mutex);unsigned attempt=s->stop_attempts;s->stopping=1;__atomic_store_n(&s->cancel,1,__ATOMIC_RELEASE);pthread_cond_signal(&s->changed);
    while(!s->done&&s->stop_attempts==attempt&&now(s)<deadline){struct timespec t=until(20);pthread_cond_timedwait(&s->changed,&s->mutex,&t);}
    if(s->stop_attempts!=attempt){enum xz_net_result r=s->stop_result;pthread_mutex_unlock(&s->mutex);return r;}
    if(!s->done){pthread_mutex_unlock(&s->mutex);return XZ_NET_BUSY;}
    enum xz_net_result r=s->stop_result;pthread_mutex_unlock(&s->mutex);pthread_join(s->thread,NULL);
    pthread_cond_destroy(&s->changed);pthread_mutex_destroy(&s->mutex);free(s);return r;
}
