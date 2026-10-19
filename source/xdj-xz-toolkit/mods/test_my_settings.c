#define _POSIX_C_SOURCE 200809L
#include "my_settings.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#ifdef NDEBUG
#error Tests require assertions
#endif

#include "my_settings_fixtures.h"

static unsigned edits;

/* Independent nibble-table implementation, also checked against 123456789. */
static uint16_t reference_crc(const uint8_t *p, size_t size) {
    static const uint16_t table[16] = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
    };
    uint16_t crc = 0;
    size_t i;
    for (i = 0; i < size; ++i) {
        crc = (uint16_t)((crc << 4) ^ table[(crc >> 12) ^ (p[i] >> 4)]);
        crc = (uint16_t)((crc << 4) ^ table[(crc >> 12) ^ (p[i] & 15)]);
    }
    return crc;
}

static void seal(enum ms_kind kind, uint8_t *p, size_t size) {
    size_t start = kind == MS_MIXER ? 0 : MS_HEADER_SIZE;
    uint16_t crc = reference_crc(p + start, size - 4 - start);
    p[size - 4] = (uint8_t)crc;
    p[size - 3] = (uint8_t)(crc >> 8);
}

static void roundtrip(const struct fixture *f) {
    struct ms_settings s, next, sentinel;
    uint8_t result[MS_MAX_FILE_SIZE + 1], expected[MS_MAX_FILE_SIZE];
    size_t size, i, j;
    unsigned value;
    assert(ms_parse(f->kind, f->bytes, f->size, &s) == MS_OK);
    assert(ms_validate(&s) == MS_OK);
    assert(ms_serialize(&s, result, sizeof(result), &size) == MS_OK);
    assert(size == f->size && !memcmp(result, f->bytes, size));
    for (i = 0; i < MS_FIELD_COUNT; ++i) {
        const struct ms_field_info *field = ms_field_info((enum ms_field)i);
        if (field->kind != f->kind) continue;
        assert(ms_get(&s, (enum ms_field)i, &value) == MS_OK);
        assert(value == f->bytes[MS_HEADER_SIZE + field->body_offset]);
        for (j = 0; j < field->choice_count; ++j) {
            next = s;
            assert(ms_set(&next, (enum ms_field)i, field->choices[j].value) == MS_OK);
            assert(ms_serialize(&next, result, sizeof(result), &size) == MS_OK);
            memcpy(expected, f->bytes, size);
            expected[MS_HEADER_SIZE + field->body_offset] = (uint8_t)field->choices[j].value;
            seal(f->kind, expected, size);
            assert(!memcmp(result, expected, size));
            assert(ms_parse(f->kind, result, size, &next) == MS_OK);
            assert(ms_get(&next, (enum ms_field)i, &value) == MS_OK);
            assert(value == field->choices[j].value);
            assert(ms_choice_label((enum ms_field)i, value));
            ++edits;
        }
        assert(ms_set(&next, (enum ms_field)i, 256) == MS_ERR_VALUE);
    }
    assert(ms_serialize(&s, result, f->size - 1, &size) == MS_ERR_SIZE);
    /* Every body bit and checksum bit must be detected without updating CRC. */
    sentinel = s;
    for (i = MS_HEADER_SIZE; i < f->size - 2; ++i) {
        for (j = 0; j < 8; ++j) {
            memcpy(result, f->bytes, f->size);
            result[i] ^= (uint8_t)(1u << j);
            assert(ms_parse(f->kind, result, f->size, &sentinel) == MS_ERR_CHECKSUM);
            assert(!memcmp(&sentinel, &s, sizeof(s)));
        }
    }
    for (i = 0; i < f->size; ++i)
        assert(ms_parse(f->kind, f->bytes, i, &next) == MS_ERR_SIZE);
    memcpy(result, f->bytes, f->size);
    result[f->size] = 0;
    assert(ms_parse(f->kind, result, f->size + 1, &next) == MS_ERR_SIZE);
    result[0] = 95;
    assert(ms_parse(f->kind, result, f->size, &next) == MS_ERR_HEADER);
    memcpy(result, f->bytes, f->size);
    result[100] ^= 1;
    assert(ms_parse(f->kind, result, f->size, &next) == MS_ERR_HEADER);
    memcpy(result, f->bytes, f->size);
    memset(result + 68, 'x', 32);
    assert(ms_parse(f->kind, result, f->size, &next) == MS_ERR_HEADER);
    if (f->kind == MS_MIXER) {
        memcpy(result, f->bytes, f->size);
        result[4] ^= 1;
        assert(ms_parse(f->kind, result, f->size, &next) == MS_ERR_CHECKSUM);
    }
}

