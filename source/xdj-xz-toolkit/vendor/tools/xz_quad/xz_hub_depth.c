// SPDX-License-Identifier: GPL-2.0-only
#ifdef XZ_HOST_TEST
#include "mock_kernel.h"
#else
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#endif

static bool apply;
module_param(apply, bool, 0444);
MODULE_PARM_DESC(apply, "Change the qualified hub level; default is observation only");
static bool adopt_existing;
module_param(adopt_existing, bool, 0444);
MODULE_PARM_DESC(adopt_existing, "Also inspect an existing unbound childless 2-1.2 hub");

/* Read-only, bounded admission trace. Values are reset only by module load.
 * These fields describe the last existing-device scan, not device addresses. */
static int scan_stage, reject_reason;
static int observed_level = -1, observed_state = -1;
static int observed_device_class = -1, observed_speed = -1;
static int observed_registered = -1, observed_interface_driver = -1;
static int observed_child_count = -1;
module_param(scan_stage, int, 0444);
module_param(reject_reason, int, 0444);
module_param(observed_level, int, 0444);
module_param(observed_state, int, 0444);
module_param(observed_device_class, int, 0444);
module_param(observed_speed, int, 0444);
module_param(observed_registered, int, 0444);
module_param(observed_interface_driver, int, 0444);
module_param(observed_child_count, int, 0444);

/* 1=no bus2, 2=HCD root unregistered, 3=no root, 4=no upstream,
 * 5=upstream mismatch, 6=no USB1, 7=exiting/owned, 8=name/path,
 * 9=level, 10=speed, 11=VID/PID, 12=device class,
 * 13=parent topology, 14=unregistered, 15=state,
 * 16=configuration, 17=interface absent/bound/descriptor,
 * 18=children present, 19=qualified. */
static void diagnose_existing(struct usb_device *dev)
{
    const struct usb_device *parent = dev->parent;
    const struct usb_interface *intf = dev->actconfig &&
        dev->actconfig->desc.bNumInterfaces == 1 ?
        dev->actconfig->interface[0] : NULL;
    unsigned int i;

    scan_stage = 5;
    observed_level = dev->level;
    observed_state = dev->state;
    observed_device_class = dev->descriptor.bDeviceClass;
    observed_speed = dev->speed;
    observed_registered = device_is_registered(&dev->dev);
    observed_interface_driver = intf ? !!intf->dev.driver : -1;
    observed_child_count = 0;
    for (i = 0; i < USB_MAXCHILDREN; ++i)
        observed_child_count += !!dev->children[i];

    if (strcmp(dev_name(&dev->dev), "2-1.2") ||
        strcmp(dev->devpath, "1.2")) reject_reason = 8;
    else if (dev->level != 2) reject_reason = 9;
    else if (dev->speed != USB_SPEED_HIGH) reject_reason = 10;
    else if (le16_to_cpu(dev->descriptor.idVendor) != 0x2b73 ||
             le16_to_cpu(dev->descriptor.idProduct) != 0x002e) reject_reason = 11;
    else if (dev->descriptor.bDeviceClass != USB_CLASS_HUB) reject_reason = 12;
    else if (!parent || parent->level != 1 || strcmp(parent->devpath, "1") ||
             le16_to_cpu(parent->descriptor.idVendor) != 0x0451 ||
             le16_to_cpu(parent->descriptor.idProduct) != 0x8027 ||
             !parent->parent || parent->parent->parent) reject_reason = 13;
    else if (!observed_registered) reject_reason = 14;
    else if (dev->state != USB_STATE_CONFIGURED &&
             dev->state != USB_STATE_SUSPENDED) reject_reason = 15;
    else if (!dev->actconfig || dev->descriptor.bNumConfigurations != 1 ||
             dev->actconfig->desc.bNumInterfaces != 1) reject_reason = 16;
    else if (!intf || intf->dev.driver || !intf->cur_altsetting ||
             intf->cur_altsetting->desc.bInterfaceNumber != 0 ||
             intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_HUB)
        reject_reason = 17;
    else if (observed_child_count) reject_reason = 18;
    else reject_reason = 19;
}

