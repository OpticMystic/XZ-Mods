/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef XZ_QUAD_PORT_LAYOUT_H
#define XZ_QUAD_PORT_LAYOUT_H
/* Front-panel labels are the reverse of the upstream hub's child numbers. */
struct xz_quad_port {
    unsigned top, child;
    const char *mount, *usb, *devpath, *ancestry;
};
static const struct xz_quad_port xz_quad_ports[] = {
    {1, 1, "/media/usb1", "2-1.2", "1.2", "/usb2/2-1/2-1.2/2-1.2:1.0/"},
    {2, 0, "/media/usb2", "2-1.1", "1.1", "/usb2/2-1/2-1.1/2-1.1:1.0/"}
};
static inline const struct xz_quad_port *xz_quad_port(unsigned top)
{
    return top >= 1 && top <= 2 ? &xz_quad_ports[top - 1] : 0;
}
#endif
