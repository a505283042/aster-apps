/* SPDX-License-Identifier: MIT
 * Folder navigation based on FakePod_S31's separate folder browse model.
 * The Catalog retains all path strings on disk, while this session stores
 * only catalog IDs and immediate folder names (no nested POSIX handles).
 */
#include "music_browser.h"
#include <aster_imports.h>

enum { BROWSER_PATH_MAX = HIFI_CATALOG_PATH_MAX,
       BROWSER_STEP = 12, BROWSER_BUDGET_MS = 5 };

typedef struct {
    char name[BROWSER_PATH_MAX]; /* One immediate child component. */
    uint32_t songs;
} folder_row_t;
static folder_row_t folders[HIFI_BROWSER_MAX_FOLDERS];
static uint32_t files[HIFI_CATALOG_TRACK_MAX];
static uint32_t folder_count, file_count, cursor, known_generation;
static bool building, failed;
static char folder_path[BROWSER_PATH_MAX];

static const char *immediate(const char *full)
{
    unsigned len = (unsigned)strlen(folder_path);
    if (!len) return full;
    if (strncmp(full, folder_path, len) || full[len] != '/')
        return NULL;
    return full + len + 1u;
}

void hifi_browser_open(const char *relative_folder)
{
    folder_path[0] = 0;
    if (relative_folder && *relative_folder) {
        unsigned len = (unsigned)strlen(relative_folder);
        if (len < sizeof(folder_path) && relative_folder[0] != '/' &&
            !strchr(relative_folder, '\\') && !strchr(relative_folder, ':')) {
            aster_snprintf(folder_path, sizeof(folder_path), "%s", relative_folder);
            while (len && folder_path[len - 1] == '/')
                folder_path[--len] = 0;
        }
    }
    folder_count = file_count = cursor = 0;
    known_generation = hifi_catalog_status().generation;
    building = true;
    failed = false;
}

void hifi_browser_up(void)
{
    if (!folder_path[0]) return;
    unsigned len = (unsigned)strlen(folder_path);
    while (len && folder_path[len - 1] != '/')
        len--;
    if (len) len--;
    folder_path[len] = 0;
    char parent[BROWSER_PATH_MAX];
    aster_snprintf(parent, sizeof(parent), "%s", folder_path);
    hifi_browser_open(parent);
}

const char *hifi_browser_folder(void) { return folder_path; }
uint32_t hifi_browser_count(void) { return folder_count + file_count; }
bool hifi_browser_busy(void) { return building; }
bool hifi_browser_error(void) { return failed; }

void hifi_browser_tick(void)
{
    hifi_catalog_status_t state = hifi_catalog_status();
    if (state.generation != known_generation) {
        char keep[BROWSER_PATH_MAX];
        aster_snprintf(keep, sizeof(keep), "%s", folder_path);
        hifi_browser_open(keep);
    }
    if (!building) return;
    uint32_t begin = aster_time_now_ms();
    char relative[HIFI_CATALOG_PATH_MAX];
    for (unsigned work = 0; work < BROWSER_STEP && cursor < state.count; work++) {
        if (aster_time_now_ms() - begin >= BROWSER_BUDGET_MS)
            break;
        if (!hifi_catalog_path_at(cursor, relative, sizeof(relative))) {
            /* A half-written or damaged catalog must not silently produce an
             * incomplete folder view that looks like an empty directory. */
            failed = true;
            building = false;
            return;
        }
        uint32_t source = cursor++;
        const char *suffix = immediate(relative);
        if (!suffix || !*suffix) continue;
        const char *slash = strchr(suffix, '/');
        if (!slash) {
            if (file_count >= HIFI_CATALOG_TRACK_MAX) {
                failed = true;
                building = false;
                return;
            }
            files[file_count++] = source;
            continue;
        }
        unsigned child_len = (unsigned)(slash - suffix);
        if (!child_len || child_len >= BROWSER_PATH_MAX) continue;
        uint32_t slot = 0;
        for (; slot < folder_count; slot++)
            if (strlen(folders[slot].name) == child_len &&
                !strncmp(folders[slot].name, suffix, child_len))
                break;
        if (slot == folder_count) {
            if (folder_count == HIFI_BROWSER_MAX_FOLDERS) {
                failed = true;
                building = false;
                return;
            }
            memcpy(folders[folder_count].name, suffix, child_len);
            folders[folder_count].name[child_len] = 0;
            folders[folder_count].songs = 0;
            folder_count++;
        }
        folders[slot].songs++;
    }
    if (cursor == state.count && !state.scanning)
        building = false;
}

bool hifi_browser_entry(uint32_t row, hifi_browser_entry_t *out)
{
    if (!out) return false;
    *out = (hifi_browser_entry_t){0};
    if (row < folder_count) {
        out->folder = true;
        out->songs = folders[row].songs;
        aster_snprintf(out->name, sizeof(out->name), "%s", folders[row].name);
        return true;
    }
    row -= folder_count;
    if (row >= file_count) return false;
    uint32_t index = files[row];
    char path[HIFI_CATALOG_PATH_MAX];
    if (!hifi_catalog_path_at(index, path, sizeof(path))) return false;
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/') name = p + 1;
    out->folder = false;
    out->track_index = index;
    aster_snprintf(out->name, sizeof(out->name), "%s", name);
    return true;
}
