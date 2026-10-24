#define _GNU_SOURCE
#define XZ_HOST_TEST
#ifdef BASELINE
#include "module.baseline.mock.c"
static bool adopt_existing;
#else
#include "xz_hub_depth.c"
#endif
#include <stdlib.h>
static struct usb_device root,up,hub,music,child;
static struct usb_bus bus;
static struct usb_interface intf;
static struct usb_host_interface alt;
static struct usb_host_config cfg;
static void device_init(struct usb_device *d,const char *name,int rank) {
    memset(d,0,sizeof(*d)); d->dev.name=name; d->dev.registered=true;
    d->dev.lock.real=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;d->dev.lock.rank=rank;
    d->refs=1;d->state=USB_STATE_CONFIGURED;
}
static void setup(void) {
    device_init(&root,"usb2",10);device_init(&up,"2-1",20);
    device_init(&hub,"2-1.2",30);device_init(&music,"2-1.1",30);device_init(&child,"child",40);
    up.parent=&root;up.level=1;up.devpath="1";up.descriptor.idVendor=0x0451;up.descriptor.idProduct=0x8027;
    hub.parent=&up;hub.level=2;hub.devpath="1.2";hub.speed=USB_SPEED_HIGH;
    hub.descriptor.idVendor=0x2b73;hub.descriptor.idProduct=0x002e;
    hub.descriptor.bDeviceClass=USB_CLASS_HUB;hub.descriptor.bNumConfigurations=1;
    alt.desc.bInterfaceClass=USB_CLASS_HUB;intf.cur_altsetting=&alt;
    cfg.desc.bNumInterfaces=1;cfg.interface[0]=&intf;hub.actconfig=&cfg;
    root.children[0]=&up;up.children[0]=&music;up.children[1]=&hub;
    bus.busnum=2;bus.root_hub=&root;bus.hcd.rh_registered=1;usb_bus_list=&bus;
}
#ifndef BASELINE
static void physical_cleanup(void) {
    usb_lock_device(&hub);hub.state=USB_STATE_NOTATTACHED;usb_unlock_device(&hub);
    notify(USB_DEVICE_REMOVE,&hub);assert(hub.level==1&&module_refs==1);
    run_work(&cleanup_work);
    assert(!target&&hub.level==2&&hub.refs==1&&module_refs==0&&!queued);
}
static void *worker_thread(void *p) {(void)p;run_work(&cleanup_work);return NULL;}
static void *remove_thread(void *p) {(void)p;notify(USB_DEVICE_REMOVE,&hub);return NULL;}
static void *exit_thread(void *p) {(void)p;xz_hub_depth_exit();return NULL;}
static void *init_thread(void *p) {(void)p;assert(xz_hub_depth_init()==0);return NULL;}
#endif
int main(int argc,char **argv) {
    assert(argc==2);int n=atoi(argv[1]);setup();apply=true;adopt_existing=true;
#ifdef BASELINE
    assert(xz_hub_depth_init()==0);assert(target==&hub&&hub.level==1);return 0;
#else
    bool reject=false;
    switch(n) {
    case 1: apply=false;reject=true;break;
    case 2: adopt_existing=false;reject=true;break;
    case 3: intf.dev.driver=&intf;reject=true;break;
    case 4: hub.children[0]=&child;reject=true;break;
    case 5: hub.descriptor.idVendor=1;reject=true;break;
    case 6: up.descriptor.idProduct=1;reject=true;break;
    case 7: hub.dev.name="2-1.1";hub.devpath="1.1";reject=true;break;
    case 8: hub.state=USB_STATE_NOTATTACHED;reject=true;break;
    case 9: module_get_fails=1;reject=true;break;
    case 10: module_going=1;reject=true;break;
    case 11: cfg.desc.bNumInterfaces=2;reject=true;break;
    case 12: hub.dev.registered=false;reject=true;break;
    case 13: bus.hcd.rh_registered=0;reject=true;break;
    case 14: hub.actconfig=NULL;reject=true;break;
    case 15: alt.desc.bInterfaceClass=1;reject=true;break;
    case 16: on_register=&hub;break;
    case 17: adopt_existing=false;break;
    case 18: apply=false;on_register=&hub;reject=true;break;
    case 19: hub.level=3;reject=true;break;
    case 20: hub.children[USB_MAXCHILDREN-1]=&child;reject=true;break;
    case 21: hub.descriptor.bNumConfigurations=2;reject=true;break;
    case 28: apply=false;reject=true;break;
    case 29: apply=false;hub.descriptor.bDeviceClass=0;reject=true;break;
    case 30: apply=false;up.descriptor.idProduct=1;reject=true;break;
    case 31: apply=false;root.children[0]=NULL;reject=true;break;
    case 32: hub.state=USB_STATE_SUSPENDED;break;
    case 33: hub.state=USB_STATE_SUSPENDED;hub.actconfig=NULL;reject=true;break;
    case 34: hub.state=USB_STATE_SUSPENDED;intf.dev.driver=&intf;reject=true;break;
    case 35: hub.state=USB_STATE_SUSPENDED;hub.children[0]=&child;reject=true;break;
    default:break;
    }
    if(n==24||n==25) {
        /* Competing ADD/REMOVE owns candidate lock while notifier-first init
         * reaches the existing-device scan. No scan can mutate concurrently. */
        pthread_t t;usb_lock_device(&hub);
        assert(!pthread_create(&t,NULL,init_thread,NULL));
        for(;;) {
            pthread_rwlock_rdlock(&notify_lock);int registered=subscriber!=NULL;pthread_rwlock_unlock(&notify_lock);
            if(registered)break;
            usleep(100);
        }
        if(n==25)hub.state=USB_STATE_NOTATTACHED;
        hub_event(&hub_notifier,n==24?USB_DEVICE_ADD:USB_DEVICE_REMOVE,&hub);
        usb_unlock_device(&hub);pthread_join(t,NULL);
        if(n==25)reject=true;
    } else assert(xz_hub_depth_init()==0);
    if(n==17) {assert(!target);notify(USB_DEVICE_ADD,&hub);}
    assert(root.level==0&&up.level==1&&music.level==0&&music.refs==1);
    if(reject) {
        assert(!target&&hub.level==(n==19?3:2)&&hub.refs==1&&!module_refs&&!queued);
        if(n==28) assert(scan_stage==5&&reject_reason==19&&observed_level==2&&
                         observed_state==USB_STATE_CONFIGURED&&observed_device_class==USB_CLASS_HUB&&
                         observed_speed==USB_SPEED_HIGH&&observed_registered==1&&
                         observed_interface_driver==0&&observed_child_count==0);
        if(n==29) assert(scan_stage==5&&reject_reason==12&&observed_device_class==0);
        if(n==30) assert(scan_stage==3&&reject_reason==5);
        if(n==31) assert(scan_stage==2&&reject_reason==4);
        if(n==33) assert(scan_stage==5&&reject_reason==16&&observed_state==USB_STATE_SUSPENDED);
        if(n==34) assert(scan_stage==5&&reject_reason==17&&observed_interface_driver==1);
        if(n==35) assert(scan_stage==5&&reject_reason==18&&observed_child_count==1);
        xz_hub_depth_exit();assert(!subscriber&&cancelled==1);puts("PASS refusal/observation/default");return 0;
    }
    assert(target==&hub&&hub.level==1&&hub.refs==2&&module_refs==1);
    if(n==32) assert(scan_stage==5&&reject_reason==19&&observed_state==USB_STATE_SUSPENDED);
    notify(USB_DEVICE_ADD,&hub);assert(hub.refs==2&&module_refs==1);
    notify(USB_DEVICE_REMOVE,&hub);run_work(&cleanup_work);
    assert(target==&hub&&hub.level==1&&hub.refs==2&&module_refs==1&&queued);
    if(n==22) {
        /* Hold device lock: worker must not hold target_lock while waiting.
         * REMOVE can take target_lock, then both callbacks complete. */
        pthread_t a,b;usb_lock_device(&hub);
        assert(!pthread_create(&a,NULL,worker_thread,NULL));
        mutex_lock(&target_lock);mutex_unlock(&target_lock);
        assert(!pthread_create(&b,NULL,remove_thread,NULL));
        usb_unlock_device(&hub);pthread_join(a,NULL);pthread_join(b,NULL);
        assert(hub.refs==2&&module_refs==1);
    }
    if(n==23) {
        /* Exit waits for executing cleanup after its last self-pin drops.
         * Simulate worker blocked at device lock, then disconnect. */
        pthread_t a,b;usb_lock_device(&hub);hub.state=USB_STATE_NOTATTACHED;
        assert(!pthread_create(&a,NULL,worker_thread,NULL));
        for(;;){pthread_mutex_lock(&work_lock);int running=active;pthread_mutex_unlock(&work_lock);if(running)break;usleep(100);}
        assert(!pthread_create(&b,NULL,exit_thread,NULL));
        usb_unlock_device(&hub);pthread_join(a,NULL);pthread_join(b,NULL);
        assert(!target&&!module_refs&&hub.refs==1&&hub.level==2&&!queued&&!subscriber&&cancelled==1);
        puts("PASS concurrent worker/exit/last-put");return 0;
    }
    if(n==27) {
        /* Replacement ADD before old delayed cleanup must not be lost. */
        struct usb_device replacement=hub;
        replacement.dev.lock.real=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        replacement.level=2;replacement.refs=1;
        up.children[1]=&replacement;
        hub.state=USB_STATE_NOTATTACHED;
        notify(USB_DEVICE_ADD,&replacement);
        assert(target==&hub&&replacement.level==2&&replacement.refs==1);
        run_work(&cleanup_work);
        assert(target==&replacement&&module_refs==1&&hub.refs==1&&hub.level==2);
        assert(replacement.level==1&&replacement.refs==2&&queued);
        replacement.state=USB_STATE_NOTATTACHED;
        run_work(&cleanup_work);
        assert(!target&&!module_refs&&replacement.level==2&&replacement.refs==1&&!queued);
        xz_hub_depth_exit();puts("PASS fast replacement recovery and independent refs");return 0;
    }
    if(n==26) {
        /* Already unbound generic driver produces no second REMOVE. */
        usb_lock_device(&hub);hub.state=USB_STATE_NOTATTACHED;usb_unlock_device(&hub);
        run_work(&cleanup_work);
        assert(!target&&hub.level==2&&hub.refs==1&&module_refs==0&&!queued);
    } else physical_cleanup();
    /* New generation after cleanup: exactly one new pin. */
    hub.state=USB_STATE_CONFIGURED;notify(USB_DEVICE_ADD,&hub);
    assert(target==&hub&&module_refs==1&&hub.refs==2);physical_cleanup();
    xz_hub_depth_exit();assert(!subscriber&&cancelled==1&&!queued);
    puts("PASS adoption/duplicate/configured-remove/disconnect/reconnect");return 0;
#endif
}
