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
    /* The camera schedules a timed local cut for presentation. An explicit
     * seek or pause needs the exact playhead, and remote lines have no clock. */
    lyrics.count = 2;
    lyrics.cue[0].time_ms = 5000;
    lyrics.cue[1].time_ms = 9000;
    lyrics.offset_ms = 20;
    player_sample_at = 1000;
    player.position_ms = 4800;
    player.state = ASTER_PLAYER_PLAYING;
    seeking = SEEK_IDLE;
    camera_cut_ms = 80;
    assert(camera_cue(1099) == -1);
    assert(camera_cue(1100) == 0);
    paper_line = lyric_line = 0;
    camera_cut_ms = 50;
    assert(camera_cue(1110) == 0); /* A faster measurement cannot undo a visible cut. */
    assert(camera_age(1180) == 0);
    assert(camera_age(1200) == 20);
    assert(camera_position(1500, 0) == 5000);
    player.state = ASTER_PLAYER_PAUSED;
    assert(camera_cue(1500) == -1 && camera_age(1500) == 0);
    player.state = ASTER_PLAYER_PLAYING;
    seek_position = 9200;
    seeking = SEEK_DRAG;
    assert(camera_cue(1500) == 1 && camera_position(1500, 80) == 9200);
    paper_line = 1;
    assert(camera_age(1500) == 220);
    seek_position = 15020;
    assert(camera_age(1500) == CAMERA_MOVE_MS);
    seeking = SEEK_IDLE;
    player.mode = ASTER_PLAYER_BLUETOOTH;
    paper_began = 1350;
    assert(camera_position(1500, 80) == player.position_ms && camera_age(1500) == 150);
    player.mode = ASTER_PLAYER_LOCAL;
    player.position_ms = player.duration_ms - 20;
    assert(camera_position(1500, 100) == player.duration_ms);
    return 0;
}
