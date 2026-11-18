/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "xz_sdk_host_internal.h"

#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mntent.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#if defined(XZ_SDK_HOST_TEST_EXEC) && !defined(__x86_64__)
#error XZ_SDK_HOST_TEST_EXEC is an x86_64 integration-test escape hatch only
#endif

#define XZ_MANIFEST_NAME "xz-package.cfg"
#define XZ_CONFIG_FOLDER "VJ.Tools"
#define XZ_CONFIG_NAME "XZ-SDK.cfg"
#define XZ_CONFIG_BYTES 4096u
#define XZ_ELF_PROGRAM_HEADERS 128u
#define XZ_ELF_DYNAMIC_ENTRIES 2048u
#define XZ_ELF_NEEDED 16u
#define XZ_ELF_VERSION_RECORDS 128u
#define XZ_TMPFS_MAGIC UINT32_C(0x01021994)
#define XZ_RAMFS_MAGIC UINT32_C(0x858458f6)
#define XZ_DIRECTORY_ENTRIES 512u
#define XZ_MOUNT_ENTRIES 1024u
#define XZ_OWNER_NAME ".xz-sdk-owner"
#define XZ_RECOVERY_STAGES 64u
#define XZ_RECOVERY_PROCESSES 4096u

struct stage_owner {
    long host, child;
    unsigned long long host_start, child_start;
    unsigned long long base_device, base_inode, device, inode;
    char entry[XZ_SDK_FILE_NAME];
};

static int cancelled(const struct xz_sdk_scan_spec *spec)
{
    return spec && spec->cancelled && spec->cancelled(spec->cancel_context);
}

static void set_error(char output[XZ_SDK_SCAN_MESSAGE], const char *format, ...)
{
    va_list arguments;
    if (!output) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(output, XZ_SDK_SCAN_MESSAGE, format, arguments);
    va_end(arguments);
    output[XZ_SDK_SCAN_MESSAGE - 1u] = '\0';
}

static int copy_text(char *output, size_t capacity, const char *input)
{
    size_t length;
    if (!output || !capacity || !input) {
        return -1;
    }
    length = strlen(input);
    if (length >= capacity) {
        return -1;
    }
    memcpy(output, input, length + 1u);
    return 0;
}

static int stat_identity_equal(const struct stat *first,
                               const struct stat *second)
{
    return first->st_dev == second->st_dev &&
           first->st_ino == second->st_ino;
}

static int stat_file_unchanged(const struct stat *first,
                               const struct stat *second)
{
    return stat_identity_equal(first, second) &&
           first->st_mode == second->st_mode &&
           first->st_size == second->st_size &&
           first->st_mtime == second->st_mtime &&
           first->st_ctime == second->st_ctime &&
           first->st_mtim.tv_nsec == second->st_mtim.tv_nsec &&
           first->st_ctim.tv_nsec == second->st_ctim.tv_nsec;
}

static int ascii_component_char(unsigned char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') ||
           value == '.' || value == '_' || value == '-';
}

static int id_valid(const char *value)
{
    size_t index;
    size_t length = value ? strlen(value) : 0u;
    if (!length || length >= XZ_SDK_ID) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        if (!ascii_component_char((unsigned char)value[index])) {
            return 0;
        }
    }
    return 1;
}

static int flat_name_valid(const char *value)
{
    size_t index;
    size_t length = value ? strlen(value) : 0u;
    if (!length || length >= XZ_SDK_FILE_NAME || value[0] == '.') {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        if (!ascii_component_char((unsigned char)value[index])) {
            return 0;
        }
    }
    return strcmp(value, XZ_MANIFEST_NAME) != 0;
}

static int relative_path_valid(const char *value)
{
    const char *cursor;
    const char *component;
    size_t length;
    if (!value || !value[0] || value[0] == '/' ||
        strlen(value) >= XZ_SDK_PATH) {
        return 0;
    }
    component = value;
    for (cursor = value;; ++cursor) {
        unsigned char character = (unsigned char)*cursor;
        if (character == '/' || character == '\0') {
            length = (size_t)(cursor - component);
            if (!length || length >= XZ_SDK_FILE_NAME ||
                (length == 1u && component[0] == '.') ||
                (length == 2u && component[0] == '.' && component[1] == '.')) {
                return 0;
            }
            if (!character) {
                return 1;
            }
            component = cursor + 1;
        } else if (character < 0x20u || character > 0x7eu ||
                   character == '\\') {
            return 0;
        }
    }
}

static int duplicate_cloexec(int descriptor)
{
    int copy = dup(descriptor);
    if (copy >= 0) {
        (void)fcntl(copy, F_SETFD, FD_CLOEXEC);
    }
    return copy;
}

static int open_absolute_directory(const char *path, struct stat *identity)
{
    const char *cursor;
    int current;
    if (!path || path[0] != '/' || strlen(path) >= XZ_SDK_PATH) {
        errno = EINVAL;
        return -1;
    }
    current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) {
        return -1;
    }
    cursor = path + 1;
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        char component[XZ_SDK_FILE_NAME];
        int next;
        if (!length) {
            if (slash && slash[1] == '\0') {
                break;
            }
            close(current);
            errno = EINVAL;
            return -1;
        }
        if (length >= sizeof(component) ||
            (length == 1u && cursor[0] == '.') ||
            (length == 2u && cursor[0] == '.' && cursor[1] == '.')) {
            close(current);
            errno = EINVAL;
            return -1;
        }
        memcpy(component, cursor, length);
        component[length] = '\0';
        next = openat(current, component,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) {
            close(current);
            return -1;
        }
        close(current);
        current = next;
        if (!slash || !slash[1]) {
            break;
        }
        cursor = slash + 1;
    }
    if (identity && fstat(current, identity) != 0) {
        close(current);
        return -1;
    }
    return current;
}

static int open_or_create_absolute_directory(const char *path,
                                             struct stat *identity)
{
    const char *cursor;
    int current;
    if (!path || path[0] != '/' || strlen(path) >= XZ_SDK_PATH ||
        strcmp(path, "/") == 0) {
        errno = EINVAL;
        return -1;
    }
    current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) {
        return -1;
    }
    cursor = path + 1;
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        int last = !slash || slash[1] == '\0';
        char component[XZ_SDK_FILE_NAME];
        int next;
        if (!length || length >= sizeof(component) ||
            (length == 1u && cursor[0] == '.') ||
            (length == 2u && cursor[0] == '.' && cursor[1] == '.')) {
            close(current);
            errno = EINVAL;
            return -1;
        }
        memcpy(component, cursor, length);
        component[length] = '\0';
        next = openat(current, component,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0 && errno == ENOENT && last) {
            if (mkdirat(current, component, 0700) != 0 && errno != EEXIST) {
                close(current);
                return -1;
            }
            next = openat(current, component,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (next < 0) {
            close(current);
            return -1;
        }
        close(current);
        current = next;
        if (last) {
            break;
        }
        cursor = slash + 1;
    }
    if (identity && fstat(current, identity) != 0) {
        close(current);
        return -1;
    }
    return current;
}

static int open_relative_directory(int parent, const char *path,
                                   struct stat *identity)
{
    const char *cursor;
    int current;
    if (!relative_path_valid(path)) {
        errno = EINVAL;
        return -1;
    }
    current = duplicate_cloexec(parent);
    if (current < 0) {
        return -1;
    }
    cursor = path;
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
        char component[XZ_SDK_FILE_NAME];
        int next;
        memcpy(component, cursor, length);
        component[length] = '\0';
        next = openat(current, component,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) {
            close(current);
            return -1;
        }
        close(current);
        current = next;
        if (!slash) {
            break;
        }
        cursor = slash + 1;
    }
    if (identity && fstat(current, identity) != 0) {
        close(current);
        return -1;
    }
    return current;
}

static int read_bounded(int descriptor, unsigned char *buffer, size_t capacity,
                        size_t *length)
{
    size_t used = 0;
    for (;;) {
        ssize_t count;
        if (used == capacity) {
            unsigned char extra;
            do {
                count = read(descriptor, &extra, 1u);
            } while (count < 0 && errno == EINTR);
            if (count != 0) {
                return -1;
            }
            break;
        }
        do {
            count = read(descriptor, buffer + used, capacity - used);
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            return -1;
        }
        if (!count) {
            break;
        }
        used += (size_t)count;
    }
    *length = used;
    return 0;
}

static int line_value(const char *line, const char *key, char *output,
                      size_t capacity)
{
    size_t key_length = strlen(key);
    if (strncmp(line, key, key_length) != 0 || !line[key_length]) {
        return -1;
    }
    return copy_text(output, capacity, line + key_length);
}

static char *take_line(char **cursor)
{
    char *line;
    char *newline;
    if (!cursor || !*cursor || !**cursor) {
        return NULL;
    }
    line = *cursor;
    newline = strchr(line, '\n');
    if (!newline) {
        return NULL;
    }
    *newline = '\0';
    *cursor = newline + 1;
    return line;
}

static int parse_hex_digest(const char *text,
                            uint8_t output[XZ_SHA256_BYTES])
{
    size_t index;
    if (!text || strlen(text) != XZ_SHA256_BYTES * 2u) {
        return -1;
    }
    for (index = 0; index < XZ_SHA256_BYTES; ++index) {
        unsigned char high = (unsigned char)text[index * 2u];
        unsigned char low = (unsigned char)text[index * 2u + 1u];
        unsigned high_value;
        unsigned low_value;
        if (high >= '0' && high <= '9') {
            high_value = (unsigned)(high - '0');
        } else if (high >= 'a' && high <= 'f') {
            high_value = (unsigned)(high - 'a' + 10);
        } else {
            return -1;
        }
        if (low >= '0' && low <= '9') {
            low_value = (unsigned)(low - '0');
        } else if (low >= 'a' && low <= 'f') {
            low_value = (unsigned)(low - 'a' + 10);
        } else {
            return -1;
        }
        output[index] = (uint8_t)((high_value << 4) | low_value);
    }
    return 0;
}

