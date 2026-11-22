/* SPDX-License-Identifier: MIT
 * S31-style folder browsing over Aster's persisted catalog.
 * Does not open new directory handles while the scanner is running.
 */
#ifndef HIFI_MUSIC_BROWSER_H
#define HIFI_MUSIC_BROWSER_H
#include <stdbool.h>
#include <stdint.h>
#include "music_catalog.h"

enum { HIFI_BROWSER_MAX_FOLDERS = 384 };
typedef struct {
    bool folder;
    uint32_t track_index; /* Valid only for files; catalog index, not row index. */
    uint32_t songs;       /* Song count under a folder, including descendants. */
    char name[HIFI_CATALOG_PATH_MAX]; /* Child name without parent directory. */
} hifi_browser_entry_t;

void hifi_browser_open(const char *relative_folder);
void hifi_browser_up(void);
void hifi_browser_tick(void);
const char *hifi_browser_folder(void);
uint32_t hifi_browser_count(void);
bool hifi_browser_busy(void);
bool hifi_browser_error(void);
bool hifi_browser_entry(uint32_t row, hifi_browser_entry_t *out);
#endif
