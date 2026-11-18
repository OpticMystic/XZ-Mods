/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_PIN_H
#define XZ_RAM_PIN_H
#include <stddef.h>
#include <stdio.h>
struct xz_ram_file_pin {void *mapping;size_t bytes;};
/* Owning worker only, with every FILE quiescent. All files or none. */
int xz_ram_files_pin(FILE *const files[],struct xz_ram_file_pin pins[],size_t count);
void xz_ram_file_unpin(struct xz_ram_file_pin *);
/* For the calling thread's next xz_ram_files_pin: asked before every 32 KiB of the copy.
 * 0 carry on, above 0 wait and ask again, below 0 abandon the copy (the pin then returns 0). NULL clears. */
void xz_ram_pin_set_poll(int (*poll)(void *),void *context);
#endif
