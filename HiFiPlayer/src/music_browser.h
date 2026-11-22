/* SPDX-License-Identifier: MIT
 * Persisted folder tree HFD1, bound to the committed HIF2 catalog generation.
 * Folder navigation never scans the global catalog.
 */
#ifndef HIFI_MUSIC_BROWSER_H
#define HIFI_MUSIC_BROWSER_H
#include <stdbool.h>
#include <stdint.h>
#include "music_catalog.h"

typedef struct {
    bool folder;
    uint32_t folder_id;   /* Valid only for folders. */
    uint32_t track_index; /* Valid only for files, global HIF2 ID. */
    uint32_t songs;       /* Recursive song count in a folder. */
    char name[HIFI_CATALOG_PATH_MAX];
} hifi_browser_entry_t;

enum hifi_browser_stage {
    HIFI_BROWSER_WAIT, HIFI_BROWSER_LOADING, HIFI_BROWSER_BUILDING,
    HIFI_BROWSER_SAVING, HIFI_BROWSER_READY, HIFI_BROWSER_ERROR
};
typedef struct {
    enum hifi_browser_stage stage;
    uint32_t done, total, folders;
    bool ready, error;
} hifi_browser_progress_t;

void hifi_browser_start(void);
/* Close pending HFD1 I/O before app stop or USB MSC handoff. */
void hifi_browser_close(void);
void hifi_browser_tick(void);
void hifi_browser_open(const char *relative_folder);
bool hifi_browser_enter_folder(uint32_t folder_id);
void hifi_browser_up(void);
const char *hifi_browser_folder(void);
uint32_t hifi_browser_count(void);
bool hifi_browser_busy(void);
bool hifi_browser_error(void);
bool hifi_browser_ready(void);
hifi_browser_progress_t hifi_browser_progress(void);
bool hifi_browser_entry(uint32_t row, hifi_browser_entry_t *out);
#endif
