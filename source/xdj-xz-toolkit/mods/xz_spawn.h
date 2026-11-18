/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_SPAWN_H
#define XZ_SPAWN_H
/* Starting a child process from inside the player.
 *
 * The unit's glibc (2.13) runs posix_spawn through fork() whenever file actions or most attributes are given,
 * and popen() and system() always fork. A fork of the player copies its whole memory map while holding the
 * memory-map lock for writing, and leaves every private page write-protected. Measured on the unit: the
 * forking thread spends about 350 ms in the kernel, and for that time every other thread that touches a
 * protected page or maps memory waits in a page fault: the deck 3/4 players, the display thread, the control
 * surface threads and the stock audio thread. The network service did this every 20 to 60 seconds on a
 * network without a DHCP answer, which was heard as a dropout on decks 3 and 4 each time.
 *
 * POSIX_SPAWN_USEVFORK makes the same posix_spawn call use vfork: nothing is copied or protected, and only the
 * calling thread waits until the child has called exec. Every spawn from the player must set it; never call
 * fork(), popen() or system() from a thread of the player once playback can be running. */
#include <spawn.h>
#ifndef POSIX_SPAWN_USEVFORK
#define POSIX_SPAWN_USEVFORK 0x40
#endif
/* attributes must already be initialised; other flags set earlier are kept. */
static inline int xz_spawn_without_copy(posix_spawnattr_t *attributes)
{
    short flags = 0;
    if (posix_spawnattr_getflags(attributes, &flags)) return -1;
    return posix_spawnattr_setflags(attributes, (short)(flags | POSIX_SPAWN_USEVFORK));
}
#endif