static void variants(const struct fixture *f) {
    struct ms_settings s;
    uint8_t bytes[MS_MAX_FILE_SIZE], output[MS_MAX_FILE_SIZE];
    size_t i, size;
    memcpy(bytes, f->bytes, f->size);
    /* Simulate older writers that leave later fields at zero. This is a
     * compatibility policy test, not a captured older firmware export. */
    for (i = 0; i < MS_FIELD_COUNT; ++i) {
        const struct ms_field_info *field = ms_field_info((enum ms_field)i);
        if (field->kind == f->kind) bytes[MS_HEADER_SIZE + field->body_offset] = 0;
    }
    memset(bytes + 68, 0, 32);
    memcpy(bytes + 68, "0.001", 5);
    bytes[f->size - 1] = 0x42;
    bytes[f->size - 6] = 0xa5;
    seal(f->kind, bytes, f->size);
    assert(ms_parse(f->kind, bytes, f->size, &s) == MS_OK);
    assert(ms_serialize(&s, output, sizeof(output), &size) == MS_OK);
    assert(!memcmp(bytes, output, size));
    for (i = 0; i < MS_FIELD_COUNT; ++i) {
        const struct ms_field_info *field = ms_field_info((enum ms_field)i);
        if (field->kind != f->kind) continue;
        bytes[MS_HEADER_SIZE + field->body_offset] = 0xfe;
        seal(f->kind, bytes, f->size);
        assert(ms_parse(f->kind, bytes, f->size, &s) == MS_OK);
        assert(!ms_choice_label((enum ms_field)i, 0xfe));
        assert(ms_serialize(&s, output, sizeof(output), &size) == MS_OK);
        assert(!memcmp(bytes, output, size));
        assert(ms_set(&s, (enum ms_field)i, 0xfd) == MS_ERR_VALUE);
    }
    /* Exercise every opaque byte, not just the customary zero padding. */
    memcpy(bytes, f->bytes, f->size);
    for (i = MS_HEADER_SIZE; i < f->size - 4; ++i) {
        size_t j;
        int mapped = 0;
        for (j = 0; j < MS_FIELD_COUNT; ++j) {
            const struct ms_field_info *field = ms_field_info((enum ms_field)j);
            if (field->kind == f->kind && MS_HEADER_SIZE + field->body_offset == i)
                mapped = 1;
        }
        if (!mapped) bytes[i] = (uint8_t)(i * 73u);
    }
    bytes[35] = 0x96;
    bytes[67] = 0x97;
    bytes[99] = 0x98;
    bytes[f->size - 2] = 0x99;
    bytes[f->size - 1] = 0x9a;
    seal(f->kind, bytes, f->size);
    assert(ms_parse(f->kind, bytes, f->size, &s) == MS_OK);
    assert(ms_serialize(&s, output, sizeof(output), &size) == MS_OK);
    assert(!memcmp(bytes, output, size));
}

#ifdef MS_TEST_FAULTS
static int fault;
ssize_t __real_write(int fd, const void *p, size_t n);
int __real_fsync(int fd);
int __real_rename(const char *old, const char *next);
ssize_t __wrap_write(int fd, const void *p, size_t n) {
    if (fault == 1) { errno = ENOSPC; return -1; }
    if (fault == 5) { fault = 0; errno = EINTR; return -1; }
    return __real_write(fd, p, fault == 6 && n > 1 ? 1 : n);
}
int __wrap_fsync(int fd) {
    struct stat st;
    assert(fstat(fd, &st) == 0);
    if (fault == 2 || (fault == 4 && S_ISDIR(st.st_mode))) { errno = EIO; return -1; }
    return __real_fsync(fd);
}
int __wrap_rename(const char *old, const char *next) {
    if (fault == 3) { errno = EIO; return -1; }
    return __real_rename(old, next);
}
#endif

static void no_temporaries(const char *directory) {
    DIR *dir = opendir(directory);
    struct dirent *entry;
    assert(dir);
    while ((entry = readdir(dir)) != NULL)
        assert(strncmp(entry->d_name, ".my-settings-", 13));
    closedir(dir);
}