static DEFINE_MUTEX(target_lock);
static struct usb_device *target;
static bool exiting;
static void cleanup_target(struct work_struct *work);
static void inspect_existing(void);
static DECLARE_DELAYED_WORK(cleanup_work, cleanup_target);

static bool qualified_hub(const struct usb_device *dev)
{
    const struct usb_device *parent = dev->parent;
    const char *name = dev_name(&dev->dev);
    const char *path = !strcmp(name, "2-1.1") ? "1.1" :
                       !strcmp(name, "2-1.2") ? "1.2" : NULL;

    return path && dev->level == 2 && dev->speed == USB_SPEED_HIGH &&
        le16_to_cpu(dev->descriptor.idVendor) == 0x2b73 &&
        le16_to_cpu(dev->descriptor.idProduct) == 0x002e &&
        dev->descriptor.bDeviceClass == USB_CLASS_HUB &&
        !strcmp(dev->devpath, path) && parent &&
        parent->level == 1 && !strcmp(parent->devpath, "1") &&
        le16_to_cpu(parent->descriptor.idVendor) == 0x0451 &&
        le16_to_cpu(parent->descriptor.idProduct) == 0x8027 &&
        parent->parent && !parent->parent->parent;
}

/* Caller holds the USB device lock. Interface binding also requires this
 * parent lock (drivers/base/bus.c driver_bind/driver_unbind). */
static bool idle_hub(struct usb_device *dev)
{
    struct usb_interface *intf;
    unsigned int i;

    /* A retained configuration can be autosuspended. usb_probe_interface()
     * autoresumes before calling the hub driver; this module does not resume. */
    if (!device_is_registered(&dev->dev) ||
        (dev->state != USB_STATE_CONFIGURED &&
         dev->state != USB_STATE_SUSPENDED) ||
        !dev->actconfig || dev->descriptor.bNumConfigurations != 1 ||
        dev->actconfig->desc.bNumInterfaces != 1)
        return false;
    intf = dev->actconfig->interface[0];
    if (!intf || intf->dev.driver || !intf->cur_altsetting ||
        intf->cur_altsetting->desc.bInterfaceNumber != 0 ||
        intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_HUB)
        return false;
    for (i = 0; i < USB_MAXCHILDREN; ++i)
        if (dev->children[i])
            return false;
    return true;
}

/* USB device lock -> target_lock; never acquire USB locks in the notifier.
 * No bind, reset, or configuration operations occur in this module. */
static void consider_locked(struct usb_device *dev, bool existing)
{
    mutex_lock(&target_lock);
    if (existing) {
        if (exiting || target) reject_reason = 7;
        else diagnose_existing(dev);
    }
    if (!exiting && !target && (!existing || !strcmp(dev_name(&dev->dev), "2-1.2")) &&
        qualified_hub(dev) && idle_hub(dev)) {
        printk(KERN_INFO "xz_hub_depth: qualified %s existing=%u apply=%u\n",
               dev_name(&dev->dev), existing, apply);
        if (apply && try_module_get(THIS_MODULE)) {
            target = usb_get_dev(dev);
            dev->level = 1;
            /* Monitor even without REMOVE: generic-driver unbind emits REMOVE
             * while configured, and subsequent disconnect may emit none. */
            schedule_delayed_work(&cleanup_work, HZ);
            printk(KERN_INFO "xz_hub_depth: level 2 -> 1; awaiting explicit hub bind\n");
        }
    }
    mutex_unlock(&target_lock);
}

static void cleanup_target(struct work_struct *work)
{
    struct usb_device *dev;
    bool released = false;
    (void)work;

    /* Snapshot a temporary reference without holding target_lock while
     * acquiring the USB device lock. */
    mutex_lock(&target_lock);
    dev = usb_get_dev(target);
    mutex_unlock(&target_lock);
    if (!dev)
        return;
    usb_lock_device(dev);
    mutex_lock(&target_lock);
    if (target == dev) {
        if (dev->state == USB_STATE_NOTATTACHED) {
            dev->level = 2;
            target = NULL;
            released = true;
            printk(KERN_INFO "xz_hub_depth: physically disconnected; level restored\n");
        } else if (!exiting) {
            schedule_delayed_work(&cleanup_work, HZ);
        }
    }
    mutex_unlock(&target_lock);
    usb_unlock_device(dev);
    if (released)
        usb_put_dev(dev); /* ownership reference, after all device locks */
    usb_put_dev(dev); /* temporary reference */
    /* ADD may precede this deferred cleanup. While the old self-pin still
     * protects module text, recover a missed replacement on the supported
     * adoption path. The scan takes all device locks afresh, never target_lock
     * -> device lock. The new generation acquires its own independent pin. */
    if (released && adopt_existing)
        inspect_existing();
    /* exit synchronizes with this callback before freeing module text. No
     * work is queued and no module state is accessed after this final put. */
    if (released)
        module_put(THIS_MODULE);
}