static int manifest_parse(unsigned char *bytes, size_t length,
                          struct xz_sdk_manifest *manifest,
                          char error[XZ_SDK_SCAN_MESSAGE])
{
    char *cursor;
    char *line;
    char value[XZ_SDK_FILE_NAME + 65u];
    size_t index;
    if (!length || bytes[length - 1u] != '\n') {
        set_error(error, "manifest must end with newline");
        return -1;
    }
    for (index = 0; index < length; ++index) {
        if ((bytes[index] < 0x20u || bytes[index] > 0x7eu) &&
            bytes[index] != '\n') {
            set_error(error, "manifest is not canonical ASCII");
            return -1;
        }
    }
    bytes[length] = '\0';
    memset(manifest, 0, sizeof(*manifest));
    cursor = (char *)bytes;
    line = take_line(&cursor);
    if (!line || strcmp(line, "XZ_SDK_PACKAGE 1") != 0) {
        set_error(error, "bad package header");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "id=", manifest->id,
                            sizeof(manifest->id)) != 0 ||
        !id_valid(manifest->id)) {
        set_error(error, "invalid id field");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "name=", manifest->name,
                            sizeof(manifest->name)) != 0 ||
        !manifest->name[0]) {
        set_error(error, "invalid name field");
        return -1;
    }
    for (index = 0; manifest->name[index]; ++index) {
        unsigned char character = (unsigned char)manifest->name[index];
        if (character < 0x20u || character > 0x7eu) {
            set_error(error, "invalid name field");
            return -1;
        }
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "version=", manifest->version,
                            sizeof(manifest->version)) != 0 ||
        !manifest->version[0]) {
        set_error(error, "invalid version field");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "kind=", value, sizeof(value)) != 0) {
        set_error(error, "invalid kind field");
        return -1;
    }
    if (strcmp(value, "application") == 0) {
        manifest->kind = XZ_SDK_APPLICATION;
    } else if (strcmp(value, "extension") == 0) {
        manifest->kind = XZ_SDK_EXTENSION;
    } else if (strcmp(value, "profile") == 0) {
        manifest->kind = XZ_SDK_PROFILE;
    } else {
        set_error(error, "unknown package kind");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || strcmp(line, "firmware=1.26") != 0) {
        set_error(error, "firmware must be 1.26");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || strcmp(line, "api=1") != 0) {
        set_error(error, "api must be 1");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "entry=", manifest->entry,
                            sizeof(manifest->entry)) != 0 ||
        !flat_name_valid(manifest->entry)) {
        set_error(error, "invalid entry field");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "autoload=", value, sizeof(value)) != 0 ||
        (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) {
        set_error(error, "invalid autoload field");
        return -1;
    }
    manifest->autoload = (uint32_t)(value[0] == '1');

    while ((line = take_line(&cursor)) != NULL) {
        char *separator;
        struct xz_sdk_file_record *file;
        uint32_t previous;
        if (!line[0]) {
            set_error(error, "blank manifest line");
            return -1;
        }
        if (strncmp(line, "file=", 5u) != 0 ||
            manifest->file_count >= XZ_SDK_FILES) {
            set_error(error, "invalid or excess file line");
            return -1;
        }
        separator = strchr(line + 5u, ':');
        if (!separator || strchr(separator + 1, ':')) {
            set_error(error, "malformed file line");
            return -1;
        }
        *separator = '\0';
        file = &manifest->files[manifest->file_count];
        if (!flat_name_valid(line + 5u) ||
            copy_text(file->name, sizeof(file->name), line + 5u) != 0 ||
            parse_hex_digest(separator + 1, file->digest) != 0) {
            set_error(error, "invalid file name or digest");
            return -1;
        }
        for (previous = 0; previous < manifest->file_count; ++previous) {
            if (strcmp(manifest->files[previous].name, file->name) == 0) {
                set_error(error, "duplicate file entry");
                return -1;
            }
        }
        ++manifest->file_count;
    }
    if (*cursor || !manifest->file_count) {
        set_error(error, "manifest has trailing or no file records");
        return -1;
    }
    for (index = 0; index < manifest->file_count; ++index) {
        if (strcmp(manifest->files[index].name, manifest->entry) == 0) {
            return 0;
        }
    }
    set_error(error, "entry is not listed as payload");
    return -1;
}

static int pread_exact(int descriptor, void *buffer, size_t length,
                       off_t offset)
{
    size_t used = 0;
    while (used < length) {
        ssize_t count;
        do {
            count = pread(descriptor, (unsigned char *)buffer + used,
                          length - used, offset + (off_t)used);
        } while (count < 0 && errno == EINTR);
        if (count <= 0) {
            return -1;
        }
        used += (size_t)count;
    }
    return 0;
}

static int file_range_valid(uint64_t offset, uint64_t length, uint64_t size)
{
    return offset <= size && length <= size - offset;
}

static int elf_virtual_to_file(const Elf32_Phdr *headers, uint16_t count,
                               uint32_t address, uint32_t length,
                               uint32_t *offset)
{
    uint16_t index;
    for (index = 0; index < count; ++index) {
        uint64_t delta;
        if (headers[index].p_type != PT_LOAD || address < headers[index].p_vaddr) {
            continue;
        }
        delta = (uint64_t)address - headers[index].p_vaddr;
        if (delta <= headers[index].p_filesz &&
            length <= headers[index].p_filesz - delta &&
            (uint64_t)headers[index].p_offset + delta <= UINT32_MAX) {
            *offset = headers[index].p_offset + (uint32_t)delta;
            return 0;
        }
    }
    return -1;
}

static int elf_read_string(int descriptor, uint64_t file_size,
                           uint32_t table_offset, uint32_t table_size,
                           uint32_t string_offset, char *output,
                           size_t capacity)
{
    size_t used = 0;
    if (string_offset >= table_size || !capacity) {
        return -1;
    }
    while (used + 1u < capacity && string_offset + used < table_size) {
        char character;
        uint64_t offset = (uint64_t)table_offset + string_offset + used;
        if (!file_range_valid(offset, 1u, file_size) ||
            pread_exact(descriptor, &character, 1u, (off_t)offset) != 0) {
            return -1;
        }
        output[used++] = character;
        if (!character) {
            return 0;
        }
    }
    return -1;
}

