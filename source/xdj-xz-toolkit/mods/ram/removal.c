/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "removal.h"
#include "abi.h"
#include "../runtime.h"
#include <stdint.h>
#include <string.h>

struct removal_scope { void *manager; unsigned pending; struct removal_scope *previous; int early_stop; };
static _Thread_local struct removal_scope *scope;
static void (*stock_remove)(void *, const void *);
static void (*stock_stop)(void *, const void *);
static void (*stock_stop_no_args)(void *);
static void (*stock_rec_complete)(void *, unsigned, const char *);
static void (*stock_unload)(void *, unsigned, unsigned);
static void (*stock_emergency)(void *, unsigned, unsigned);
static void (*manager_complete)(void *, int, unsigned);
static int (*stock_stop_key)(void *,const void *);
static void (*stock_mute)(void *,int,unsigned,unsigned);
static void (*stock_disconnect)(void *,int,unsigned,unsigned);

static uint32_t word(const void *object, unsigned offset)
{
    uint32_t value;
    memcpy(&value, (const unsigned char *)object + offset, sizeof(value));
    return value;
}
static int removing(const void *manager)
{
    unsigned state = word(manager, 0x88);
    return state == 3 || state == 4;
}
static int suppress(void *player, unsigned device, unsigned node)
{
    if (!scope || node || !removing(scope->manager)) return 0;
    unsigned kind = word(scope->manager, 0x3b0);
    if ((kind != 2 && kind != 3) || (device && device != kind)) return 0;
    for (unsigned deck = 0; deck < 2; ++deck) {
        if (word(scope->manager, 0x3e0 + deck * 4) != (uint32_t)(uintptr_t)player ||
            ((unsigned char *)scope->manager)[0x95 + deck] != 2) continue;
        if (!xz_ram_preserve_player((int)deck, kind,(uintptr_t)player)) return 0;
        scope->pending |= 1u << deck;
        return 1;
    }
    return 0;
}
static void unload_hook(void *player, unsigned device, unsigned node)
{
    if (!suppress(player, device, node)){xz_ram_forget_player((uintptr_t)player,device,node);stock_unload(player, device, node);}
}
static void emergency_for_caller(void *player,unsigned device,unsigned node,uintptr_t caller){
    int sentinel=caller==0xb09f8||caller==0xb0af0||caller==0xb4c24||caller==0xb4c80;
    if(sentinel&&xz_ram_preserve_sentinel((uintptr_t)player,device,node))return;
    if(!suppress(player,device,node)){xz_ram_forget_player((uintptr_t)player,device,node);stock_emergency(player,device,node);}
}
static void emergency_hook(void *player,unsigned device,unsigned node){
    emergency_for_caller(player,device,node,(uintptr_t)__builtin_return_address(0));
}
static int early_preserve(void *player,int value,unsigned device,unsigned node){
    if(!value||node||!scope||!scope->early_stop||word(scope->manager,0x88)!=2)return 0;
    unsigned kind=word(scope->manager,0x3b0);
    if((kind!=2&&kind!=3)||device!=kind)return 0;
    for(unsigned deck=0;deck<2;deck++)
        if(word(scope->manager,0x3e0+deck*4)==(uint32_t)(uintptr_t)player)
            return xz_ram_ready_player((int)deck,kind,(uintptr_t)player);
    return 0;
}
static void mute_hook(void *player,int value,unsigned device,unsigned node){
    if(!early_preserve(player,value,device,node))stock_mute(player,value,device,node);
}
static void disconnect_hook(void *player,int value,unsigned device,unsigned node){
    if(!early_preserve(player,value,device,node))stock_disconnect(player,value,device,node);
}
static void finish(struct removal_scope *current)
{
    scope = current->previous;
    if (scope && scope->manager == current->manager) {
        scope->pending |= current->pending;
        return;
    }
    /* Complete only after the dispatcher has set every deck obligation. Clear
     * TLS while acknowledging so reentrant stock calls retain normal behavior. */
    struct removal_scope *previous = scope;
    scope = NULL;
    for (unsigned deck = 0; deck < 2; ++deck) {
        if ((current->pending & (1u << deck)) && removing(current->manager) &&
            ((unsigned char *)current->manager)[0x95 + deck] == 2)
            manager_complete(current->manager, (int)deck, word(current->manager, 0x3b0));
    }
    scope = previous;
}
static void remove_hook(void *manager, const void *dataset)
{
    struct removal_scope current = {manager, 0, scope, 0};
    scope = &current;
    stock_remove(manager, dataset);
    finish(&current);
}
static void stop_hook(void *manager, const void *dataset)
{
    struct removal_scope current = {manager, 0, scope, 0};
    scope = &current;
    stock_stop(manager, dataset);
    finish(&current);
}
static void stop_no_args_hook(void *manager)
{
    struct removal_scope current = {manager, 0, scope, 0};
    scope = &current;
    stock_stop_no_args(manager);
    finish(&current);
}
static void rec_complete_hook(void *manager, unsigned message, const char *path)
{
    struct removal_scope current = {manager, 0, scope, 0};
    scope = &current;
    stock_rec_complete(manager, message, path);
    finish(&current);
}
static int stop_key_hook(void *manager,const void *input){
    int early=input&&(((const unsigned char *)input)[0xb]&15)==0&&word(manager,0x88)==2;
    struct removal_scope current={manager,0,scope,early};scope=&current;
    int result=stock_stop_key(manager,input);finish(&current);return result;
}

int xz_ram_removal_start(void)
{
    static const unsigned char complete_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    unsigned char actual[8];
    if (xz_read_memory(0x29f054, actual, sizeof(actual)) ||
        memcmp(actual, complete_guard, sizeof(actual))) return -1;
    manager_complete = (void (*)(void *, int, unsigned))(uintptr_t)0x29f054;
    /* A partial install remains inert: suppress requires a fully published RAM
     * deck, and the RAM worker starts only after every entrypoint is installed. */
    return (!stock_unload && xz_hook_arm(0x254628, xz_ram_hook_guard(0x254628), (void *)unload_hook, (void **)&stock_unload)) ||
        (!stock_stop_key && xz_hook_arm(0x29fd9c,xz_ram_hook_guard(0x29fd9c),(void *)stop_key_hook,(void **)&stock_stop_key)) ||
        (!stock_mute && xz_hook_arm(0x255210,xz_ram_hook_guard(0x255210),(void *)mute_hook,(void **)&stock_mute)) ||
        (!stock_disconnect && xz_hook_arm(0x2555a8,xz_ram_hook_guard(0x2555a8),(void *)disconnect_hook,(void **)&stock_disconnect)) ||
        (!stock_emergency && xz_hook_arm(0x2547f8, xz_ram_hook_guard(0x2547f8), (void *)emergency_hook, (void **)&stock_emergency)) ||
        (!stock_remove && xz_hook_arm(0x29c5a0, xz_ram_hook_guard(0x29c5a0), (void *)remove_hook, (void **)&stock_remove)) ||
        (!stock_stop && xz_hook_arm(0x29d6f4, xz_ram_hook_guard(0x29d6f4), (void *)stop_hook, (void **)&stock_stop)) ||
        (!stock_stop_no_args && xz_hook_arm(0x29e8b4, xz_ram_hook_guard(0x29e8b4), (void *)stop_no_args_hook, (void **)&stock_stop_no_args)) ||
        (!stock_rec_complete && xz_hook_arm(0x29e248, xz_ram_hook_guard(0x29e248), (void *)rec_complete_hook, (void **)&stock_rec_complete)) ? -1 : 0;
}
