/* SPDX-License-Identifier: MIT
 * FakePod_S31-inspired on-disk music index for the Aster 4.5 sandbox.
 * Independent of LVGL, ESP-IDF and the system playback backend.
 */
#ifndef HIFI_MUSIC_CATALOG_H
#define HIFI_MUSIC_CATALOG_H
#include <stdbool.h>
#include <stdint.h>

enum { HIFI_CATALOG_TRACK_MAX = 16384, HIFI_CATALOG_PATH_MAX = 152 };
typedef struct {
    uint32_t count;       /* Published entries, or scanned entries on first boot. */
    uint32_t discovered;  /* Entries found by the current directory walk. */
    uint32_t generation;  /* Incremented only after a changed snapshot commits. */
    int32_t error;
    bool scanning;
    bool has_saved;
} hifi_catalog_status_t;

void hifi_catalog_start(void);
void hifi_catalog_tick(void);
void hifi_catalog_close(void);
/* Close and delete only app-local snapshots. Reopen with hifi_catalog_start(). */
void hifi_catalog_drop_snapshots(void);
hifi_catalog_status_t hifi_catalog_status(void);
/* Returns the relative UTF-8 path below /music, including nested directories. */
bool hifi_catalog_path_at(uint32_t index, char *out, unsigned capacity);
/* Active snapshot hashes are rebuilt in small slices after startup. */
bool hifi_catalog_lookup_ready(void);
/* O(n) in 4-byte hashes, with an SD read only for rare hash matches. */
int32_t hifi_catalog_find(const char *relative_path);
#endif
