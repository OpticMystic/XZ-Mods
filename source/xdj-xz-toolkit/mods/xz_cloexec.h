/* SPDX-License-Identifier: MIT */
#ifndef XZ_CLOEXEC_H
#define XZ_CLOEXEC_H
#include <stdio.h> /* defines __GLIBC__ before the test below */
/* fopen mode for files on a USB stick that stay open: close-on-exec, so no helper the player starts (the four-deck
   bridge, the updater) inherits them and keeps the stick busy when USB STOP unmounts it (usb_release.h).
   glibc reads the 'e'; other C libraries (the Windows host tests) reject it, and start no such helpers. */
#ifdef __GLIBC__
#define XZ_FOPEN_READ_CLOEXEC "rbe"
#else
#define XZ_FOPEN_READ_CLOEXEC "rb"
#endif
#endif
