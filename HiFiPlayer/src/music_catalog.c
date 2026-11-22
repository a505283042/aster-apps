/* SPDX-License-Identifier: MIT
 *
 * Portable adaptation of the FakePod_S31 V2 Catalog/Manifest lifecycle:
 * recursive walk, bounded cooperative work, checksummed disk snapshots,
 * generational publication and old-snapshot retention on scan failure.
 *
 * Aster does not expose file mtime or the S31 Catalog allocation APIs.
 * Therefore this v1 snapshot stores path + size, and detects most changes by
 * order-independent path+size fingerprints. Same-size in-place file edits may
 * not be detected until a future firmware metadata API is available.
 *
 * All records remain on SD instead of consuming WASM memory for thousands
 * of fixed 152-byte paths. The 5-row UI is a naturally virtualized reader.
 * Aster IDF currently permits only four open directory handles and sandbox
 * API paths shorter than 160 bytes; fit these actual native bounds.
 */
#include "music_catalog.h"
#include <aster_imports.h>
/* aster_imports.h includes the SDK's freestanding aster_libc.h. */

enum {
    CAT_MAGIC = 0x32464948u, /* "HIF2" */
    CAT_VERSION = 2,
    CAT_DEPTH = 4,
    CAT_STEP_ENTRIES = 8,
    CAT_STEP_MS = 7
};
static const char *const paths[2] = {
    "/data/hifi_catalog_a.bin", "/data/hifi_catalog_b.bin"
};

/* Stable disk layout: all header fields are u32, record is exactly 160B.
 * Deliberately not binary-compatible with ESP-IDF's V2 metadata Catalog. */
typedef struct {
    uint32_t magic, version, generation, count;
    uint32_t payload_crc, signature_xor, signature_sum, flags;
    uint32_t header_crc;
} cat_header_t;
typedef struct {
    uint64_t size;
    char name[HIFI_CATALOG_PATH_MAX];
} cat_row_t;

typedef struct {
    aster_handle_t handle;
    char path[256];
} walk_dir_t;

static walk_dir_t stack[CAT_DEPTH];
static int depth = -1;
static aster_handle_t active, staging;
static cat_header_t committed;
static uint32_t discovered, record_crc, sig_xor, sig_sum;
static uint32_t generation;
static int active_slot = -1;
static int32_t scan_error;
static bool scanning, verifying, has_saved;
static uint32_t last_stat_log, verify_bytes, verify_crc;
static cat_header_t pending_header;
static uint8_t verify_chunk[4096];
/* Only 4 bytes per song for fast index lookup, not 152-byte path arrays.
 * The active snapshot is hydrated cooperatively; scan fills the second map. */
static uint32_t hashes_a[HIFI_CATALOG_TRACK_MAX];
static uint32_t hashes_b[HIFI_CATALOG_TRACK_MAX];
static uint32_t *active_hashes = hashes_a, *staging_hashes = hashes_b;
static uint32_t active_hash_loaded;
static bool active_hash_failed;

/* Aster's minimal libc intentionally has no memchr/strnlen imports. */
static bool path_terminated(const char *text, unsigned capacity)
{
    for (unsigned i = 0; i < capacity; ++i)
        if (text[i] == '\0')
            return true;
    return false;
}

static bool read_exact(aster_handle_t file, void *dst, unsigned size)
{
    unsigned done = 0;
    while (done < size) {
        int got = aster_fs_read(file, (uint8_t *)dst + done, size - done);
        if (got <= 0) return false;
        done += (unsigned)got;
    }
    return true;
}

static bool write_exact(aster_handle_t file, const void *src, unsigned size)
{
    unsigned done = 0;
    while (done < size) {
        int got = aster_fs_write(file, (const uint8_t *)src + done, size - done);
        if (got <= 0) return false;
        done += (unsigned)got;
    }
    return true;
}