static int hub_event(struct notifier_block *block, unsigned long action, void *data)
{
    struct usb_device *dev = data;
    (void)block;
    if (action == USB_DEVICE_ADD) {
        consider_locked(dev, false);
    } else if (action == USB_DEVICE_REMOVE) {
        mutex_lock(&target_lock);
        if (target == dev) {
            printk(KERN_INFO "xz_hub_depth: REMOVE observed state=%u; cleanup deferred\n",
                   dev->state);
            schedule_delayed_work(&cleanup_work, HZ);
        }
        mutex_unlock(&target_lock);
    } else {
        return NOTIFY_DONE;
    }
    return NOTIFY_OK;
}

static struct notifier_block hub_notifier = { .notifier_call = hub_event };

static void inspect_existing(void)
{
    struct usb_bus *bus;
    /* Match the public usbfs traversal locking in devices.c. Bus-list mutex
     * also excludes root teardown. Only bus 2 / root port 1 / upstream port 2
     * is inspected; the music port is never locked or modified. */
    scan_stage = 0;
    reject_reason = 1;
    mutex_lock(&usb_bus_list_lock);
    list_for_each_entry(bus, &usb_bus_list, bus_list) {
        struct usb_device *root, *upstream, *dev;
        if (bus->busnum != 2)
            continue;
        scan_stage = 1;
        if (!bus_to_hcd(bus)->rh_registered) { reject_reason = 2; continue; }
        if (!bus->root_hub) { reject_reason = 3; continue; }
        root = bus->root_hub;
        scan_stage = 2;
        usb_lock_device(root);
        upstream = root->children[0];
        if (upstream) {
            scan_stage = 3;
            usb_lock_device(upstream);
            if (upstream->level == 1 && !strcmp(upstream->devpath, "1") &&
                le16_to_cpu(upstream->descriptor.idVendor) == 0x0451 &&
                le16_to_cpu(upstream->descriptor.idProduct) == 0x8027) {
                dev = upstream->children[1];
                if (dev) {
                    scan_stage = 4;
                    usb_lock_device(dev);
                    consider_locked(dev, true);
                    usb_unlock_device(dev);
                } else reject_reason = 6;
            } else reject_reason = 5;
            usb_unlock_device(upstream);
        } else reject_reason = 4;
        usb_unlock_device(root);
    }
    mutex_unlock(&usb_bus_list_lock);
}

static int __init xz_hub_depth_init(void)
{
#ifndef XZ_HOST_TEST
    BUILD_BUG_ON(offsetof(struct usb_device, dev) != 0x68);
    BUILD_BUG_ON(offsetof(struct usb_device, level) != 0x293);
    BUILD_BUG_ON(sizeof(struct module) != 320);
#endif
    usb_register_notify(&hub_notifier);
    if (adopt_existing)
        inspect_existing();
    /* No failure return after notifier registration, pin, or mutation. */
    printk(KERN_INFO "xz_hub_depth: registered apply=%u adopt_existing=%u\n",
           apply, adopt_existing);
    return 0;
}

static void __exit xz_hub_depth_exit(void)
{
    usb_unregister_notify(&hub_notifier);
    mutex_lock(&target_lock);
    exiting = true;
    mutex_unlock(&target_lock);
    cancel_delayed_work_sync(&cleanup_work);
    printk(KERN_INFO "xz_hub_depth: removed\n");
}

module_init(xz_hub_depth_init);
module_exit(xz_hub_depth_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Volatile XDJ-XZ loopback hub metadata with guarded existing USB1 adoption");
