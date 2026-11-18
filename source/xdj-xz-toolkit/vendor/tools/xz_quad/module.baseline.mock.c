#include "mock_kernel.h"
// SPDX-License-Identifier: GPL-2.0-only

static bool apply;
module_param(apply, bool, 0444);
MODULE_PARM_DESC(apply, "Change the qualified hub level; default is observation only");

static DEFINE_MUTEX(target_lock);
static struct usb_device *target;

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

static int hub_event(struct notifier_block *block, unsigned long action, void *data)
{
    struct usb_device *dev = data;

    if (action != USB_DEVICE_ADD && action != USB_DEVICE_REMOVE)
        return NOTIFY_DONE;

    mutex_lock(&target_lock);
    if (action == USB_DEVICE_REMOVE && target == dev) {
        dev->level = 2;
        target = NULL;
        printk(KERN_INFO "xz_hub_depth: target disconnected; level restored\n");
        module_put(THIS_MODULE);
    } else if (action == USB_DEVICE_ADD && !target && qualified_hub(dev)) {
        printk(KERN_INFO "xz_hub_depth: qualified 2b73:002e at %s apply=%u\n",
               dev->devpath, apply);
        if (apply && try_module_get(THIS_MODULE)) {
            /* The USB notifier runs while the USB device is still locked.
             * Pin this module until REMOVE; ordinary rmmod must refuse while
             * the altered device exists. Binding remains a userspace action.
             */
            target = dev;
            dev->level = 1;
            printk(KERN_INFO "xz_hub_depth: level 2 -> 1; awaiting explicit hub bind\n");
        }
    }
    mutex_unlock(&target_lock);
    return NOTIFY_OK;
}

static struct notifier_block hub_notifier = {
    .notifier_call = hub_event,
};

static int __init xz_hub_depth_init(void)
{
    BUILD_BUG_ON(offsetof(struct usb_device, dev) != 0x68);
    BUILD_BUG_ON(offsetof(struct usb_device, level) != 0x293);
    BUILD_BUG_ON(sizeof(struct module) != 320);
    usb_register_notify(&hub_notifier);
    printk(KERN_INFO "xz_hub_depth: registered apply=%u; no existing devices changed\n", apply);
    return 0;
}

static void __exit xz_hub_depth_exit(void)
{
    usb_unregister_notify(&hub_notifier);
    printk(KERN_INFO "xz_hub_depth: removed\n");
}

module_init(xz_hub_depth_init);
module_exit(xz_hub_depth_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Volatile XDJ-XZ loopback hub experiment on either top USB port");
