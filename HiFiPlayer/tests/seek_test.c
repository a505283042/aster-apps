/* SPDX-License-Identifier: MIT */
/* Build with -ffunction-sections -fdata-sections -Wl,--gc-sections. */
#include "../src/main.c"
#include <assert.h>

int32_t aster_audio_set_volume(int32_t percent) { return percent; }

int main(void)
{
    player.mode = ASTER_PLAYER_BLUETOOTH;
    player.flags = ASTER_PLAYER_F_POSITION;
    player.duration_ms = 180000;
    player.position_ms = 25000;
    page = PLAYBACK;
    assert(hit(PLAY_SEEK_X + PLAY_SEEK_W / 2, PLAY_PROGRESS) == NONE);
    slider(SEEK, PLAY_SEEK_X + PLAY_SEEK_W);
    assert(seeking == SEEK_IDLE && seek_position == 0);
    assert(position() == 25000);
    page = LYRICS;
    assert(hit(PROGRESS_X + PROGRESS_W / 2, LYRIC_PROGRESS) == NONE);
    slider(SEEK, PROGRESS_X + PROGRESS_W / 2);
    player.position_ms = 26000;
    assert(position() == 26000 && seeking == SEEK_IDLE);

    player.mode = ASTER_PLAYER_LOCAL;
    player.flags = ASTER_PLAYER_F_BT_OUTPUT;
    page = PLAYBACK;
    assert(hit(PLAY_SEEK_X + PLAY_SEEK_W / 2, PLAY_PROGRESS) == SEEK);
    page = LYRICS;
    assert(hit(PROGRESS_X + PROGRESS_W / 2, LYRIC_PROGRESS) == SEEK);
    slider(SEEK, PROGRESS_X + PROGRESS_W / 2);
    assert(position() == 90000);
    seeking = SEEK_WAIT;
    assert(position() == 90000);
    return 0;
}
