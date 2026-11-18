/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_KERNEL_GUARD_H
#define XZ_KERNEL_GUARD_H
/* Keeps normal-priority kernel work alive on the three non-audio cores. See kernel_guard.c. */
int xz_kernel_guard_start(void);
void xz_kernel_guard_stop(void);
#endif
