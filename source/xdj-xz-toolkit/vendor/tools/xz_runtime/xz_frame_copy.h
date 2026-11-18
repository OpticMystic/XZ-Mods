/* SPDX-License-Identifier: MPL-2.0 */
#ifndef XZ_FRAME_COPY_H
#define XZ_FRAME_COPY_H
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define XZ_FRAME_BYTES (1600u * 480u)
typedef void *(*xz_frame_copy_fn)(void *, const void *, size_t);
static xz_frame_copy_fn xz_frame_libc_copy;
/* version, requested mode, applied mode, source mod64, libc available,
 * frame count, libc frames, compiler-runtime frames.
 * Mode bit0 selects libc; bit1 selects the aligned render window. */
__attribute__((visibility("default"))) uint32_t xz_frame_copy_v1[8] = {1,3,UINT32_MAX};

static int xz_frame_copy_initialize(void) {
    Dl_info info;
    /* Use the already-loaded libc explicitly; another preload may export a
     * different memcpy. This NODELETE bridge itself retains its libc dependency. */
    void *library = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
    if (!library) return 0;
    void *symbol = dlsym(library, "memcpy");
    int located = symbol && dladdr(symbol, &info) && info.dli_fname;
    dlclose(library);
    if (!located) return 0;
    const char *name = strrchr(info.dli_fname, '/');
    name = name ? name + 1 : info.dli_fname;
    if (strcmp(name, "libc.so.6") && strncmp(name, "libc-", 5)) return 0;
    xz_frame_libc_copy = (xz_frame_copy_fn)symbol;
    __atomic_store_n(&xz_frame_copy_v1[4], 1u, __ATOMIC_RELEASE);
    return 1;
}

/* Invoked only by the normal-priority diagnostics worker. Invalid or absent
 * control files retain the selected mode. No file access on the copy path. */
static void xz_frame_copy_accept_control(const char *value, ssize_t length) {
    if ((length == 1 || (length == 2 && value[1] == '\n')) && value[0] >= '0' && value[0] <= '3')
        __atomic_store_n(&xz_frame_copy_v1[1], (unsigned)(value[0]-'0'), __ATOMIC_RELEASE);
}
static void xz_frame_copy_control_tick(void) {
    int fd = open("/dev/shm/xz-frame-copy-mode", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return;
    char value[3]; ssize_t length = read(fd, value, sizeof(value)); close(fd);
    xz_frame_copy_accept_control(value, length);
}

static inline unsigned xz_frame_copy_render_offset(void) {
    /* Twelve RGB565 pixels reproduce the existing source's 24-byte offset. */
    return (__atomic_load_n(&xz_frame_copy_v1[1], __ATOMIC_ACQUIRE) & 2u) ? 0u : 12u;
}

static void xz_frame_copy_present(uint8_t *destination, size_t pitch, const uint8_t *source) {
    unsigned mode = __atomic_load_n(&xz_frame_copy_v1[1], __ATOMIC_ACQUIRE);
    xz_frame_copy_fn native = (mode & 1u) ? xz_frame_libc_copy : NULL;
    if (pitch == 1600u) {
        if (native) native(destination, source, XZ_FRAME_BYTES);
        else memcpy(destination, source, XZ_FRAME_BYTES);
    } else {
        for (unsigned row = 0; row < 480u; row++) {
            if (native) native(destination + row*pitch, source + row*1600u, 1600u);
            else memcpy(destination + row*pitch, source + row*1600u, 1600u);
        }
    }
    unsigned alignment = (unsigned)((uintptr_t)source & 63u);
    __atomic_store_n(&xz_frame_copy_v1[3], alignment, __ATOMIC_RELAXED);
    __atomic_store_n(&xz_frame_copy_v1[2], (native ? 1u : 0u) | (!alignment ? 2u : 0u), __ATOMIC_RELEASE);
    __atomic_add_fetch(&xz_frame_copy_v1[5], 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&xz_frame_copy_v1[native ? 6 : 7], 1u, __ATOMIC_RELAXED);
}
#endif
