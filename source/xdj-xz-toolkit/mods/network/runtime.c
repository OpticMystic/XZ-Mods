/* SPDX-License-Identifier: MIT */
#include "runtime.h"
#include <pthread.h>
#include <string.h>
static pthread_mutex_t guard=PTHREAD_MUTEX_INITIALIZER;
static struct xz_net_service *service;
static struct xz_net_linux *linux_backend;
static struct xz_net_linux_os *os;
static struct xz_net_backend backend;
enum xz_net_result xz_net_runtime_start(const struct xz_net_linux_config *config){
    pthread_mutex_lock(&guard);enum xz_net_result r=XZ_NET_OK;
    if(!service){struct xz_net_linux_io io;r=xz_net_linux_os_create(&os,&io,config);
        if(!r)r=xz_net_linux_create(&linux_backend,&backend,&io);
        if(!r)r=xz_net_service_start(&service,&backend);
        if(r){xz_net_linux_destroy(linux_backend);linux_backend=NULL;xz_net_linux_os_destroy(os);os=NULL;}}
    pthread_mutex_unlock(&guard);return r;
}
enum xz_net_result xz_net_runtime_read(struct xz_net_view *v){
    if(pthread_mutex_trylock(&guard))return XZ_NET_BUSY;
    enum xz_net_result r=service?xz_net_service_try_read(service,v):XZ_NET_NEEDS_BACKEND;pthread_mutex_unlock(&guard);return r;
}
enum xz_net_result xz_net_runtime_submit(const struct xz_net_command *c){
    if(pthread_mutex_trylock(&guard))return XZ_NET_BUSY;
    enum xz_net_result r=service?xz_net_service_submit(service,c):XZ_NET_NEEDS_BACKEND;pthread_mutex_unlock(&guard);return r;
}
enum xz_net_result xz_net_runtime_advertised(char ip[16],char adapter[16]){
    if(!ip||!adapter)return XZ_NET_INVALID;
    struct xz_net_view v;enum xz_net_result r=xz_net_runtime_read(&v);if(r)return r;
    if(!v.applied_known||!v.advertised_known||v.phase!=XZ_NET_TRIAL_READY)return XZ_NET_NEEDS_DETECT;
    xz_net_ipv4_text(v.advertised.ip,ip);memcpy(adapter,v.advertised_adapter.name,16);return XZ_NET_OK;
}
int xz_mods_network_address_v1(char out[16]){
    if(!out)return 0;memset(out,0,16);char ip[16]={0},adapter[16];
    if(xz_net_runtime_advertised(ip,adapter)!=XZ_NET_OK)return 0;
    struct xz_net_ipv4 parsed;if(xz_net_parse_ipv4(ip,&parsed)!=XZ_NET_OK)return 0;
    xz_net_ipv4_text(parsed,out);return 1;
}
enum xz_net_result xz_net_runtime_stop(unsigned budget_ms){
    pthread_mutex_lock(&guard);enum xz_net_result r=XZ_NET_OK;
    if(service){r=xz_net_service_stop(service,backend.now_ms(backend.context)+budget_ms);if(!r){service=NULL;xz_net_linux_destroy(linux_backend);linux_backend=NULL;xz_net_linux_os_destroy(os);os=NULL;}}
    pthread_mutex_unlock(&guard);return r;
}
