/* SPDX-License-Identifier: MIT */
#include "usb_profile.h"
#include <string.h>
int xz_usb_profile_load(struct xz_usb_profile *p,const char *root,
    unsigned long mount_id,struct xz_settings *out,unsigned *present) {
    if(!p||!root||!out||strlen(root)>=sizeof(p->root)||(!mount_id&&!strncmp(root,"/media/",7)))return -1;
    struct xz_settings next;xz_settings_default(&next);
    unsigned fields=0;
    int rc=xz_settings_load_present(root,&next,&fields);
    if(mount_id&&xz_settings_mount_id(root)!=mount_id)return -2;
    strcpy(p->root,root);p->mount_id=mount_id;p->generation++;
    /* Invalid records also use defaults, never the previous user's values. */
    if(rc<0)xz_settings_default(&next);
    if(present)*present=rc?0:fields;
    *out=next;return rc;
}
int xz_usb_profile_save(const struct xz_usb_profile *p,uint64_t generation,
    const struct xz_settings *settings) {
    if(!p||!p->root[0]||generation!=p->generation)return -1;
    return xz_settings_save_mount(p->root,p->mount_id,settings);
}
void xz_usb_profile_removed(struct xz_usb_profile *p) {
    if(!p)return;p->root[0]=0;p->mount_id=0;p->generation++;
}
