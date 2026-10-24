#ifndef MOCK_KERNEL_H
#define MOCK_KERNEL_H
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define __init
#define __exit
#define KERN_INFO ""
#define HZ 100
#define USB_MAXCHILDREN 31
#define USB_SPEED_HIGH 3
#define USB_CLASS_HUB 9
#define USB_STATE_CONFIGURED 7
#define USB_STATE_SUSPENDED 8
#define USB_STATE_NOTATTACHED 0
#define USB_DEVICE_ADD 1
#define USB_DEVICE_REMOVE 2
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define BUILD_BUG_ON(x) ((void)0)
#define module_param(...)
#define MODULE_PARM_DESC(...)
#define MODULE_LICENSE(...)
#define MODULE_DESCRIPTION(...)
#define module_init(...)
#define module_exit(...)
#define le16_to_cpu(x) (x)
#define printk printf
struct mutex { pthread_mutex_t real; int rank; };
#define DEFINE_MUTEX(n) struct mutex n = {PTHREAD_MUTEX_INITIALIZER, 100}
static _Thread_local struct mutex *held[16];
static _Thread_local int depth;
static void mutex_lock(struct mutex *m) {
    assert(depth < 16);
    if (depth) assert(held[depth-1]->rank < m->rank);
    assert(!pthread_mutex_lock(&m->real)); held[depth++]=m;
}
static void mutex_unlock(struct mutex *m) {
    assert(depth && held[--depth]==m); assert(!pthread_mutex_unlock(&m->real));
}
struct device { const char *name; void *driver; bool registered; struct mutex lock; };
struct usb_interface_descriptor { unsigned char bInterfaceNumber,bInterfaceClass; };
struct usb_host_interface { struct usb_interface_descriptor desc; };
struct usb_interface { struct device dev; struct usb_host_interface *cur_altsetting; };
struct usb_host_config { struct { unsigned char bNumInterfaces; } desc; struct usb_interface *interface[1]; };
struct usb_device {
    struct device dev; struct usb_device *parent;
    int level,speed,state,refs;
    const char *devpath;
    struct { unsigned short idVendor,idProduct; unsigned char bDeviceClass,bNumConfigurations; } descriptor;
    struct usb_host_config *actconfig;
    struct usb_device *children[USB_MAXCHILDREN];
};
static const char *dev_name(const struct device *d) { return d->name; }
static bool device_is_registered(const struct device *d) { return d->registered; }
static void usb_lock_device(struct usb_device *d) {mutex_lock(&d->dev.lock);}
static void usb_unlock_device(struct usb_device *d) {mutex_unlock(&d->dev.lock);}
static struct usb_device *usb_get_dev(struct usb_device *d) {if(d)assert(__sync_add_and_fetch(&d->refs,1)>1);return d;}
static void usb_put_dev(struct usb_device *d) {if(d){assert(depth==0);assert(__sync_sub_and_fetch(&d->refs,1)>=1);}}
static int module_refs, module_get_fails, module_going;
#define THIS_MODULE NULL
static int try_module_get(void *m) {(void)m; if(module_get_fails||module_going)return 0;__sync_add_and_fetch(&module_refs,1);return 1;}
static void module_put(void *m) {(void)m;assert(depth==0);assert(__sync_sub_and_fetch(&module_refs,1)>=0);}
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; void (*fn)(struct work_struct *); };
#define DECLARE_DELAYED_WORK(n,f) struct delayed_work n = {{0},f}
static pthread_mutex_t work_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_cv=PTHREAD_COND_INITIALIZER;
static int queued,active,cancelled;
static int schedule_delayed_work(struct delayed_work *w,unsigned long delay) {
    (void)w;assert(delay==HZ);pthread_mutex_lock(&work_lock);int old=queued;queued=1;pthread_mutex_unlock(&work_lock);return !old;
}
static bool cancel_delayed_work_sync(struct delayed_work *w) {
    (void)w;assert(depth==0);pthread_mutex_lock(&work_lock);
    while(active)pthread_cond_wait(&work_cv,&work_lock);
    queued=0;cancelled++;pthread_mutex_unlock(&work_lock);return true;
}
static void run_work(struct delayed_work *w) {
    pthread_mutex_lock(&work_lock);assert(!active);if(!queued){pthread_mutex_unlock(&work_lock);return;}
    queued=0;active=1;pthread_mutex_unlock(&work_lock);w->fn(&w->work);
    pthread_mutex_lock(&work_lock);active=0;pthread_cond_broadcast(&work_cv);pthread_mutex_unlock(&work_lock);
}
struct notifier_block {int (*notifier_call)(struct notifier_block *,unsigned long,void *);};
static pthread_rwlock_t notify_lock=PTHREAD_RWLOCK_INITIALIZER;
static struct notifier_block *subscriber;
static struct usb_device *on_register;
static void notify(unsigned long action,struct usb_device *d) {
    pthread_rwlock_rdlock(&notify_lock);usb_lock_device(d);
    if(subscriber)subscriber->notifier_call(subscriber,action,d);
    usb_unlock_device(d);pthread_rwlock_unlock(&notify_lock);
}
static void usb_register_notify(struct notifier_block *n) {
    pthread_rwlock_wrlock(&notify_lock);assert(!subscriber);subscriber=n;pthread_rwlock_unlock(&notify_lock);
    if(on_register)notify(USB_DEVICE_ADD,on_register);
}
static void usb_unregister_notify(struct notifier_block *n) {
    assert(depth==0);pthread_rwlock_wrlock(&notify_lock);assert(subscriber==n);subscriber=NULL;pthread_rwlock_unlock(&notify_lock);
}
struct usb_hcd {int rh_registered;};
struct usb_bus {int busnum;struct usb_device *root_hub;struct usb_hcd hcd;struct usb_bus *next;};
static struct usb_bus *usb_bus_list;
static struct mutex usb_bus_list_lock={PTHREAD_MUTEX_INITIALIZER,1};
#define bus_to_hcd(b) (&(b)->hcd)
#define list_for_each_entry(b,h,member) for((b)=*(h);(b);(b)=(b)->next)
#endif
