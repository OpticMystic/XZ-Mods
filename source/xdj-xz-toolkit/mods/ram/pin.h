/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_PIN_H
#define XZ_RAM_PIN_H
#include <stddef.h>
#include <stdio.h>
struct xz_ram_file_pin {void *mapping;size_t bytes;};
/* Owning worker only, with every FILE quiescent. All files or none. */
int xz_ram_files_pin(FILE *const files[],struct xz_ram_file_pin pins[],size_t count);
void xz_ram_file_unpin(struct xz_ram_file_pin *);
#endif
