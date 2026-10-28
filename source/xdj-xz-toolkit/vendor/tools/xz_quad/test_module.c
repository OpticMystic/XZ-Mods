#define _GNU_SOURCE
#define XZ_HOST_TEST
#ifdef BASELINE
#include "module.baseline.mock.c"
static bool adopt_existing;
#else
#include "xz_hub_depth.c"
#endif
#include <stdlib.h>
#include <limits.h>
static struct usb_device root,up,hub,music,child;
static struct usb_bus bus;
static struct usb_interface intf;
static struct usb_host_interface alt;
static struct usb_host_config cfg;
static unsigned test_port = 1;
static void device_init(struct usb_device *d,const char *name,int rank) {
    memset(d,0,sizeof(*d)); d->dev.name=name; d->dev.registered=true;
    d->dev.lock.real=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;d->dev.lock.rank=rank;
    d->refs=1;d->state=USB_STATE_CONFIGURED;
}
static void setup(void) {
    device_init(&root,"usb2",10);device_init(&up,"2-1",20);
    device_init(&hub,test_port==1?"2-1.2":"2-1.1",30);
    /* Any attempt by the module to lock music fails the lock-order assertion. */
    device_init(&music,test_port==1?"2-1.1":"2-1.2",0);device_init(&child,"child",40);
    up.parent=&root;up.level=1;up.devpath="1";up.descriptor.idVendor=0x0451;up.descriptor.idProduct=0x8027;
    hub.parent=&up;hub.devnum=8;hub.level=2;hub.devpath=test_port==1?"1.2":"1.1";hub.speed=USB_SPEED_HIGH;
    hub.descriptor.idVendor=0x2b73;hub.descriptor.idProduct=0x002e;
    hub.descriptor.bDeviceClass=USB_CLASS_HUB;hub.descriptor.bNumConfigurations=1;
    alt.desc.bInterfaceClass=USB_CLASS_HUB;intf.cur_altsetting=&alt;
    cfg.desc.bNumInterfaces=1;cfg.interface[0]=&intf;hub.actconfig=&cfg;
    root.children[0]=&up;up.children[test_port-1]=&music;up.children[2-test_port]=&hub;
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
static struct usb_interface music_intf;
static struct usb_host_interface music_alt;
static struct usb_host_config music_cfg;
static void music_fixture(void) {
    music.dev.lock.rank=30;music.parent=&up;music.level=2;music.devnum=9;
    music.devpath=test_port==1?"1.1":"1.2";music.speed=USB_SPEED_HIGH;
    music.descriptor.idVendor=0x2109;music.descriptor.idProduct=0x2817;
    music.descriptor.bDeviceClass=USB_CLASS_HUB;music.descriptor.bNumConfigurations=1;
    music_alt.desc.bInterfaceClass=USB_CLASS_HUB;music_intf.cur_altsetting=&music_alt;
    music_cfg.desc.bNumInterfaces=1;music_cfg.interface[0]=&music_intf;music.actconfig=&music_cfg;
}
static void detach_device(struct usb_device *d) {
    usb_lock_device(d);d->state=USB_STATE_NOTATTACHED;usb_unlock_device(d);
    notify(USB_DEVICE_REMOVE,d);run_work(&cleanup_work);
}
static void *music_remove_thread(void *p) {(void)p;notify(USB_DEVICE_REMOVE,&music);return NULL;}
static void music_case(int n) {
    music_fixture();music_hubs=true;
    bool reject=false;
    switch(n) {
    case 41: up.children[2-test_port]=NULL;break;
    case 42: music_hubs=false;reject=true;break;
    case 43: apply=false;reject=true;break;
    case 44: music_intf.dev.driver=&music_intf;reject=true;break;
    case 45: music.children[0]=&child;reject=true;break;
    case 46: music.speed=2;reject=true;break;
    case 47: music.devpath="1.3";reject=true;break;
    case 48: music.descriptor.idVendor=0x2b73;music.descriptor.idProduct=0x002e;reject=true;break;
    case 49: music.descriptor.idVendor=0;reject=true;break;
    case 50: music.descriptor.idProduct=0;reject=true;break;
    case 51: music.descriptor.bDeviceClass=0;reject=true;break;
    case 52: music.descriptor.bNumConfigurations=2;reject=true;break;
    case 53: music_cfg.desc.bNumInterfaces=2;reject=true;break;
    case 54: music.actconfig=NULL;reject=true;break;
    case 55: music_alt.desc.bInterfaceClass=8;reject=true;break;
    case 56: music.state=USB_STATE_NOTATTACHED;reject=true;break;
    case 57: music.dev.registered=false;reject=true;break;
    case 58: on_register=&music;break;
    case 59: adopt_existing=false;break;
    case 62: music.descriptor.bDeviceClass=8;music.dev.lock.rank=0;reject=true;break;
    case 67: music.state=USB_STATE_SUSPENDED;break;
    case 68: music.state=USB_STATE_SUSPENDED;music.actconfig=NULL;reject=true;break;
    case 69: music.devnum=0;reject=true;break;
    default:break;
    }
    if(n==65) {
        pthread_t init;usb_lock_device(&music);
        assert(!pthread_create(&init,NULL,init_thread,NULL));
        while(!__atomic_load_n(&notify_registered,__ATOMIC_ACQUIRE))usleep(100);
        hub_event(&hub_notifier,USB_DEVICE_ADD,&music);
        usb_unlock_device(&music);pthread_join(init,NULL);
    } else assert(xz_hub_depth_init()==0);
    if(n==59) {assert(!target&&!music_target);notify(USB_DEVICE_ADD,&hub);notify(USB_DEVICE_ADD,&music);}
    assert(root.level==0&&up.level==1);
    if(reject) {
        assert(!music_target&&!music_hub_devnum&&music.level==2&&music.refs==1);
        if(target)detach_device(&hub);
        assert(!target&&!module_refs&&!loopback_hub_devnum&&!queued);
        xz_hub_depth_exit();puts("PASS music hub guard; no mutation/ref leak");return;
    }
    assert(music_target==&music&&music.level==1&&music.refs==2&&music_hub_devnum==9);
    assert(target==(n==41?NULL:&hub));
    assert(module_refs==(n==41?1:2)&&loopback_hub_devnum==(n==41?0:8));
    notify(USB_DEVICE_ADD,&music);notify(USB_DEVICE_REMOVE,&music);run_work(&cleanup_work);
    assert(music_target==&music&&music.refs==2&&music_hub_devnum==9);
    if(n==61) {
        struct usb_device nested=music;
        nested.dev.lock.real=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        nested.dev.lock.rank=40;nested.dev.name="2-1.1.1";nested.devpath="1.1.1";
        nested.parent=&music;nested.level=2;nested.refs=1;nested.devnum=10;
        notify(USB_DEVICE_ADD,&nested);
        assert(nested.level==2&&nested.refs==1&&module_refs==2);
    }
    if(n==63) {
        pthread_t a,b;usb_lock_device(&music);
        assert(!pthread_create(&a,NULL,worker_thread,NULL));
        mutex_lock(&target_lock);mutex_unlock(&target_lock);
        assert(!pthread_create(&b,NULL,music_remove_thread,NULL));
        usb_unlock_device(&music);pthread_join(a,NULL);pthread_join(b,NULL);
        assert(module_refs==2&&music.refs==2&&hub.refs==2);
    }
    if(n==64) {
        pthread_t a,b;usb_lock_device(&hub);hub.state=USB_STATE_NOTATTACHED;
        music.state=USB_STATE_NOTATTACHED;
        assert(!pthread_create(&a,NULL,worker_thread,NULL));
        for(;;){pthread_mutex_lock(&work_lock);int running=active;pthread_mutex_unlock(&work_lock);if(running)break;usleep(100);}
        assert(!pthread_create(&b,NULL,exit_thread,NULL));
        usb_unlock_device(&hub);pthread_join(a,NULL);pthread_join(b,NULL);
        assert(!target&&!music_target&&!module_refs&&!queued&&!subscriber&&cancelled==1);
        assert(hub.level==2&&music.level==2&&hub.refs==1&&music.refs==1);
        assert(!music_hub_devnum&&!loopback_hub_devnum);
        puts("PASS concurrent two-hub worker/exit/last-put");return;
    }
    if(n==60) {
        struct usb_device replacement=music;
        replacement.dev.lock.real=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        replacement.level=2;replacement.refs=1;replacement.devnum=10;
        up.children[test_port-1]=&replacement;
        music.state=USB_STATE_NOTATTACHED;notify(USB_DEVICE_ADD,&replacement);
        assert(music_target==&music&&replacement.level==2);
        run_work(&cleanup_work);
        assert(music_target==&replacement&&music_hub_devnum==10&&module_refs==2);
        assert(music.level==2&&music.refs==1&&target==&hub);
        detach_device(&replacement);
        up.children[test_port-1]=NULL;
        assert(!music_target&&!music_hub_devnum&&target==&hub&&module_refs==1);
    } else if(n==66) {
        usb_lock_device(&music);music.state=USB_STATE_NOTATTACHED;usb_unlock_device(&music);
        run_work(&cleanup_work);assert(!music_target&&!music_hub_devnum&&target==&hub&&module_refs==1);
    } else if(n==70) {
        detach_device(&music);assert(!music_target&&target==&hub&&module_refs==1&&hub.level==1);
    }
    if(target) {
        detach_device(&hub);
        assert(!target&&!loopback_hub_devnum);
        if(music_target)assert(music_target==&music&&music.level==1&&module_refs==1&&music_hub_devnum==9);
    }
    if(music_target)detach_device(&music);
    assert(!target&&!music_target&&!module_refs&&!queued&&hub.refs==1&&music.refs==1);
    assert(!music_hub_devnum&&!loopback_hub_devnum);
    xz_hub_depth_exit();assert(!subscriber&&cancelled==1);
    puts("PASS two-hub independent adoption/lifecycle/refcounts");
}
#endif
int main(int argc,char **argv) {
    assert(argc==2||argc==3);int n=atoi(argv[1]);if(argc==3)test_port=(unsigned)atoi(argv[2]);
    assert(test_port==1||test_port==2);setup();apply=true;adopt_existing=true;
#ifdef BASELINE
    assert(xz_hub_depth_init()==0);assert(target==&hub&&hub.level==1);return 0;
#else
    loopback_port=test_port;
    if(n>=40) {assert(n<=70);music_case(n);return 0;}
    if(n>=36&&n<=38) {
        loopback_port=n==36?0:n==37?3:UINT_MAX;
        assert(xz_hub_depth_init()==-EINVAL&&!subscriber&&!target&&!module_refs&&!queued);
        puts("PASS invalid or missing port rejected before notifier registration");return 0;
    }
    bool reject=false;
    switch(n) {
    case 1: apply=false;reject=true;break;
    case 2: adopt_existing=false;reject=true;break;
    case 3: intf.dev.driver=&intf;reject=true;break;
    case 4: hub.children[0]=&child;reject=true;break;
    case 5: hub.descriptor.idVendor=1;reject=true;break;
    case 6: up.descriptor.idProduct=1;reject=true;break;
    case 7: hub.dev.name=music.dev.name;hub.devpath=test_port==1?"1.1":"1.2";reject=true;break;
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
    case 39: adopt_existing=false;on_register=&hub;loopback_port=3-test_port;reject=true;break;
    default:break;
    }
    if(n==24||n==25) {
        /* Competing ADD/REMOVE owns candidate lock while notifier-first init
         * reaches the existing-device scan. No scan can mutate concurrently. */
        pthread_t t;usb_lock_device(&hub);
        assert(!pthread_create(&t,NULL,init_thread,NULL));
        for(;;) {
            if(__atomic_load_n(&notify_registered,__ATOMIC_ACQUIRE))break;
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
        up.children[2-test_port]=&replacement;
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
