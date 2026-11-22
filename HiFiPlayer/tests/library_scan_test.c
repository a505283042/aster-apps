/* SPDX-License-Identifier: MIT */
/* Link as a host test with --gc-sections, like autoplay_test.c. */
#include "../src/main.c"
#include <assert.h>
#include <stdio.h>

static int listed;
static int closed;

int32_t aster_fs_opendir(const char *path, uint32_t length, aster_handle_t *out)
{
    assert(length == 6 && !strcmp(path, "/music"));
    *out = 1;
    listed = 0;
    return 0;
}

int32_t aster_fs_dir_next(aster_handle_t handle, char *name, uint32_t capacity, uint32_t *is_dir)
{
    assert(handle == 1);
    if (listed == 140)
        return 0;
    int length = snprintf(name, capacity, "song%03d.flac", listed++);
    *is_dir = 0;
    return length;
}

int32_t aster_fs_closedir(aster_handle_t handle)
{
    assert(handle == 1);
    closed++;
    return 0;
}

int main(void)
{
    scan_start();
    assert(!library_scanned && track_count == 0);
    while (!library_scanned)
        scan_tick();
    assert(closed == 1 && track_count == 140 && scan_error == 0);
    assert(track_capacity >= 140);
    assert(!strcmp(tracks[0], "song000.flac"));
    assert(!strcmp(tracks[63], "song063.flac"));
    assert(!strcmp(tracks[64], "song064.flac"));
    assert(!strcmp(tracks[139], "song139.flac"));
    assert(page_count() == 28);
    player.mode = ASTER_PLAYER_LOCAL;
    aster_snprintf(player.path, sizeof(player.path), "/SD:/music/song139.flac");
    assert(current_index() == 139);
    change_library_page(27);
    assert(library_page == 27);
    scan_start();
    assert(track_count == 0 && library_page == 0 && !library_scanned);
    scan_close();
    tracks_clear();
    return 0;
}
