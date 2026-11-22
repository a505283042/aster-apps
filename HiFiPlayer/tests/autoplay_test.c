/* SPDX-License-Identifier: MIT */
/* Link with aster_printf.c and --gc-sections, as for the seek test. */
#include "../src/main.c"
#include <assert.h>

static int result;
static uint32_t requested_output;

int32_t aster_player_control(const aster_player_request_t *request, uint32_t size)
{
    assert(size == sizeof(*request) && request->command == ASTER_PLAYER_PLAY_FILE);
    assert(!strcmp(request->path, queued_file));
    requested_output = request->value;
    return result;
}

int32_t aster_player_bt_list(aster_player_bt_list_t *list, uint32_t size)
{
    assert(size == sizeof(*list));
    list->state = ASTER_PLAYER_BT_CONNECTED;
    list->output_rate = 44100;
    return 0;
}

uint32_t aster_time_now_ms(void)
{
    return 4000;
}

static int sample(uint32_t state, const char *path)
{
    player.state = state;
    aster_snprintf(player.path, sizeof(player.path), "%s", path);
    return automatic_next();
}

int main(void)
{
    const char *a = "/SD:/music/a.flac", *b = "/SD/music/b.mp3";
    assert(track_append("a.flac", sizeof("a.flac") - 1));
    assert(track_append("b.mp3", sizeof("b.mp3") - 1));
    library_scanned = true;
    player.mode = ASTER_PLAYER_LOCAL;
    player.capabilities = ASTER_PLAYER_CAP_SYSTEM_OUTPUT;
    assert(output() == ASTER_PLAYER_OUTPUT_SYSTEM);
    player.capabilities = 0;
    assert(output() == ASTER_PLAYER_OUTPUT_BLUETOOTH); /* Earlier firmware. */

    assert(sample(ASTER_PLAYER_ENDED, a) == -1); /* Reopened after the song ended. */
    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    assert(sample(ASTER_PLAYER_PAUSED, a) == -1);
    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    assert(sample(ASTER_PLAYER_ENDED, a) == 1);
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);

    memcpy(queued_file, "/music/b.mp3", sizeof("/music/b.mp3"));
    result = ASTER_EBUSY;
    start_queued(300);
    assert(queued_file[0] && !message[0]);
    result = 0;
    start_queued(450);
    assert(!queued_file[0] && requested_output == ASTER_PLAYER_OUTPUT_BLUETOOTH);
    assert(sample(ASTER_PLAYER_ENDED, b) == 0); /* Very short file, wrap once. */
    assert(sample(ASTER_PLAYER_ENDED, b) == -1);

    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    library_scanned = false;
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    library_scanned = true;
    seeking = SEEK_DRAG;
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    seeking = SEEK_IDLE;
    assert(sample(ASTER_PLAYER_ENDED, a) == 1);

    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    assert(sample(ASTER_PLAYER_STOPPING, a) == -1);
    assert(sample(ASTER_PLAYER_IDLE, a) == -1);
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    assert(sample(ASTER_PLAYER_ERROR, a) == -1);
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    track_count = 1;
    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    track_count = 2;
    assert(sample(ASTER_PLAYER_PLAYING, "/assets/audio/demo.flac") == -1);
    assert(sample(ASTER_PLAYER_ENDED, "/assets/audio/demo.flac") == -1);
    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    player.mode = ASTER_PLAYER_BLUETOOTH;
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    player.mode = ASTER_PLAYER_LOCAL;
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);

    /* New firmware owns auto-next (including outside the APP). */
    player.capabilities |= ASTER_PLAYER_CAP_AUTO_NEXT;
    assert(sample(ASTER_PLAYER_PLAYING, a) == -1);
    assert(sample(ASTER_PLAYER_ENDED, a) == -1);
    player.capabilities &= ~ASTER_PLAYER_CAP_AUTO_NEXT;

    /* The APP library is not truncated at 64 tracks; both the manual and
     * legacy-firmware automatic successor must cross the old boundary. */
    tracks_clear();
    char name[32], path[80];
    for (int i = 0; i < 80; i++) {
        aster_snprintf(name, sizeof(name), "song%03d.flac", i);
        assert(track_append(name, strlen(name)));
    }
    assert(track_count == 80);
    assert(track_used > 0);
    aster_snprintf(path, sizeof(path), "/SD:/music/song063.flac");
    assert(sample(ASTER_PLAYER_PLAYING, path) == -1);
    assert(current_index() == 63);
    assert(sample(ASTER_PLAYER_ENDED, path) == 64);
    aster_snprintf(path, sizeof(path), "/SD:/music/song079.flac");
    assert(sample(ASTER_PLAYER_PLAYING, path) == -1);
    assert(sample(ASTER_PLAYER_ENDED, path) == 0);
    tracks_clear();
    return 0;
}