static void files(void) {
    char directory[] = "/tmp/my-settings-test-XXXXXX", path[256], link[256];
    struct ms_settings s, readback, changed;
    struct stat st;
    struct statvfs mount_info;
    static const char *const read_only_candidates[] = {
        "/sys", "/tmp/.X11-unix", "/usr/lib/wsl/drivers"
    };
    size_t candidate;
    assert(mkdtemp(directory));
    snprintf(path, sizeof(path), "%s/MYSETTING.DAT", directory);
    snprintf(link, sizeof(link), "%s/link", directory);
    assert(ms_parse(MS_PLAYER, fixture_2, sizeof(fixture_2), &s) == MS_OK);
    assert(ms_write_atomic(path, &s) == MS_OK);
    assert(ms_read_file(MS_PLAYER, path, &readback) == MS_OK);
    assert(!memcmp(s.raw, readback.raw, s.size));
    assert(chmod(path, 0640) == 0);
    changed = s;
    changed.values.player.on_air_display = MS_TOGGLE_OFF;
    assert(ms_write_atomic(path, &changed) == MS_OK);
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640);
    assert(ms_read_file(MS_PLAYER, path, &readback) == MS_OK);
    assert(readback.values.player.on_air_display == MS_TOGGLE_OFF);
    changed.values.player.on_air_display = (enum ms_toggle)0xff;
    assert(ms_validate(&changed) == MS_ERR_VALUE);
    assert(ms_write_atomic(path, &changed) == MS_ERR_VALUE);
    assert(symlink(path, link) == 0);
    assert(ms_write_atomic(link, &s) == MS_ERR_PATH);
    assert(ms_write_atomic(directory, &s) == MS_ERR_PATH);
    assert(ms_write_atomic("/nonexistent-my-settings-directory/file", &s) == MS_ERR_IO);
    for (candidate = 0; candidate < sizeof(read_only_candidates) / sizeof(read_only_candidates[0]); ++candidate) {
        const char *parent = read_only_candidates[candidate];
        if (statvfs(parent, &mount_info) == 0 && (mount_info.f_flag & ST_RDONLY)) {
            char refused[256];
            snprintf(refused, sizeof(refused), "%s/my-settings-never-created", parent);
            assert(ms_write_atomic(refused, &s) == MS_ERR_READ_ONLY);
            printf("my_settings: read-only mount refusal passed on %s\n", parent);
            break;
        }
    }
    if (candidate == sizeof(read_only_candidates) / sizeof(read_only_candidates[0]))
        puts("my_settings: SKIP read-only mount refusal; no read-only test mount");
#ifdef MS_TEST_FAULTS
    {
        int i;
        for (i = 1; i <= 6; ++i) {
            assert(ms_write_atomic(path, &s) == MS_OK);
            changed = s;
            changed.values.player.on_air_display = MS_TOGGLE_OFF;
            fault = i;
            assert(ms_write_atomic(path, &changed) ==
                   (i <= 3 ? MS_ERR_IO : i == 4 ? MS_ERR_DURABILITY : MS_OK));
            fault = 0;
            assert(ms_read_file(MS_PLAYER, path, &readback) == MS_OK);
            assert(readback.values.player.on_air_display ==
                   (i <= 3 ? s.values.player.on_air_display : MS_TOGGLE_OFF));
            no_temporaries(directory);
        }
    }
#endif
    no_temporaries(directory);
    assert(unlink(link) == 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

int main(int argc, char **argv) {
    size_t i;
    unsigned value;
    struct ms_settings s;
    assert(ms_crc16("123456789", 9) == 0x31c3);
    assert(reference_crc((const uint8_t *)"123456789", 9) == 0x31c3);
    for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) roundtrip(&fixtures[i]);
    for (i = 0; i < 4; ++i) variants(&fixtures[i]);
    assert(ms_parse((enum ms_kind)-1, fixture_0, sizeof(fixture_0), &s) == MS_ERR_ARGUMENT);
    assert(ms_parse(MS_PLAYER, NULL, 148, &s) == MS_ERR_ARGUMENT);
    assert(ms_parse(MS_PLAYER, fixture_2, sizeof(fixture_2), &s) == MS_OK);
    assert(ms_get(&s, MS_FIELD_DEVICE_WAVEFORM_COLOR, &value) == MS_ERR_ARGUMENT);
    assert(!ms_field_info(MS_FIELD_COUNT));
    assert(!ms_filename(MS_KIND_COUNT));
    assert(ms_validate(NULL) == MS_ERR_ARGUMENT);
    if (argc == 1 || strcmp(argv[1], "--codec-only")) files();
    printf("my_settings: %zu export fixtures, %u choice edits, corruptions and variants passed\n",
           sizeof(fixtures) / sizeof(fixtures[0]), edits);
    return 0;
}