static uint32_t crc_step(uint32_t crc, const void *data, unsigned len)
{
    const uint8_t *p = data;
    for (unsigned i = 0; i < len; i++) {
        crc ^= p[i];
        for (unsigned k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t name_hash(const char *name)
{
    /* FNV-1a: a hash match always performs a full path comparison. */
    uint32_t h = 2166136261u;
    while (*name) {
        h = (h ^ (uint8_t)*name++) * 16777619u;
    }
    return h;
}

static uint32_t row_fingerprint(const cat_row_t *row)
{
    uint32_t crc = crc_step(0xffffffffu, row->name,
                             (unsigned)strlen(row->name));
    crc = crc_step(crc, &row->size, sizeof(row->size));
    return ~crc;
}

static bool music_extension(const char *name)
{
    const char *dot = NULL;
    for (const char *p = name; *p; p++)
        if (*p == '.') dot = p;
    if (!dot) return false;
    char ext[6] = {0};
    unsigned len = (unsigned)strlen(dot + 1);
    if (!len || len >= sizeof(ext)) return false;
    for (unsigned i = 0; i < len; i++) {
        char ch = dot[i + 1];
        ext[i] = ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
    }
    return !strcmp(ext, "mp3") || !strcmp(ext, "flac") ||
           !strcmp(ext, "wav");
}

static void close_stack(void)
{
    while (depth >= 0) {
        if (stack[depth].handle)
            aster_fs_closedir(stack[depth].handle);
        stack[depth].handle = 0;
        depth--;
    }
}

void hifi_catalog_close(void)
{
    close_stack();
    if (staging) aster_fs_close(staging);
    if (active) aster_fs_close(active);
    active = staging = 0;
    active_slot = -1;
    scanning = verifying = has_saved = false;
    discovered = verify_bytes = active_hash_loaded = 0;
    active_hash_failed = false;
    active_hashes = hashes_a;
    staging_hashes = hashes_b;
    generation = 0;
}

void hifi_catalog_drop_snapshots(void)
{
    /* Never touch /music, the APP's other /data files, or installed assets. */
    hifi_catalog_close();
    for (unsigned i = 0; i < 2u; ++i)
        (void)aster_fs_unlink(paths[i], strlen(paths[i]));
}

static int open_valid_snapshot(int slot, cat_header_t *header,
                               aster_handle_t *handle)
{
    aster_handle_t file = 0;
    aster_stat_t stat = {.size = sizeof(stat), .version = 1};
    const char *path = paths[slot];
    if (aster_fs_stat(path, strlen(path), &stat, sizeof(stat)) < 0 ||
        aster_fs_open(path, strlen(path), ASTER_FS_O_RDONLY, &file) < 0)
        return 0;
    bool read_ok = read_exact(file, header, sizeof(*header));
    uint32_t hcrc = ~crc_step(0xffffffffu, header,
                                     sizeof(*header) - sizeof(header->header_crc));
    if (!read_ok || header->magic != CAT_MAGIC ||
        header->version != CAT_VERSION || header->count > HIFI_CATALOG_TRACK_MAX ||
        header->header_crc != hcrc ||
        stat.filesize != (uint64_t)sizeof(*header) +
                             (uint64_t)header->count * sizeof(cat_row_t)) {
        aster_fs_close(file);
        return 0;
    }
    *handle = file;
    return 1;
}

static bool enter_directory(const char *path)
{
    if (depth + 1 >= CAT_DEPTH) return false;
    aster_handle_t dir = 0;
    if (aster_fs_opendir(path, strlen(path), &dir) < 0) return false;
    depth++;
    stack[depth].handle = dir;
    aster_snprintf(stack[depth].path, sizeof(stack[depth].path), "%s", path);
    return true;
}

static int candidate_slot(void)
{
    return active_slot == 0 ? 1 : 0;
}

static void discard_staging(void)
{
    verifying = false;
    if (staging) aster_fs_close(staging);
    staging = 0;
    int slot = candidate_slot();
    (void)aster_fs_unlink(paths[slot], strlen(paths[slot]));
}

static void finish_scan(bool success)
{
    close_stack();
    scanning = false;
    if (!staging) return;
    if (!success) {
        discard_staging();
        return; /* last valid snapshot stays published */
    }
    if (has_saved && committed.count == discovered &&
        committed.signature_xor == sig_xor &&
        committed.signature_sum == sig_sum) {
        /* Logical path/size set has not changed; no new generation. */
        discard_staging();
        return;
    }
    pending_header = (cat_header_t){
        .magic = CAT_MAGIC, .version = CAT_VERSION,
        .generation = generation + 1u, .count = discovered,
        .payload_crc = ~record_crc, .signature_xor = sig_xor,
        .signature_sum = sig_sum, .flags = 0
    };
    pending_header.header_crc = ~crc_step(0xffffffffu, &pending_header,
                    sizeof(pending_header) - sizeof(pending_header.header_crc));
    /* Keep the on-disk header invalid until the full record payload has
     * been read back and verified. A sudden power loss must never publish
     * an unverified staging file ahead of the previous good snapshot. */
    verify_bytes = 0;
    verify_crc = 0xffffffffu;
    verifying = true;
}

static void verify_tick(void)
{
    if (!verifying) return;
    uint32_t total = discovered * (uint32_t)sizeof(cat_row_t);
    unsigned count = total - verify_bytes;
    if (count > sizeof(verify_chunk)) count = sizeof(verify_chunk);
    if (count) {
        uint32_t pos = (uint32_t)sizeof(cat_header_t) + verify_bytes;
        if (aster_fs_seek(staging, (int32_t)pos, ASTER_FS_SEEK_SET) < 0 ||
            !read_exact(staging, verify_chunk, count)) {
            scan_error = ASTER_EIO;
            discard_staging();
            return;
        }
        verify_crc = crc_step(verify_crc, verify_chunk, count);
        verify_bytes += count;
    }
    if (verify_bytes != total) return;
    if ((~verify_crc) != pending_header.payload_crc) {
        scan_error = ASTER_ECORRUPT;
        discard_staging();
        return;
    }
    /* Publish the valid header as the LAST write, after CRC validation. */
    if (aster_fs_seek(staging, 0, ASTER_FS_SEEK_SET) < 0 ||
        !write_exact(staging, &pending_header, sizeof(pending_header))) {
        scan_error = ASTER_EIO;
        discard_staging();
        return;
    }
    int slot = candidate_slot();
    if (active) aster_fs_close(active);
    active = staging;
    staging = 0;
    committed = pending_header;
    active_slot = slot;
    uint32_t *old_hashes = active_hashes;
    active_hashes = staging_hashes;
    staging_hashes = old_hashes;
    active_hash_loaded = discovered;
    active_hash_failed = false;
    generation = committed.generation;
    has_saved = true;
    verifying = false;
    aster_printf("HiFi catalog committed tracks=%u generation=%u",
                 discovered, generation);
}

void hifi_catalog_start(void)
{
    hifi_catalog_close();
    scan_error = 0;
    cat_header_t a = {0}, b = {0};
    aster_handle_t ha = 0, hb = 0;
    int va = open_valid_snapshot(0, &a, &ha);
    int vb = open_valid_snapshot(1, &b, &hb);
    if (va && (!vb || (int32_t)(a.generation - b.generation) >= 0)) {
        active = ha;
        active_slot = 0;
        committed = a;
        generation = a.generation;
        if (hb) aster_fs_close(hb);
    } else if (vb) {
        active = hb;
        active_slot = 1;
        committed = b;
        generation = b.generation;
        if (ha) aster_fs_close(ha);
    }
    has_saved = active != 0;
    /* Never truncate the published slot before the replacement is complete. */
    int stage_slot = candidate_slot();
    (void)aster_fs_mkdir("/data", 5);
    scan_error = aster_fs_open(paths[stage_slot], strlen(paths[stage_slot]),
                   ASTER_FS_O_RDWR | ASTER_FS_O_CREAT | ASTER_FS_O_TRUNC,
                   &staging);
    if (scan_error < 0) return;
    cat_header_t blank = {0};
    if (!write_exact(staging, &blank, sizeof(blank))) {
        scan_error = ASTER_EIO;
        finish_scan(false);
        return;
    }
    record_crc = 0xffffffffu;
    sig_xor = sig_sum = discovered = 0;
    if (!enter_directory("/music")) {
        scan_error = ASTER_ENOENT;
        finish_scan(false);
        return;
    }
    scanning = true;
    aster_printf("HiFi catalog scan start cached=%u",
                 has_saved ? committed.count : 0);
}

void hifi_catalog_tick(void)
{
    /* Hydrate 16 consecutive records per tick with ONE seek/read instead of
     * thousands of single-sector seeks. The steady-state RAM index uses
     * just four bytes per track; the file still owns all long path strings. */
    if (active && !active_hash_failed && active_hash_loaded < committed.count) {
        cat_row_t group[16];
        uint32_t count = committed.count - active_hash_loaded;
        if (count > 16u) count = 16u;
        uint32_t offset = (uint32_t)sizeof(cat_header_t) +
                          active_hash_loaded * (uint32_t)sizeof(cat_row_t);
        if (aster_fs_seek(active, (int32_t)offset, ASTER_FS_SEEK_SET) < 0 ||
            !read_exact(active, group, count * sizeof(cat_row_t))) {
            active_hash_failed = true;
            active_hash_loaded = 0;
        } else {
            for (uint32_t i = 0; i < count; i++) {
                if (!path_terminated(group[i].name, sizeof(group[i].name))) {
                    active_hash_failed = true;
                    active_hash_loaded = 0;
                    break;
                }
                active_hashes[active_hash_loaded++] = name_hash(group[i].name);
            }
        }
    }
    if (verifying) {
        verify_tick();
        return;
    }
    if (!scanning) return;
    uint32_t begun = aster_time_now_ms();
    for (unsigned i = 0; i < CAT_STEP_ENTRIES && scanning; i++) {
        if (aster_time_now_ms() - begun >= CAT_STEP_MS) break;
        if (depth < 0) {
            finish_scan(true);
            break;
        }
        char name[256] = {0};
        uint32_t is_dir = 0;
        int n = aster_fs_dir_next(stack[depth].handle, name,
                                  sizeof(name) - 1u, &is_dir);
        if (n < 0) {
            scan_error = n;
            finish_scan(false);
            break;
        }
        if (!n) {
            aster_fs_closedir(stack[depth].handle);
            stack[depth].handle = 0;
            depth--;
            if (depth < 0) finish_scan(true);
            continue;
        }
        if ((unsigned)n >= sizeof(name)) continue;
        name[n] = 0;
        if (!strcmp(name, ".") || !strcmp(name, "..") ||
            name[0] == '.' || !strcmp(name, "_FakePod"))
            continue;
        const char *parent = stack[depth].path;
        unsigned base = (unsigned)strlen(parent);
        if (base + 1u + (unsigned)n >= sizeof(stack[depth].path))
            continue;
        char full[256];
        aster_snprintf(full, sizeof(full), "%s/%s", parent, name);
        if (is_dir) {
            /* Avoid silently committing an incomplete catalog. */
            if (depth + 1 >= CAT_DEPTH) {
                scan_error = ASTER_ENOMEM;
                finish_scan(false);
            } else if (!enter_directory(full)) {
                scan_error = ASTER_EIO;
                finish_scan(false);
            }
            continue;
        }
        if (!music_extension(name)) continue;
        if (discovered >= HIFI_CATALOG_TRACK_MAX) {
            scan_error = ASTER_ENOMEM;
            finish_scan(false);
            break;
        }
        aster_stat_t stat = {.size = sizeof(stat), .version = 1};
        if (aster_fs_stat(full, strlen(full), &stat, sizeof(stat)) < 0 ||
            (stat.mode & ASTER_STAT_DIR))
            continue;
        cat_row_t row = {0};
        row.size = stat.filesize;
        if (strlen(full) <= 7 || strlen(full + 7) >= sizeof(row.name))
            continue;
        aster_snprintf(row.name, sizeof(row.name), "%s", full + 7);
        uint32_t spot = sizeof(cat_header_t) + discovered * sizeof(row);
        if (spot > INT32_MAX ||
            aster_fs_seek(staging, (int32_t)spot, ASTER_FS_SEEK_SET) < 0 ||
            !write_exact(staging, &row, sizeof(row))) {
            scan_error = ASTER_EIO;
            finish_scan(false);
            break;
        }
        staging_hashes[discovered] = name_hash(row.name);
        uint32_t h = row_fingerprint(&row);
        sig_xor ^= h;
        sig_sum += h * 0x9e3779b1u;
        record_crc = crc_step(record_crc, &row, sizeof(row));
        discovered++;
        if (discovered - last_stat_log >= 256u) {
            last_stat_log = discovered;
            aster_printf("HiFi catalog progress=%u", discovered);
        }
    }
}

hifi_catalog_status_t hifi_catalog_status(void)
{
    hifi_catalog_status_t state = {
        .count = active ? committed.count : discovered,
        .discovered = discovered,
        .generation = generation,
        .error = scan_error,
        .scanning = scanning || verifying,
        .has_saved = has_saved
    };
    return state;
}

bool hifi_catalog_path_at(uint32_t index, char *out, unsigned capacity)
{
    if (!out || capacity == 0u) return false;
    out[0] = 0;
    aster_handle_t file = active ? active : staging;
    uint32_t count = active ? committed.count : discovered;
    if (!file || index >= count || capacity < HIFI_CATALOG_PATH_MAX)
        return false;
    uint32_t pos = (uint32_t)sizeof(cat_header_t) + index * (uint32_t)sizeof(cat_row_t);
    cat_row_t row;
    if (pos > INT32_MAX || aster_fs_seek(file, (int32_t)pos, ASTER_FS_SEEK_SET) < 0 ||
        !read_exact(file, &row, sizeof(row)) ||
        !path_terminated(row.name, sizeof(row.name)) || row.name[0] == '/' ||
        !row.name[0])
        return false;
    memcpy(out, row.name, sizeof(row.name));
    return true;
}

bool hifi_catalog_lookup_ready(void)
{
    if (active) return !active_hash_failed && active_hash_loaded == committed.count;
    return !scanning && !verifying && discovered > 0;
}

int32_t hifi_catalog_find(const char *relative)
{
    if (!relative || !*relative) return -1;
    uint32_t target = name_hash(relative);
    uint32_t count = active ? active_hash_loaded : discovered;
    const uint32_t *keys = active ? active_hashes : staging_hashes;
    char path[HIFI_CATALOG_PATH_MAX];
    for (uint32_t i = 0; i < count; i++) {
        if (keys[i] == target &&
            hifi_catalog_path_at(i, path, sizeof(path)) &&
            !strcmp(relative, path))
            return (int32_t)i;
    }
    return -1;
}
