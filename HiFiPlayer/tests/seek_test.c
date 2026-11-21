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
    assert(timed_cue(1099, camera_cut_ms) == -1);
    assert(timed_cue(1100, camera_cut_ms) == 0);
    paper_line = lyric_line = 0;
    camera_cut_ms = 50;
    assert(timed_cue(1110, camera_cut_ms) == 0); /* A faster measurement cannot undo a visible cut. */
    assert(camera_age(1180) == 0);
    assert(camera_age(1200) == 20);
    assert(lyric_position(1500, 0) == 5000);
    player.state = ASTER_PLAYER_PAUSED;
    assert(timed_cue(1500, camera_cut_ms) == -1 && camera_age(1500) == 0);
    player.state = ASTER_PLAYER_PLAYING;
    seek_position = 9200;
    seeking = SEEK_DRAG;
    assert(timed_cue(1500, camera_cut_ms) == 1 && lyric_position(1500, 80) == 9200);
    paper_line = 1;
    assert(camera_age(1500) == 220);
    seek_position = 15020;
    assert(camera_age(1500) == CAMERA_MOVE_MS);
    seeking = SEEK_IDLE;
    player.mode = ASTER_PLAYER_BLUETOOTH;
    paper_began = 1350;
    assert(lyric_position(1500, 80) == player.position_ms && camera_age(1500) == 150);
    player.mode = ASTER_PLAYER_LOCAL;
    player.position_ms = player.duration_ms - 20;
    assert(lyric_position(1500, 100) == player.duration_ms);
    /* The ordinary page uses the same absolute clock at the start and end
     * of a track. Presentation and polling delays never accumulate. */
    player.duration_ms = 300000;
    lyrics.offset_ms = 0;
    for (unsigned minute = 0; minute < 4; minute++) {
        unsigned base = 60000 * minute;
        player.position_ms = base + 500;
        player_sample_at = base + 900;
        lyrics.cue[0].time_ms = base + 670;
        lyrics.cue[1].time_ms = base + 700;
        lyric_line = -1;
        assert(timed_cue(base + 979, 90) == -1);
        assert(timed_cue(base + 980, 90) == 0);
        assert(timed_cue(base + 1010, 90) == 1);
    }
    return 0;
}