static int allowed_library(const char *name)
{
    static const char *const allowed[] = {
        "libc.so.6", "libm.so.6", "libpthread.so.0", "librt.so.1", "libdl.so.2", "ld-linux.so.3"
    };
    size_t index;
    for (index = 0; index < sizeof(allowed) / sizeof(allowed[0]); ++index) {
        if (strcmp(name, allowed[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int glibc_version_allowed(const char *name)
{
    const char *cursor;
    unsigned major = 0;
    unsigned minor = 0;
    int extra_nonzero = 0;
    if (strncmp(name, "GLIBC_", 6u) != 0) {
        return 1;
    }
    cursor = name + 6u;
    if (*cursor < '0' || *cursor > '9') {
        return 0;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        if (major > 100u) {
            return 0;
        }
        major = major * 10u + (unsigned)(*cursor++ - '0');
    }
    if (*cursor++ != '.' || *cursor < '0' || *cursor > '9') {
        return 0;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        if (minor > 1000u) {
            return 0;
        }
        minor = minor * 10u + (unsigned)(*cursor++ - '0');
    }
    while (*cursor) {
        unsigned component = 0;
        if (*cursor++ != '.' || *cursor < '0' || *cursor > '9') {
            return 0;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            if (component > 1000u) {
                return 0;
            }
            component = component * 10u + (unsigned)(*cursor++ - '0');
        }
        if (component) {
            extra_nonzero = 1;
        }
    }
    return major < 2u ||
           (major == 2u &&
            (minor < 13u || (minor == 13u && !extra_nonzero)));
}

static int validate_arm_elf(int descriptor, const struct stat *status,
                            char error[XZ_SDK_SCAN_MESSAGE])
{
    Elf32_Ehdr header;
    Elf32_Phdr programs[XZ_ELF_PROGRAM_HEADERS];
    uint16_t index;
    int dynamic_index = -1;
    uint32_t string_address = 0;
    uint32_t string_size = 0;
    uint32_t version_address = 0;
    uint32_t version_count = 0;
    uint32_t needed[XZ_ELF_NEEDED];
    uint32_t needed_count = 0;
    uint32_t string_offset;
    uint32_t version_offset;
    uint64_t file_size = (uint64_t)status->st_size;

    if (status->st_size < (off_t)sizeof(header) ||
        pread_exact(descriptor, &header, sizeof(header), 0) != 0 ||
        memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS32 ||
        header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_VERSION] != EV_CURRENT ||
        header.e_version != EV_CURRENT || header.e_machine != EM_ARM ||
        header.e_ehsize != sizeof(Elf32_Ehdr) ||
        (header.e_type != ET_EXEC && header.e_type != ET_DYN)) {
        set_error(error, "entry is not ELF32 little-endian ARM");
        return -1;
    }
    if ((header.e_flags & 0x600u) != 0x200u) {
        set_error(error, "entry lacks ARM soft-float ABI flag");
        return -1;
    }
    if (!header.e_entry) {
        set_error(error, "entry has no executable entry point");
        return -1;
    }
    if (!header.e_phnum || header.e_phnum > XZ_ELF_PROGRAM_HEADERS ||
        header.e_phentsize != sizeof(Elf32_Phdr) ||
        !file_range_valid(header.e_phoff,
                          (uint64_t)header.e_phnum * sizeof(Elf32_Phdr),
                          file_size) ||
        pread_exact(descriptor, programs,
                    (size_t)header.e_phnum * sizeof(Elf32_Phdr),
                    (off_t)header.e_phoff) != 0) {
        set_error(error, "invalid ELF program headers");
        return -1;
    }
    for (index = 0; index < header.e_phnum; ++index) {
        if (programs[index].p_type == PT_DYNAMIC) {
            if (dynamic_index >= 0) {
                set_error(error, "multiple ELF dynamic tables");
                return -1;
            }
            dynamic_index = (int)index;
        }
        if (programs[index].p_filesz > programs[index].p_memsz ||
            !file_range_valid(programs[index].p_offset,
                              programs[index].p_filesz, file_size)) {
            set_error(error, "ELF segment exceeds entry file");
            return -1;
        }
    }
    if (dynamic_index < 0) {
        return 0;
    }
    {
        const Elf32_Phdr *dynamic_program = &programs[dynamic_index];
        uint32_t entries = dynamic_program->p_filesz / sizeof(Elf32_Dyn);
        uint32_t entry_index;
        int terminated = 0;
        if (dynamic_program->p_filesz % sizeof(Elf32_Dyn) != 0 ||
            entries > XZ_ELF_DYNAMIC_ENTRIES) {
            set_error(error, "invalid or excessive ELF dynamic table");
            return -1;
        }
        for (entry_index = 0; entry_index < entries; ++entry_index) {
            Elf32_Dyn dynamic;
            off_t offset = (off_t)dynamic_program->p_offset +
                           (off_t)entry_index * (off_t)sizeof(dynamic);
            if (pread_exact(descriptor, &dynamic, sizeof(dynamic), offset) != 0) {
                set_error(error, "cannot read ELF dynamic table");
                return -1;
            }
            if (dynamic.d_tag == DT_NULL) {
                terminated = 1;
                break;
            }
            if (dynamic.d_tag == DT_STRTAB) {
                string_address = dynamic.d_un.d_ptr;
            } else if (dynamic.d_tag == DT_STRSZ) {
                string_size = dynamic.d_un.d_val;
            } else if (dynamic.d_tag == DT_NEEDED) {
                if (needed_count >= XZ_ELF_NEEDED) {
                    set_error(error, "too many ELF dependencies");
                    return -1;
                }
                needed[needed_count++] = dynamic.d_un.d_val;
            } else if (dynamic.d_tag == DT_VERNEED) {
                version_address = dynamic.d_un.d_ptr;
            } else if (dynamic.d_tag == DT_VERNEEDNUM) {
                version_count = dynamic.d_un.d_val;
            } else if (dynamic.d_tag == DT_TEXTREL) {
                set_error(error, "ELF text relocations are not allowed");
                return -1;
            }
        }
        if (!terminated) {
            set_error(error, "unterminated ELF dynamic table");
            return -1;
        }
    }
    if ((!string_address || !string_size || string_size > 1024u * 1024u) &&
        (needed_count || version_count)) {
        set_error(error, "invalid ELF dynamic string table");
        return -1;
    }
    if (string_size &&
        (elf_virtual_to_file(programs, header.e_phnum, string_address,
                             string_size, &string_offset) != 0 ||
         !file_range_valid(string_offset, string_size, file_size))) {
        set_error(error, "ELF string table is outside load segments");
        return -1;
    }
    for (index = 0; index < needed_count; ++index) {
        char library[128];
        if (elf_read_string(descriptor, file_size, string_offset, string_size,
                            needed[index], library, sizeof(library)) != 0 ||
            !allowed_library(library)) {
            set_error(error, "ELF dependency is not allowed");
            return -1;
        }
    }
    if (!version_count) {
        return 0;
    }
    if (!version_address || version_count > XZ_ELF_VERSION_RECORDS ||
        elf_virtual_to_file(programs, header.e_phnum, version_address,
                            sizeof(Elf32_Verneed), &version_offset) != 0) {
        set_error(error, "invalid ELF version requirements");
        return -1;
    }
    {
        uint32_t current = version_offset;
        uint32_t record;
        for (record = 0; record < version_count; ++record) {
            Elf32_Verneed need;
            uint32_t auxiliary;
            uint32_t auxiliary_index;
            if (!file_range_valid(current, sizeof(need), file_size) ||
                pread_exact(descriptor, &need, sizeof(need), current) != 0 ||
                need.vn_version != VER_NEED_CURRENT || !need.vn_cnt ||
                need.vn_cnt > XZ_ELF_VERSION_RECORDS ||
                !need.vn_aux) {
                set_error(error, "malformed ELF version record");
                return -1;
            }
            if ((uint64_t)current + need.vn_aux > UINT32_MAX) {
                set_error(error, "ELF version record overflow");
                return -1;
            }
            auxiliary = current + need.vn_aux;
            for (auxiliary_index = 0; auxiliary_index < need.vn_cnt;
                 ++auxiliary_index) {
                Elf32_Vernaux aux;
                char version[64];
                if (!file_range_valid(auxiliary, sizeof(aux), file_size) ||
                    pread_exact(descriptor, &aux, sizeof(aux), auxiliary) != 0 ||
                    elf_read_string(descriptor, file_size, string_offset,
                                    string_size, aux.vna_name, version,
                                    sizeof(version)) != 0 ||
                    !glibc_version_allowed(version)) {
                    set_error(error, "ELF requires GLIBC newer than 2.13");
                    return -1;
                }
                if (auxiliary_index + 1u < need.vn_cnt) {
                    if (!aux.vna_next ||
                        (uint64_t)auxiliary + aux.vna_next > UINT32_MAX) {
                        set_error(error, "malformed ELF auxiliary versions");
                        return -1;
                    }
                    auxiliary += aux.vna_next;
                }
            }
            if (record + 1u < version_count) {
                if (!need.vn_next ||
                    (uint64_t)current + need.vn_next > UINT32_MAX) {
                    set_error(error, "malformed ELF version chain");
                    return -1;
                }
                current += need.vn_next;
            }
        }
    }
    return 0;
}

static int validate_executable(int descriptor, const struct stat *status,
                               char error[XZ_SDK_SCAN_MESSAGE])
{
#ifdef XZ_SDK_HOST_TEST_EXEC
    /*
     * TEST-ONLY ESCAPE HATCH. Production builds do not contain this branch.
     * It permits native x86_64 ELF fixtures so integration tests can exercise
     * real posix_spawn/reap behavior without weakening ARM package checks.
     */
    {
        Elf64_Ehdr host;
        if (status->st_size >= (off_t)sizeof(host) &&
            pread_exact(descriptor, &host, sizeof(host), 0) == 0 &&
            memcmp(host.e_ident, ELFMAG, SELFMAG) == 0 &&
            host.e_ident[EI_CLASS] == ELFCLASS64 &&
            host.e_ident[EI_DATA] == ELFDATA2LSB &&
            host.e_machine == EM_X86_64 &&
            (host.e_type == ET_EXEC || host.e_type == ET_DYN)) {
            return 0;
        }
    }
#endif
    return validate_arm_elf(descriptor, status, error);
}

static int hash_open_payload(int package_fd, const char *name,
                             uint8_t digest[XZ_SHA256_BYTES],
                             uint64_t *total,
                             const struct xz_sdk_scan_spec *spec,
                             char error[XZ_SDK_SCAN_MESSAGE])
{
    int descriptor;
    struct stat before;
    struct stat after;
    struct stat entry_after;
    struct xz_sha256_context hash;
    unsigned char buffer[16384];
    uint64_t used = 0;
    descriptor = openat(package_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 || fstat(descriptor, &before) != 0 ||
        !S_ISREG(before.st_mode) || before.st_size < 0 ||
        (uint64_t)before.st_size > XZ_SDK_PACKAGE_BYTES ||
        *total > XZ_SDK_PACKAGE_BYTES - (uint64_t)before.st_size) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        set_error(error, "payload is missing, linked, or too large");
        return -1;
    }
    xz_sha256_init(&hash);
    for (;;) {
        ssize_t count;
        if (cancelled(spec)) {
            close(descriptor);
            set_error(error, "scan cancelled");
            return -1;
        }
        do {
            count = read(descriptor, buffer, sizeof(buffer));
        } while (count < 0 && errno == EINTR);
        if (count < 0 || (count == 0 && used != (uint64_t)before.st_size) ||
            (count > 0 &&
             (uint64_t)count > (uint64_t)before.st_size - used)) {
            close(descriptor);
            set_error(error, "payload changed while hashing");
            return -1;
        }
        if (!count) {
            break;
        }
        xz_sha256_update(&hash, buffer, (size_t)count);
        used += (uint64_t)count;
    }
    if (fstat(descriptor, &after) != 0 ||
        fstatat(package_fd, name, &entry_after, AT_SYMLINK_NOFOLLOW) != 0 ||
        !stat_file_unchanged(&before, &after) ||
        !stat_file_unchanged(&before, &entry_after)) {
        close(descriptor);
        set_error(error, "payload identity changed while hashing");
        return -1;
    }
    close(descriptor);
    xz_sha256_final(&hash, digest);
    *total += used;
    return 0;
}

static int validate_directory_closure(int package_fd,
                                      const struct xz_sdk_manifest *manifest,
                                      char error[XZ_SDK_SCAN_MESSAGE])
{
    int copy = duplicate_cloexec(package_fd);
    DIR *directory;
    struct dirent *entry;
    uint32_t found[XZ_SDK_FILES];
    uint32_t entries = 0;
    uint32_t index;
    memset(found, 0, sizeof(found));
    if (copy < 0 || !(directory = fdopendir(copy))) {
        if (copy >= 0) {
            close(copy);
        }
        set_error(error, "cannot enumerate package closure");
        return -1;
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        struct stat status;
        int matched = -1;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        ++entries;
        if (entries > XZ_SDK_FILES + 1u ||
            fstatat(package_fd, entry->d_name, &status,
                    AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(status.st_mode)) {
            closedir(directory);
            set_error(error, "package contains link, directory, or too many files");
            return -1;
        }
        if (strcmp(entry->d_name, XZ_MANIFEST_NAME) == 0) {
            continue;
        }
        for (index = 0; index < manifest->file_count; ++index) {
            if (strcmp(entry->d_name, manifest->files[index].name) == 0) {
                matched = (int)index;
                break;
            }
        }
        if (matched < 0 || found[matched]) {
            closedir(directory);
            set_error(error, "package contains unlisted or duplicate payload");
            return -1;
        }
        found[matched] = 1u;
    }
    if (errno) {
        closedir(directory);
        set_error(error, "package enumeration failed");
        return -1;
    }
    closedir(directory);
    if (entries != manifest->file_count + 1u) {
        set_error(error, "package payload closure is incomplete");
        return -1;
    }
    for (index = 0; index < manifest->file_count; ++index) {
        if (!found[index]) {
            set_error(error, "listed payload is absent");
            return -1;
        }
    }
    return 0;
}

static int validate_profile(int package_fd, const char *entry,
                            char error[XZ_SDK_SCAN_MESSAGE])
{
    int descriptor = openat(package_fd, entry,
                            O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    unsigned char record[XZ_SDK_PROFILE_BYTES + 1u];
    size_t length;
    size_t index;
    struct stat before;
    struct stat after;
    struct stat entry_after;
    if (descriptor < 0 || fstat(descriptor, &before) != 0 ||
        !S_ISREG(before.st_mode) || before.st_size < 0 ||
        before.st_size > XZ_SDK_PROFILE_BYTES ||
        read_bounded(descriptor, record, XZ_SDK_PROFILE_BYTES, &length) != 0) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        set_error(error, "profile record exceeds 2048 bytes");
        return -1;
    }
    if (fstat(descriptor, &after) != 0 ||
        fstatat(package_fd, entry, &entry_after, AT_SYMLINK_NOFOLLOW) != 0 ||
        !stat_file_unchanged(&before, &after) ||
        !stat_file_unchanged(&before, &entry_after)) {
        close(descriptor);
        set_error(error, "profile record changed while reading");
        return -1;
    }
    close(descriptor);
    for (index = 0; index < length; ++index) {
        if (!record[index]) {
            set_error(error, "profile record contains NUL");
            return -1;
        }
    }
    record[length] = '\0';
    if (length < strlen("XZ_MODS_SETTINGS 1\n") ||
        memcmp(record, "XZ_MODS_SETTINGS 1\n",
               strlen("XZ_MODS_SETTINGS 1\n")) != 0) {
        set_error(error, "profile record has wrong header");
        return -1;
    }
    return 0;
}

static int validate_package_fd(int package_fd,
                               struct xz_sdk_package_record *package,
                               const struct xz_sdk_scan_spec *spec,
                               char error[XZ_SDK_SCAN_MESSAGE])
{
    int manifest_fd = -1;
    int entry_fd = -1;
    struct stat directory_before;
    struct stat directory_after;
    struct stat manifest_before;
    struct stat manifest_after;
    struct stat manifest_entry_after;
    unsigned char manifest_bytes[XZ_SDK_MANIFEST_BYTES + 1u];
    unsigned char manifest_for_hash[XZ_SDK_MANIFEST_BYTES];
    size_t manifest_length;
    uint64_t total;
    uint32_t index;
    struct xz_sha256_context content;
    uint8_t actual[XZ_SHA256_BYTES];

    memset(package, 0, sizeof(*package));
    if (fstat(package_fd, &directory_before) != 0 ||
        !S_ISDIR(directory_before.st_mode)) {
        set_error(error, "package directory is unavailable");
        return -1;
    }
    manifest_fd = openat(package_fd, XZ_MANIFEST_NAME,
                         O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (manifest_fd < 0 || fstat(manifest_fd, &manifest_before) != 0 ||
        !S_ISREG(manifest_before.st_mode) || manifest_before.st_size < 0 ||
        (uint64_t)manifest_before.st_size > XZ_SDK_MANIFEST_BYTES ||
        read_bounded(manifest_fd, manifest_bytes, XZ_SDK_MANIFEST_BYTES,
                     &manifest_length) != 0 ||
        fstat(manifest_fd, &manifest_after) != 0 ||
        fstatat(package_fd, XZ_MANIFEST_NAME, &manifest_entry_after,
                AT_SYMLINK_NOFOLLOW) != 0 ||
        !stat_file_unchanged(&manifest_before, &manifest_after) ||
        !stat_file_unchanged(&manifest_before, &manifest_entry_after)) {
        if (manifest_fd >= 0) {
            close(manifest_fd);
        }
        set_error(error, "manifest is missing, linked, changed, or oversized");
        return -1;
    }
    close(manifest_fd);
    memcpy(manifest_for_hash, manifest_bytes, manifest_length);
    if (manifest_parse(manifest_bytes, manifest_length, &package->manifest,
                       error) != 0) {
        return -1;
    }
    total = manifest_length;
    xz_sha256_init(&content);
    xz_sha256_update(&content, manifest_for_hash, manifest_length);
    for (index = 0; index < package->manifest.file_count; ++index) {
        if (hash_open_payload(package_fd, package->manifest.files[index].name,
                              actual, &total, spec, error) != 0) {
            return -1;
        }
        if (memcmp(actual, package->manifest.files[index].digest,
                   XZ_SHA256_BYTES) != 0) {
            set_error(error, "payload SHA256 mismatch");
            return -1;
        }
        xz_sha256_update(&content, package->manifest.files[index].name,
                         strlen(package->manifest.files[index].name) + 1u);
        xz_sha256_update(&content, actual, sizeof(actual));
    }
    if (validate_directory_closure(package_fd, &package->manifest, error) != 0) {
        return -1;
    }
    if (package->manifest.kind == XZ_SDK_PROFILE) {
        if (validate_profile(package_fd, package->manifest.entry, error) != 0) {
            return -1;
        }
    } else {
        entry_fd = openat(package_fd, package->manifest.entry,
                          O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (entry_fd < 0 || fstat(entry_fd, &manifest_before) != 0 ||
            !S_ISREG(manifest_before.st_mode) ||
            validate_executable(entry_fd, &manifest_before, error) != 0 ||
            fstat(entry_fd, &manifest_after) != 0 ||
            fstatat(package_fd, package->manifest.entry,
                    &manifest_entry_after, AT_SYMLINK_NOFOLLOW) != 0 ||
            !stat_file_unchanged(&manifest_before, &manifest_after) ||
            !stat_file_unchanged(&manifest_before, &manifest_entry_after)) {
            if (entry_fd >= 0) {
                close(entry_fd);
            }
            if (!error[0]) {
                set_error(error, "entry executable is invalid");
            }
            return -1;
        }
        close(entry_fd);
    }
    if (fstat(package_fd, &directory_after) != 0 ||
        !stat_file_unchanged(&directory_before, &directory_after)) {
        set_error(error, "package directory changed during validation");
        return -1;
    }
    xz_sha256_final(&content, package->content_digest);
    package->valid = 1;
    copy_text(package->view.id, sizeof(package->view.id), package->manifest.id);
    copy_text(package->view.name, sizeof(package->view.name), package->manifest.name);
    copy_text(package->view.version, sizeof(package->view.version),
              package->manifest.version);
    package->view.kind = package->manifest.kind;
    package->view.state = XZ_SDK_READY;
    package->view.autoload = package->manifest.autoload;
    copy_text(package->view.detail, sizeof(package->view.detail), "ready");
    package->source.package_device = directory_before.st_dev;
    package->source.package_inode = directory_before.st_ino;
    return 0;
}

static void safe_display_name(char output[XZ_SDK_NAME], const char *input)
{
    size_t used = 0;
    while (input && *input && used + 1u < XZ_SDK_NAME) {
        unsigned char value = (unsigned char)*input++;
        output[used++] = (char)((value >= 0x20u && value <= 0x7eu) ? value : '?');
    }
    output[used] = '\0';
    if (!used) {
        copy_text(output, XZ_SDK_NAME, "Invalid package");
    }
}

static void invalid_package(struct xz_sdk_package_record *package,
                            const char *folder_name, uint32_t ordinal,
                            const char *reason, int automatic)
{
    char identifier[XZ_SDK_ID];
    if (!package->view.id[0] && package->manifest.id[0]) {
        copy_text(package->view.id, sizeof(package->view.id),
                  package->manifest.id);
    }
    if (!package->view.name[0] && package->manifest.name[0]) {
        copy_text(package->view.name, sizeof(package->view.name),
                  package->manifest.name);
    }
    if (!package->view.version[0] && package->manifest.version[0]) {
        copy_text(package->view.version, sizeof(package->view.version),
                  package->manifest.version);
    }
    if (!package->view.kind && package->manifest.kind) {
        package->view.kind = package->manifest.kind;
    }
    if (!package->view.id[0]) {
        (void)snprintf(identifier, sizeof(identifier), "invalid-%u", ordinal);
        copy_text(package->view.id, sizeof(package->view.id), identifier);
    }
    if (!package->view.name[0]) {
        safe_display_name(package->view.name, folder_name);
    }
    if (!package->view.version[0]) {
        copy_text(package->view.version, sizeof(package->view.version), "-");
    }
    package->view.state = XZ_SDK_FAILED;
    package->view.autoload = (uint32_t)automatic;
    copy_text(package->view.detail, sizeof(package->view.detail), reason);
    package->valid = 0;
    package->automatic = automatic;
}

static int package_name_compare(const void *left, const void *right)
{
    return strcmp((const char *)left, (const char *)right);
}

static int load_root_config(int root_fd, const char *default_applications,
                            const char *default_autoload, char *applications,
                            char *autoload, int *enabled,
                            char error[XZ_SDK_SCAN_MESSAGE])
{
    int config_directory;
    int descriptor;
    struct stat before;
    struct stat after;
    struct stat entry_after;
    unsigned char bytes[XZ_CONFIG_BYTES + 1u];
    size_t length;
    char *cursor;
    char *line;
    char value[XZ_SDK_PATH];
    if (copy_text(applications, XZ_SDK_PATH, default_applications) != 0 ||
        copy_text(autoload, XZ_SDK_PATH, default_autoload) != 0) {
        set_error(error, "default folder path is too long");
        return -1;
    }
    *enabled = 1;
    config_directory = openat(root_fd, XZ_CONFIG_FOLDER,
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (config_directory < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        set_error(error, "config folder is linked or unreadable");
        return -1;
    }
    descriptor = openat(config_directory, XZ_CONFIG_NAME,
                        O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        close(config_directory);
        if (errno == ENOENT) {
            return 0;
        }
        set_error(error, "config file is linked or unreadable");
        return -1;
    }
    if (fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size < 0 || (uint64_t)before.st_size > XZ_CONFIG_BYTES ||
        read_bounded(descriptor, bytes, XZ_CONFIG_BYTES, &length) != 0 ||
        fstat(descriptor, &after) != 0 ||
        fstatat(config_directory, XZ_CONFIG_NAME, &entry_after,
                AT_SYMLINK_NOFOLLOW) != 0 ||
        !stat_file_unchanged(&before, &after) ||
        !stat_file_unchanged(&before, &entry_after)) {
        close(descriptor);
        close(config_directory);
        set_error(error, "config is not a bounded regular file");
        return -1;
    }
    close(descriptor);
    close(config_directory);
    if (!length || bytes[length - 1u] != '\n') {
        set_error(error, "config must end with newline");
        return -1;
    }
    {
        size_t index;
        for (index = 0; index < length; ++index) {
            if ((bytes[index] < 0x20u || bytes[index] > 0x7eu) &&
                bytes[index] != '\n') {
                set_error(error, "config is not canonical ASCII");
                return -1;
            }
        }
    }
    bytes[length] = '\0';
    cursor = (char *)bytes;
    line = take_line(&cursor);
    if (!line || strcmp(line, "XZ_SDK_CONFIG 1") != 0) {
        set_error(error, "bad config header");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "applications=", value, sizeof(value)) != 0 ||
        !relative_path_valid(value) ||
        copy_text(applications, XZ_SDK_PATH, value) != 0) {
        set_error(error, "invalid applications config path");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "autoload=", value, sizeof(value)) != 0 ||
        !relative_path_valid(value) ||
        copy_text(autoload, XZ_SDK_PATH, value) != 0) {
        set_error(error, "invalid autoload config path");
        return -1;
    }
    line = take_line(&cursor);
    if (!line || line_value(line, "enabled=", value, sizeof(value)) != 0 ||
        (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) ||
        *cursor) {
        set_error(error, "invalid enabled config field");
        return -1;
    }
    *enabled = value[0] == '1';
    return 1;
}

static void scan_warning(struct xz_sdk_scan_result *result, const char *message)
{
    ++result->warning_count;
    if (!result->message[0]) {
        copy_text(result->message, sizeof(result->message), message);
    }
}

/* Loose files, symlinks and AppleDouble entries are never package candidates. */
static uint32_t directory_packages(int folder_fd,
                                   char names[XZ_SDK_PACKAGES][XZ_SDK_FILE_NAME],
                                   int *limited,
                                   const struct xz_sdk_scan_spec *spec)
{
    int copy = duplicate_cloexec(folder_fd);
    DIR *directory = copy >= 0 ? fdopendir(copy) : NULL;
    struct dirent *entry;
    uint32_t count = 0;
    unsigned visited = 0;
    if (!directory) {
        if (copy >= 0) close(copy);
        *limited = 1;
        return 0;
    }
    while (!cancelled(spec) && (entry = readdir(directory)) != NULL) {
        struct stat status;
        if (++visited > XZ_DIRECTORY_ENTRIES) {
            *limited = 1;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strncmp(entry->d_name, "._", 2u)) continue;
        if (fstatat(folder_fd, entry->d_name, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISDIR(status.st_mode)) continue;
        if (count == XZ_SDK_PACKAGES || strlen(entry->d_name) >= XZ_SDK_FILE_NAME) {
            *limited = 1;
            break;
        }
        copy_text(names[count++], XZ_SDK_FILE_NAME, entry->d_name);
    }
    closedir(directory);
    qsort(names, count, sizeof(names[0]), package_name_compare);
    return count;
}

struct expanded_roots {
    char paths[XZ_SDK_ROOTS][XZ_SDK_PATH];
    dev_t devices[XZ_SDK_ROOTS];
    ino_t inodes[XZ_SDK_ROOTS];
    unsigned count;
    int limited;
};

static void add_root(struct expanded_roots *roots, const char *path)
{
    struct stat status;
    unsigned index;
    int descriptor = open_absolute_directory(path, &status);
    if (descriptor < 0) return;
    close(descriptor);
    for (index = 0; index < roots->count; ++index) {
        if (roots->devices[index] == status.st_dev &&
            roots->inodes[index] == status.st_ino) return;
    }
    if (roots->count == XZ_SDK_ROOTS) {
        roots->limited = 1;
        return;
    }
    index = roots->count++;
    copy_text(roots->paths[index], XZ_SDK_PATH, path);
    roots->devices[index] = status.st_dev;
    roots->inodes[index] = status.st_ino;
}

/* Both discovery and the insertion probe use this exact expansion. Only
 * immediate, verified mount children replace a configured root container. */
static void expand_roots(const struct xz_sdk_scan_spec *spec,
                         struct expanded_roots *roots)
{
    unsigned index;
    memset(roots, 0, sizeof(*roots));
    for (index = 0; index < spec->root_count && !cancelled(spec); ++index) {
        const char *root = spec->roots[index];
        const char *mount_path = "/proc/mounts";
        FILE *mounts;
        struct mntent *mount;
        size_t length;
        unsigned visited = 0;
        int has_partition = 0;
        if (!root || !root[0]) continue;
        length = strlen(root);
        while (length > 1u && root[length - 1u] == '/') --length;
#ifdef XZ_SDK_HOST_TEST_EXEC
        if (getenv("XZ_SDK_TEST_MOUNTS")) mount_path = getenv("XZ_SDK_TEST_MOUNTS");
#endif
        mounts = setmntent(mount_path, "r");
        if (mounts) {
            while (!cancelled(spec) && visited++ < XZ_MOUNT_ENTRIES &&
                   (mount = getmntent(mounts)) != NULL) {
                const char *path = mount->mnt_dir;
                const char *child;
                struct stat status;
                int descriptor;
                if (strncmp(path, root, length) || path[length] != '/') continue;
                child = path + length + 1u;
                if (!flat_name_valid(child) || strchr(child, '/') ||
                    strlen(path) >= XZ_SDK_PATH) continue;
                descriptor = open_absolute_directory(path, &status);
                if (descriptor < 0) continue;
                close(descriptor);
                has_partition = 1;
                add_root(roots, path);
            }
            if (visited > XZ_MOUNT_ENTRIES) roots->limited = 1;
            endmntent(mounts);
        }
        if (!has_partition) add_root(roots, root);
    }
}

static void scan_folder(struct xz_sdk_scan_result *result, int root_fd,
                        const char *root_path, const struct stat *root_identity,
                        const char *folder_path, int from_autoload,
                        int root_enabled, uint32_t budget,
                        const struct xz_sdk_scan_spec *spec)
{
    int folder_fd;
    struct stat folder_before;
    struct stat folder_after;
    char names[XZ_SDK_PACKAGES][XZ_SDK_FILE_NAME];
    uint32_t name_count = 0;
    int limited = 0;
    uint32_t index;
    folder_fd = open_relative_directory(root_fd, folder_path, &folder_before);
    if (folder_fd < 0) {
        if (errno != ENOENT) {
            scan_warning(result, "configured folder rejected (link or unreadable)");
        }
        return;
    }
    name_count = directory_packages(folder_fd, names, &limited, spec);
    result->candidates_seen += name_count;
    if (limited || name_count > budget) result->limit_reached = 1u;
    for (index = 0; index < name_count && index < budget &&
         result->count < XZ_SDK_PACKAGES && !cancelled(spec);
         ++index) {
        struct xz_sdk_package_record *package =
            &result->packages[result->count];
        struct stat entry_status;
        int package_fd = -1;
        char error[XZ_SDK_SCAN_MESSAGE] = {0};
        memset(package, 0, sizeof(*package));
        copy_text(package->source.root, sizeof(package->source.root), root_path);
        copy_text(package->source.folder, sizeof(package->source.folder),
                  folder_path);
        copy_text(package->source.package, sizeof(package->source.package),
                  names[index]);
        package->source.root_device = root_identity->st_dev;
        package->source.root_inode = root_identity->st_ino;
        package->source.folder_device = folder_before.st_dev;
        package->source.folder_inode = folder_before.st_ino;
        package->root_autoload_enabled = root_enabled;
        if (fstatat(folder_fd, names[index], &entry_status,
                    AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(entry_status.st_mode) ||
            (package_fd = openat(folder_fd, names[index],
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                     O_NOFOLLOW)) < 0) {
            invalid_package(package, names[index], result->count,
                            "package must be one real directory", from_autoload);
            ++result->invalid_count;
            ++result->count;
            continue;
        }
        if (validate_package_fd(package_fd, package, spec, error) != 0) {
            invalid_package(package, names[index], result->count,
                            error[0] ? error : "package validation failed",
                            from_autoload);
            ++result->invalid_count;
        } else {
            package->automatic = from_autoload || package->manifest.autoload;
            package->view.autoload = (uint32_t)package->automatic;
        }
        package->source.root_device = root_identity->st_dev;
        package->source.root_inode = root_identity->st_ino;
        package->source.folder_device = folder_before.st_dev;
        package->source.folder_inode = folder_before.st_ino;
        copy_text(package->source.root, sizeof(package->source.root), root_path);
        copy_text(package->source.folder, sizeof(package->source.folder),
                  folder_path);
        copy_text(package->source.package, sizeof(package->source.package),
                  names[index]);
        package->source.package_device = entry_status.st_dev;
        package->source.package_inode = entry_status.st_ino;
        package->root_autoload_enabled = root_enabled;
        close(package_fd);
        ++result->count;
    }
    if (fstat(folder_fd, &folder_after) != 0 ||
        !stat_identity_equal(&folder_before, &folder_after)) {
        scan_warning(result, "configured folder changed during scan");
    }
    close(folder_fd);
}

static void probe_bytes(uint64_t *value, const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t index;
    for (index = 0; index < length; ++index) {
        *value ^= bytes[index];
        *value *= UINT64_C(1099511628211);
    }
}

static void probe_text(uint64_t *value, const char *text)
{
    probe_bytes(value, text, strlen(text) + 1u);
}

static void probe_stat(uint64_t *value, const struct stat *status)
{
    probe_bytes(value, &status->st_dev, sizeof(status->st_dev));
    probe_bytes(value, &status->st_ino, sizeof(status->st_ino));
    probe_bytes(value, &status->st_mode, sizeof(status->st_mode));
    probe_bytes(value, &status->st_size, sizeof(status->st_size));
    probe_bytes(value, &status->st_mtime, sizeof(status->st_mtime));
    probe_bytes(value, &status->st_ctime, sizeof(status->st_ctime));
    probe_bytes(value, &status->st_mtim.tv_nsec,
                sizeof(status->st_mtim.tv_nsec));
    probe_bytes(value, &status->st_ctim.tv_nsec,
                sizeof(status->st_ctim.tv_nsec));
}

static void probe_package_contents(int package_fd, uint64_t *value)
{
    char names[XZ_SDK_FILES + 2u][XZ_SDK_FILE_NAME];
    uint32_t count = 0;
    uint32_t index;
    int copy = duplicate_cloexec(package_fd);
    DIR *directory = copy >= 0 ? fdopendir(copy) : NULL;
    struct dirent *entry;
    struct stat status;
    if (!directory) {
        const uint32_t failed = UINT32_C(0xfffffffc);
        if (copy >= 0) {
            close(copy);
        }
        probe_bytes(value, &failed, sizeof(failed));
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (count >= XZ_SDK_FILES + 2u ||
            strlen(entry->d_name) >= XZ_SDK_FILE_NAME) {
            count = XZ_SDK_FILES + 2u;
            break;
        }
        copy_text(names[count++], XZ_SDK_FILE_NAME, entry->d_name);
    }
    closedir(directory);
    qsort(names, count, sizeof(names[0]), package_name_compare);
    probe_bytes(value, &count, sizeof(count));
    for (index = 0; index < count && index < XZ_SDK_FILES + 1u; ++index) {
        probe_text(value, names[index]);
        if (fstatat(package_fd, names[index], &status,
                    AT_SYMLINK_NOFOLLOW) == 0) {
            probe_stat(value, &status);
        }
    }
}

static void probe_folder(int root_fd, const char *folder, uint64_t *value,
                         const struct xz_sdk_scan_spec *spec)
{
    int folder_fd;
    struct stat status;
    char names[XZ_SDK_PACKAGES][XZ_SDK_FILE_NAME];
    uint32_t count = 0;
    uint32_t index;
    int overflow = 0;
    probe_text(value, folder);
    folder_fd = open_relative_directory(root_fd, folder, &status);
    if (folder_fd < 0) {
        const uint32_t absent = UINT32_C(0xffffffff);
        probe_bytes(value, &absent, sizeof(absent));
        return;
    }
    probe_stat(value, &status);
    count = directory_packages(folder_fd, names, &overflow, spec);
    probe_bytes(value, &count, sizeof(count));
    probe_bytes(value, &overflow, sizeof(overflow));
    for (index = 0; index < count && !cancelled(spec); ++index) {
        int package_fd;
        probe_text(value, names[index]);
        if (fstatat(folder_fd, names[index], &status,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            const uint32_t gone = UINT32_C(0xfffffffd);
            probe_bytes(value, &gone, sizeof(gone));
            continue;
        }
        probe_stat(value, &status);
        if (!S_ISDIR(status.st_mode)) {
            continue;
        }
        package_fd = openat(folder_fd, names[index],
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (package_fd >= 0) {
            probe_package_contents(package_fd, value);
            close(package_fd);
        }
    }
    close(folder_fd);
}

int xz_sdk_internal_probe(const struct xz_sdk_scan_spec *spec,
                          uint64_t *signature)
{
    uint64_t value = UINT64_C(1469598103934665603);
    unsigned root_index;
    struct expanded_roots roots;
    if (!spec || !signature || !spec->applications_folder ||
        !spec->autoload_folder || spec->root_count > XZ_SDK_ROOTS) {
        return -1;
    }
    expand_roots(spec, &roots);
    probe_bytes(&value, &roots.limited, sizeof(roots.limited));
    for (root_index = 0; root_index < roots.count && !cancelled(spec); ++root_index) {
        const char *root = roots.paths[root_index];
        struct stat status;
        char applications[XZ_SDK_PATH];
        char autoload[XZ_SDK_PATH];
        char ignored[XZ_SDK_SCAN_MESSAGE] = {0};
        int enabled = 1;
        int root_fd;
        if (!root || !root[0]) {
            continue;
        }
        probe_text(&value, root);
        root_fd = open_absolute_directory(root, &status);
        if (root_fd < 0) {
            const uint32_t absent = UINT32_C(0xffffffff);
            probe_bytes(&value, &absent, sizeof(absent));
            continue;
        }
        probe_stat(&value, &status);
        if (load_root_config(root_fd, spec->applications_folder,
                             spec->autoload_folder, applications, autoload,
                             &enabled, ignored) < 0) {
            copy_text(applications, sizeof(applications),
                      spec->applications_folder);
            copy_text(autoload, sizeof(autoload), spec->autoload_folder);
            enabled = 0;
        }
        probe_bytes(&value, &enabled, sizeof(enabled));
        probe_folder(root_fd, autoload, &value, spec);
        if (strcmp(applications, autoload) != 0) {
            probe_folder(root_fd, applications, &value, spec);
        }
        close(root_fd);
    }
    *signature = value;
    return cancelled(spec) ? -1 : 0;
}

int xz_sdk_internal_scan(const struct xz_sdk_scan_spec *spec,
                         struct xz_sdk_scan_result *result)
{
    unsigned root_index;
    unsigned pass;
    struct expanded_roots roots;
    struct root_plan {
        int descriptor, enabled;
        struct stat identity;
        char applications[XZ_SDK_PATH], autoload[XZ_SDK_PATH];
    } plans[XZ_SDK_ROOTS];
    if (!spec || !result || !spec->applications_folder ||
        !spec->autoload_folder ||
        !relative_path_valid(spec->applications_folder) ||
        !relative_path_valid(spec->autoload_folder) ||
        spec->root_count > XZ_SDK_ROOTS) {
        return -1;
    }
    memset(result, 0, sizeof(*result));
    expand_roots(spec, &roots);
    result->limit_reached = (uint32_t)roots.limited;
    memset(plans, 0, sizeof(plans));
    for (root_index = 0; root_index < roots.count; ++root_index) {
        const char *root_path = roots.paths[root_index];
        struct root_plan *plan = &plans[root_index];
        char config_error[XZ_SDK_SCAN_MESSAGE] = {0};
        plan->enabled = 1;
        plan->descriptor = open_absolute_directory(root_path, &plan->identity);
        if (plan->descriptor < 0) continue;
        if (load_root_config(plan->descriptor, spec->applications_folder,
                             spec->autoload_folder, plan->applications, plan->autoload,
                             &plan->enabled, config_error) < 0) {
            scan_warning(result, config_error[0] ? config_error :
                         "invalid root config; defaults used");
            copy_text(plan->applications, sizeof(plan->applications),
                      spec->applications_folder);
            copy_text(plan->autoload, sizeof(plan->autoload), spec->autoload_folder);
            plan->enabled = 0;
        }
        if (plan->enabled) {
            result->any_root_autoload_enabled = 1u;
        }
    }
    /* Allocate catalog slots fairly within each pass. Autoload on every root
     * gets priority before Applications on any root, including a full USB1. */
    for (pass = 0; pass < 2u && !cancelled(spec); ++pass) {
        uint32_t counts[XZ_SDK_ROOTS] = {0}, budgets[XZ_SDK_ROOTS] = {0};
        uint32_t remaining = XZ_SDK_PACKAGES - result->count;
        int progress = 1;
        for (root_index = 0; root_index < roots.count && !cancelled(spec); ++root_index) {
            struct root_plan *plan = &plans[root_index];
            char names[XZ_SDK_PACKAGES][XZ_SDK_FILE_NAME];
            const char *folder = pass ? plan->applications : plan->autoload;
            int limited = 0;
            int descriptor;
            if (plan->descriptor < 0 ||
                (pass && !strcmp(plan->applications, plan->autoload))) continue;
            descriptor = open_relative_directory(plan->descriptor, folder, NULL);
            if (descriptor < 0) {
                if (errno != ENOENT) scan_warning(result, "configured folder rejected (link or unreadable)");
                continue;
            }
            counts[root_index] = directory_packages(descriptor, names, &limited, spec);
            if (limited) result->limit_reached = 1u;
            close(descriptor);
        }
        while (remaining && progress) {
            progress = 0;
            for (root_index = 0; root_index < roots.count && remaining; ++root_index) {
                if (budgets[root_index] < counts[root_index]) {
                    ++budgets[root_index];
                    --remaining;
                    progress = 1;
                }
            }
        }
        for (root_index = 0; root_index < roots.count && !cancelled(spec); ++root_index) {
            struct root_plan *plan = &plans[root_index];
            if (!counts[root_index]) continue;
            scan_folder(result, plan->descriptor, roots.paths[root_index], &plan->identity,
                        pass ? plan->applications : plan->autoload, !pass,
                        plan->enabled, budgets[root_index], spec);
        }
    }
    for (root_index = 0; root_index < roots.count; ++root_index) {
        struct root_plan *plan = &plans[root_index];
        struct stat after;
        if (plan->descriptor < 0) continue;
        if (fstat(plan->descriptor, &after) != 0 ||
            !stat_identity_equal(&plan->identity, &after)) {
            scan_warning(result, "mount root changed during scan");
        }
        close(plan->descriptor);
    }
    if (result->limit_reached) {
        copy_text(result->message, sizeof(result->message),
                  "Discovery capped: 32 packages, 512 folder entries, 8 roots");
    }
    return cancelled(spec) ? -1 : 0;
}

static int copy_payload_to_stage(int package_fd, int stage_fd,
                                 const struct xz_sdk_file_record *file,
                                 int executable, uint64_t *total,
                                 const struct xz_sdk_scan_spec *spec,
                                 char error[XZ_SDK_SCAN_MESSAGE])
{
    int source = -1;
    int destination = -1;
    struct stat before;
    struct stat after;
    struct stat entry_after;
    struct xz_sha256_context hash;
    uint8_t digest[XZ_SHA256_BYTES];
    unsigned char buffer[16384];
    uint64_t copied = 0;
    source = openat(package_fd, file->name,
                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source < 0 || fstat(source, &before) != 0 ||
        !S_ISREG(before.st_mode) || before.st_size < 0 ||
        (uint64_t)before.st_size > XZ_SDK_PACKAGE_BYTES ||
        *total > XZ_SDK_PACKAGE_BYTES - (uint64_t)before.st_size) {
        if (source >= 0) {
            close(source);
        }
        set_error(error, "payload unavailable while staging");
        return -1;
    }
    destination = openat(stage_fd, file->name,
                         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                         executable ? 0700 : 0600);
    if (destination < 0) {
        close(source);
        set_error(error, "cannot create staged payload");
        return -1;
    }
    xz_sha256_init(&hash);
    for (;;) {
        ssize_t count;
        size_t written = 0;
        if (cancelled(spec)) {
            close(source);
            close(destination);
            set_error(error, "staging cancelled");
            return -1;
        }
        do {
            count = read(source, buffer, sizeof(buffer));
        } while (count < 0 && errno == EINTR);
        if (count < 0 ||
            (count > 0 &&
             (uint64_t)count > (uint64_t)before.st_size - copied) ||
            (count == 0 && copied != (uint64_t)before.st_size)) {
            close(source);
            close(destination);
            set_error(error, "payload changed while staging");
            return -1;
        }
        if (!count) {
            break;
        }
        xz_sha256_update(&hash, buffer, (size_t)count);
        while (written < (size_t)count) {
            ssize_t amount;
            if (cancelled(spec)) {
                close(source);
                close(destination);
                set_error(error, "staging cancelled");
                return -1;
            }
            do {
                amount = write(destination, buffer + written,
                               (size_t)count - written);
            } while (amount < 0 && errno == EINTR);
            if (amount <= 0) {
                close(source);
                close(destination);
                set_error(error, "staged payload write failed");
                return -1;
            }
            written += (size_t)amount;
        }
        copied += (uint64_t)count;
    }
    xz_sha256_final(&hash, digest);
    if (memcmp(digest, file->digest, sizeof(digest)) != 0 ||
        fstat(source, &after) != 0 ||
        fstatat(package_fd, file->name, &entry_after,
                AT_SYMLINK_NOFOLLOW) != 0 ||
        !stat_file_unchanged(&before, &after) ||
        !stat_file_unchanged(&before, &entry_after) ||
        fchmod(destination, executable ? 0700 : 0600) != 0) {
        close(source);
        close(destination);
        set_error(error, "payload identity or digest changed while staging");
        return -1;
    }
    close(source);
    if (close(destination) != 0) {
        set_error(error, "staged payload close failed");
        return -1;
    }
    *total += copied;
    return 0;
}

/* 1 = readable process, 0 = absent, -1 = cannot prove identity. Field 22 is
 * Linux's start time in ticks, independent of libc clocks and PID reuse. */
static int process_identity(long process, unsigned long long *start, int *alive)
{
    char path[64], bytes[4096], *cursor, *end;
    size_t length;
    unsigned field;
    int descriptor;
    if (process <= 1 || snprintf(path, sizeof(path), "/proc/%ld/stat", process) >= (int)sizeof(path)) return -1;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return errno == ENOENT || errno == ESRCH ? 0 : -1;
    if (read_bounded(descriptor, (unsigned char *)bytes, sizeof(bytes) - 1u, &length) != 0) {
        close(descriptor);
        return -1;
    }
    close(descriptor);
    bytes[length] = '\0';
    cursor = strrchr(bytes, ')');
    if (!cursor || cursor[1] != ' ' || !cursor[2]) return -1;
    *alive = cursor[2] != 'Z' && cursor[2] != 'X';
    cursor += 2;
    for (field = 3; field < 22u; ++field) {
        cursor = strchr(cursor, ' ');
        if (!cursor) return -1;
        while (*cursor == ' ') ++cursor;
    }
    errno = 0;
    *start = strtoull(cursor, &end, 10);
    return errno || end == cursor || (*end != ' ' && *end != '\0') ? -1 : 1;
}

static int verified_stage_fd(const struct xz_sdk_stage *stage)
{
    struct stat status;
    int base = open_absolute_directory(stage->base, &status);
    int directory;
    if (base < 0) return -1;
    if (status.st_dev != stage->base_device || status.st_ino != stage->base_inode) {
        close(base);
        return -1;
    }
    directory = openat(base, stage->name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    close(base);
    if (directory < 0) return -1;
    if (fstat(directory, &status) != 0 || status.st_dev != stage->directory_device ||
        status.st_ino != stage->directory_inode || status.st_uid != geteuid() ||
        (status.st_mode & 0777u) != 0700u) {
        close(directory);
        return -1;
    }
    return directory;
}

static int owner_pair(char **cursor, const char *key,
                      unsigned long long *first, unsigned long long *second)
{
    char *line = take_line(cursor), *end, *number;
    size_t length = strlen(key);
    if (!line || strncmp(line, key, length)) return -1;
    number = line + length;
    if (*number < '0' || *number > '9') return -1;
    errno = 0;
    *first = strtoull(number, &end, 10);
    if (errno || *end != ':' || end[1] < '0' || end[1] > '9') return -1;
    errno = 0;
    *second = strtoull(end + 1, &end, 10);
    return errno || *end ? -1 : 0;
}

static int read_owner(int directory, struct stage_owner *owner)
{
    char bytes[1024], *cursor, *line;
    unsigned long long host, child;
    size_t length;
    struct stat status;
    int descriptor = openat(directory, XZ_OWNER_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int result = -1;
    if (descriptor < 0) return -1;
    memset(owner, 0, sizeof(*owner));
    if (fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) &&
        status.st_uid == geteuid() && status.st_nlink == 1 &&
        read_bounded(descriptor, (unsigned char *)bytes, sizeof(bytes) - 1u, &length) == 0) {
        if (memchr(bytes, '\0', length)) {
            close(descriptor);
            return -1;
        }
        bytes[length] = '\0';
        cursor = bytes;
        line = take_line(&cursor);
        if (line && !strcmp(line, "XZ_SDK_STAGE 1") &&
            owner_pair(&cursor, "host=", &host, &owner->host_start) == 0 &&
            owner_pair(&cursor, "child=", &child, &owner->child_start) == 0 &&
            owner_pair(&cursor, "base=", &owner->base_device, &owner->base_inode) == 0 &&
            owner_pair(&cursor, "stage=", &owner->device, &owner->inode) == 0 &&
            host > 1 && host <= INT_MAX && owner->host_start &&
            (child == 0 || (child > 1 && child <= INT_MAX))) {
            line = take_line(&cursor);
            if (line && !strncmp(line, "entry=", 6u) && flat_name_valid(line + 6u) &&
                (!cursor || !*cursor)) {
                owner->host = (long)host;
                owner->child = (long)child;
                copy_text(owner->entry, sizeof(owner->entry), line + 6u);
                result = 0;
            }
        }
    }
    close(descriptor);
    return result;
}

static int write_owner(int directory, const struct stage_owner *owner)
{
    char bytes[1024];
    const char *next = XZ_OWNER_NAME "-next";
    size_t used = 0;
    int length = snprintf(bytes, sizeof(bytes),
        "XZ_SDK_STAGE 1\nhost=%ld:%llu\nchild=%ld:%llu\nbase=%llu:%llu\nstage=%llu:%llu\nentry=%s\n",
        owner->host, owner->host_start, owner->child, owner->child_start,
        owner->base_device, owner->base_inode, owner->device, owner->inode, owner->entry);
    int descriptor;
    if (length < 0 || length >= (int)sizeof(bytes)) return -1;
    descriptor = openat(directory, next, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) return -1;
    while (used < (size_t)length) {
        ssize_t amount = write(descriptor, bytes + used, (size_t)length - used);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) break;
        used += (size_t)amount;
    }
    close(descriptor);
    if (used != (size_t)length || renameat(directory, next, directory, XZ_OWNER_NAME) != 0) {
        (void)unlinkat(directory, next, 0);
        return -1;
    }
    return 0;
}

static int stage_owner_begin(const struct xz_sdk_stage *stage, const char *entry)
{
    struct stage_owner owner;
    int alive, result, directory = verified_stage_fd(stage);
    if (directory < 0) return -1;
    memset(&owner, 0, sizeof(owner));
    owner.host = (long)getpid();
    if (process_identity(owner.host, &owner.host_start, &alive) != 1) {
        close(directory);
        return -1;
    }
    owner.base_device = (unsigned long long)stage->base_device;
    owner.base_inode = (unsigned long long)stage->base_inode;
    owner.device = (unsigned long long)stage->directory_device;
    owner.inode = (unsigned long long)stage->directory_inode;
    copy_text(owner.entry, sizeof(owner.entry), entry);
    result = write_owner(directory, &owner);
    close(directory);
    return result;
}

int xz_sdk_internal_stage_child(const struct xz_sdk_stage *stage, pid_t child)
{
    struct stage_owner owner;
    int alive, identity, result, directory = verified_stage_fd(stage);
    if (directory < 0) return -1;
    if (read_owner(directory, &owner) != 0 || owner.host != (long)getpid()) {
        close(directory);
        return -1;
    }
    owner.child = (long)child;
    identity = process_identity(owner.child, &owner.child_start, &alive);
    result = identity < 0 ? -1 : write_owner(directory, &owner);
    close(directory);
    return result;
}

int xz_sdk_internal_stage(const struct xz_sdk_package_record *package,
                          const char *stage_root, uint64_t nonce,
                          const struct xz_sdk_scan_spec *spec,
                          struct xz_sdk_stage *stage,
                          char error[XZ_SDK_SCAN_MESSAGE])
{
    int root_fd = -1;
    int folder_fd = -1;
    int package_fd = -1;
    int base_fd = -1;
    int stage_fd = -1;
    struct stat root_status;
    struct stat folder_status;
    struct stat package_status;
    struct stat base_status;
    struct stat stage_status;
    struct statfs stage_filesystem;
    struct xz_sdk_package_record fresh;
    char validation_error[XZ_SDK_SCAN_MESSAGE] = {0};
    uint32_t index;
    uint64_t total = 0;
    int result = -1;
    if (!package || !package->valid || !stage || !stage_root) {
        set_error(error, "invalid staging request");
        return -1;
    }
    memset(stage, 0, sizeof(*stage));
    root_fd = open_absolute_directory(package->source.root, &root_status);
    if (root_fd < 0 || root_status.st_dev != package->source.root_device ||
        root_status.st_ino != package->source.root_inode) {
        set_error(error, "package media root changed");
        goto done;
    }
    folder_fd = open_relative_directory(root_fd, package->source.folder,
                                        &folder_status);
    if (folder_fd < 0 ||
        folder_status.st_dev != package->source.folder_device ||
        folder_status.st_ino != package->source.folder_inode) {
        set_error(error, "package folder changed");
        goto done;
    }
    package_fd = openat(folder_fd, package->source.package,
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (package_fd < 0 || fstat(package_fd, &package_status) != 0 ||
        package_status.st_dev != package->source.package_device ||
        package_status.st_ino != package->source.package_inode) {
        set_error(error, "package directory changed");
        goto done;
    }
    if (validate_package_fd(package_fd, &fresh, spec, validation_error) != 0 ||
        memcmp(fresh.content_digest, package->content_digest,
               XZ_SHA256_BYTES) != 0 ||
        strcmp(fresh.manifest.id, package->manifest.id) != 0) {
        set_error(error, "%s", validation_error[0] ? validation_error :
                  "package content changed before launch");
        goto done;
    }
    base_fd = open_or_create_absolute_directory(stage_root, &base_status);
    if (base_fd < 0 || fstatfs(base_fd, &stage_filesystem) != 0 ||
        ((uint32_t)stage_filesystem.f_type != XZ_TMPFS_MAGIC &&
         (uint32_t)stage_filesystem.f_type != XZ_RAMFS_MAGIC)) {
        set_error(error, "stage root is missing, linked, or not RAM-backed");
        goto done;
    }
    if (copy_text(stage->base, sizeof(stage->base), stage_root) != 0 ||
        snprintf(stage->name, sizeof(stage->name), "xz-%ld-%llu-%02x%02x%02x%02x",
                 (long)getpid(), (unsigned long long)nonce,
                 package->content_digest[0], package->content_digest[1],
                 package->content_digest[2], package->content_digest[3]) >=
            (int)sizeof(stage->name) ||
        snprintf(stage->path, sizeof(stage->path), "%s/%s", stage_root,
                 stage->name) >= (int)sizeof(stage->path) ||
        mkdirat(base_fd, stage->name, 0700) != 0) {
        set_error(error, "cannot allocate unique stage directory");
        goto done;
    }
    stage_fd = openat(base_fd, stage->name,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (stage_fd < 0 || fstat(stage_fd, &stage_status) != 0) {
        (void)unlinkat(base_fd, stage->name, AT_REMOVEDIR);
        set_error(error, "cannot verify unique stage directory");
        goto done;
    }
    stage->base_device = base_status.st_dev;
    stage->base_inode = base_status.st_ino;
    stage->directory_device = stage_status.st_dev;
    stage->directory_inode = stage_status.st_ino;
    stage->active = 1;
    if (stage_owner_begin(stage, fresh.manifest.entry) != 0) {
        set_error(error, "cannot journal stage ownership");
        goto done;
    }
    for (index = 0; index < fresh.manifest.file_count; ++index) {
        int executable = (fresh.manifest.kind != XZ_SDK_PROFILE &&
                          strcmp(fresh.manifest.files[index].name,
                                 fresh.manifest.entry) == 0);
        if (copy_payload_to_stage(package_fd, stage_fd,
                                  &fresh.manifest.files[index], executable,
                                  &total, spec,
                                  error) != 0 ||
            copy_text(stage->files[index], sizeof(stage->files[index]),
                      fresh.manifest.files[index].name) != 0) {
            goto done;
        }
        ++stage->file_count;
    }
    if (snprintf(stage->entry_path, sizeof(stage->entry_path), "%s/%s",
                 stage->path, fresh.manifest.entry) >=
        (int)sizeof(stage->entry_path) ||
        fstat(root_fd, &root_status) != 0 ||
        root_status.st_dev != package->source.root_device ||
        root_status.st_ino != package->source.root_inode ||
        fstat(folder_fd, &folder_status) != 0 ||
        folder_status.st_dev != package->source.folder_device ||
        folder_status.st_ino != package->source.folder_inode ||
        fstat(package_fd, &package_status) != 0 ||
        package_status.st_dev != package->source.package_device ||
        package_status.st_ino != package->source.package_inode) {
        set_error(error, "media identity changed while staging");
        goto done;
    }
    result = 0;

done:
    if (stage_fd >= 0) {
        close(stage_fd);
    }
    if (base_fd >= 0) {
        close(base_fd);
    }
    if (package_fd >= 0) {
        close(package_fd);
    }
    if (folder_fd >= 0) {
        close(folder_fd);
    }
    if (root_fd >= 0) {
        close(root_fd);
    }
    if (result != 0 && stage->active) {
        xz_sdk_internal_stage_cleanup(stage);
    }
    return result;
}

int xz_sdk_internal_source_present(const struct xz_sdk_source *source)
{
    int root_fd = -1;
    int folder_fd = -1;
    int package_fd = -1;
    struct stat status;
    int present = 0;
    if (!source) {
        return 0;
    }
    root_fd = open_absolute_directory(source->root, &status);
    if (root_fd < 0 || status.st_dev != source->root_device ||
        status.st_ino != source->root_inode) {
        goto done;
    }
    folder_fd = open_relative_directory(root_fd, source->folder, &status);
    if (folder_fd < 0 || status.st_dev != source->folder_device ||
        status.st_ino != source->folder_inode) {
        goto done;
    }
    package_fd = openat(folder_fd, source->package,
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (package_fd < 0 || fstat(package_fd, &status) != 0 ||
        status.st_dev != source->package_device ||
        status.st_ino != source->package_inode) {
        goto done;
    }
    present = 1;
done:
    if (package_fd >= 0) {
        close(package_fd);
    }
    if (folder_fd >= 0) {
        close(folder_fd);
    }
    if (root_fd >= 0) {
        close(root_fd);
    }
    return present;
}

void xz_sdk_internal_stage_cleanup(struct xz_sdk_stage *stage)
{
    int base_fd = -1;
    int stage_fd = -1;
    int copy = -1;
    DIR *directory = NULL;
    struct dirent *entry;
    struct stat status;
    unsigned visited = 0;
    if (!stage || !stage->active || !flat_name_valid(stage->name)) {
        return;
    }
    base_fd = open_absolute_directory(stage->base, &status);
    if (base_fd < 0 || status.st_dev != stage->base_device ||
        status.st_ino != stage->base_inode) {
        goto done;
    }
    stage_fd = openat(base_fd, stage->name,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (stage_fd < 0 || fstat(stage_fd, &status) != 0 ||
        status.st_dev != stage->directory_device ||
        status.st_ino != stage->directory_inode) {
        goto done;
    }
    copy = duplicate_cloexec(stage_fd);
    directory = copy >= 0 ? fdopendir(copy) : NULL;
    if (!directory) {
        if (copy >= 0) {
            close(copy);
        }
        goto done;
    }
    /* Keep the journal if a child added an oversized or nested tree. */
    while ((entry = readdir(directory)) != NULL) {
        if (++visited > XZ_DIRECTORY_ENTRIES ||
            (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") &&
             (fstatat(stage_fd, entry->d_name, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
              S_ISDIR(status.st_mode)))) goto done;
    }
    rewinddir(directory);
    visited = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (++visited > XZ_DIRECTORY_ENTRIES) goto done;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (fstatat(stage_fd, entry->d_name, &status,
                    AT_SYMLINK_NOFOLLOW) == 0 && !S_ISDIR(status.st_mode)) {
            (void)unlinkat(stage_fd, entry->d_name, 0);
        }
    }
    closedir(directory);
    directory = NULL;
    close(stage_fd);
    stage_fd = -1;
    (void)unlinkat(base_fd, stage->name, AT_REMOVEDIR);
done:
    if (directory) {
        closedir(directory);
    }
    if (stage_fd >= 0) {
        close(stage_fd);
    }
    if (base_fd >= 0) {
        close(base_fd);
    }
    stage->active = 0;
}

static int executable_matches(const struct xz_sdk_stage *stage, long process)
{
    char path[64], target[XZ_SDK_PATH];
    struct stat executable, staged;
    ssize_t length;
    if (snprintf(path, sizeof(path), "/proc/%ld/exe", process) >= (int)sizeof(path)) return 0;
    length = readlink(path, target, sizeof(target) - 1u);
    if (length < 0) return 0;
    target[length] = '\0';
    return !strcmp(target, stage->entry_path) &&
        stat(path, &executable) == 0 && lstat(stage->entry_path, &staged) == 0 &&
        S_ISREG(staged.st_mode) && stat_identity_equal(&executable, &staged);
}

/* A death between spawn and journal update is covered by a bounded /proc
 * search, still requiring the exact staged executable inode and path. */
static int locate_unjournaled_child(const struct xz_sdk_stage *stage,
                                    struct stage_owner *owner,
                                    const struct xz_sdk_scan_spec *spec)
{
    DIR *processes = opendir("/proc");
    struct dirent *entry;
    unsigned visited = 0;
    int result = 0;
    if (!processes) return -1;
    while (!cancelled(spec) && (entry = readdir(processes)) != NULL) {
        char *end;
        long process = strtol(entry->d_name, &end, 10);
        unsigned long long start;
        int alive;
        if (++visited > XZ_RECOVERY_PROCESSES) {
            result = -1;
            break;
        }
        if (*end || process <= 1 || !executable_matches(stage, process)) continue;
        if (process_identity(process, &start, &alive) != 1 || !alive) continue;
        if (owner->child) {
            result = -1;
            break;
        }
        owner->child = process;
        owner->child_start = start;
    }
    closedir(processes);
    return cancelled(spec) ? -1 : result;
}

static int recovery_child_state(const struct xz_sdk_stage *stage,
                                const struct stage_owner *owner)
{
    unsigned long long start;
    int alive;
    int result = process_identity(owner->child, &start, &alive);
    if (!result) return 0;
    if (result < 0 || start != owner->child_start) return -1;
    if (!alive) return 0;
    if (!executable_matches(stage, owner->child)) return -1;
    result = process_identity(owner->child, &start, &alive);
    return result == 1 && start == owner->child_start && alive ? 1 : -1;
}

int xz_sdk_internal_recover(const char *stage_root,
                            const struct xz_sdk_scan_spec *spec)
{
    int base;
    int copy;
    DIR *directory;
    struct dirent *entry;
    struct stat base_status;
    struct statfs filesystem;
    unsigned visited = 0, stages = 0;
    int recovered = 0;
    base = open_absolute_directory(stage_root, &base_status);
    if (base < 0) return 0;
    if (fstatfs(base, &filesystem) != 0 ||
        ((uint32_t)filesystem.f_type != XZ_TMPFS_MAGIC &&
         (uint32_t)filesystem.f_type != XZ_RAMFS_MAGIC)) {
        close(base);
        return 0;
    }
    copy = duplicate_cloexec(base);
    directory = copy >= 0 ? fdopendir(copy) : NULL;
    if (!directory) {
        if (copy >= 0) close(copy);
        close(base);
        return 0;
    }
    while (!cancelled(spec) && (entry = readdir(directory)) != NULL) {
        struct xz_sdk_stage stage;
        struct stage_owner owner;
        struct stat status;
        unsigned long long host_start;
        long name_host;
        char *name_end;
        int alive, identity, descriptor, child_state = 0;
        unsigned attempts;
        if (++visited > XZ_DIRECTORY_ENTRIES || stages >= XZ_RECOVERY_STAGES) break;
        if (!flat_name_valid(entry->d_name) || strncmp(entry->d_name, "xz-", 3u) ||
            entry->d_name[3] < '0' || entry->d_name[3] > '9') continue;
        errno = 0;
        name_host = strtol(entry->d_name + 3u, &name_end, 10);
        if (errno || *name_end != '-' || !name_end[1]) continue;
        ++stages;
        memset(&stage, 0, sizeof(stage));
        stage.base_device = base_status.st_dev;
        stage.base_inode = base_status.st_ino;
        copy_text(stage.base, sizeof(stage.base), stage_root);
        copy_text(stage.name, sizeof(stage.name), entry->d_name);
        if (snprintf(stage.path, sizeof(stage.path), "%s/%s", stage_root, stage.name) >= (int)sizeof(stage.path) ||
            fstatat(base, stage.name, &status, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(status.st_mode)) continue;
        stage.directory_device = status.st_dev;
        stage.directory_inode = status.st_ino;
        descriptor = verified_stage_fd(&stage);
        if (descriptor < 0) continue;
        if (read_owner(descriptor, &owner) != 0 || owner.host != name_host ||
            owner.base_device != (unsigned long long)stage.base_device ||
            owner.base_inode != (unsigned long long)stage.base_inode ||
            owner.device != (unsigned long long)stage.directory_device ||
            owner.inode != (unsigned long long)stage.directory_inode) {
            close(descriptor);
            continue;
        }
        close(descriptor);
        identity = process_identity(owner.host, &host_start, &alive);
        if (identity < 0 || (identity == 1 && host_start == owner.host_start && alive)) continue;
        if (snprintf(stage.entry_path, sizeof(stage.entry_path), "%s/%s", stage.path, owner.entry) >= (int)sizeof(stage.entry_path)) continue;
        if (!owner.child && locate_unjournaled_child(&stage, &owner, spec) != 0) continue;
        if (owner.child) {
            child_state = recovery_child_state(&stage, &owner);
            if (child_state < 0 || cancelled(spec)) continue;
            if (child_state == 1) {
                if (kill((pid_t)owner.child, SIGKILL) != 0 && errno != ESRCH) continue;
                for (attempts = 0; attempts < 10u && !cancelled(spec); ++attempts) {
                    struct timespec delay = {0, 50000000};
                    child_state = recovery_child_state(&stage, &owner);
                    if (child_state == 0) break;
                    (void)nanosleep(&delay, NULL);
                }
            }
        }
        if (child_state != 0 || cancelled(spec)) continue;
        stage.active = 1;
        xz_sdk_internal_stage_cleanup(&stage);
        ++recovered;
    }
    closedir(directory);
    close(base);
    return recovered;
}
