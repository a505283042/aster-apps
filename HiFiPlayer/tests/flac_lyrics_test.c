/* SPDX-License-Identifier: MIT */
/* Exercise the same file-selection and frame reader used on the device. */
#include "../src/main.c"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static unsigned char song[65536];
static unsigned size, cursor[3], closed, bytes_read;
static const char *sidecar;

int aster_snprintf(char *s, int n, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(s, n, fmt, args);
    va_end(args);
    return written;
}

void aster_logf(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

int32_t aster_gfx_text_w(uint32_t px, const char *utf8, uint32_t n)
{
    (void)px;
    (void)utf8;
    assert(n);
    return 12;
}

int32_t aster_fs_stat(const char *path, uint32_t len, aster_stat_t *st, uint32_t n)
{
    (void)path;
    (void)len;
    (void)n;
    st->filesize = size;
    return 0;
}

int32_t aster_fs_open(const char *path, uint32_t len, uint32_t flags, aster_handle_t *out)
{
    (void)len;
    assert(flags == ASTER_FS_O_RDONLY);
    if (strstr(path, ".lrc") && sidecar)
        *out = 1;
    else if (strstr(path, ".flac"))
        *out = 2;
    else
        return ASTER_ENOENT;
    cursor[*out] = 0;
    return 0;
}

int32_t aster_fs_seek(aster_handle_t f, int32_t at, uint32_t mode)
{
    assert(f == 2 && mode == ASTER_FS_SEEK_SET && at >= 0);
    cursor[f] = at;
    return 0;
}

int32_t aster_fs_read(aster_handle_t f, void *dst, uint32_t len)
{
    unsigned end = f == 1 ? strlen(sidecar) : size;
    if (cursor[f] >= end)
        return 0;
    if (len > 53)
        len = 53; /* Short reads, including inside tag headers. */
    if (len > end - cursor[f])
        len = end - cursor[f];
    memcpy(dst, (f == 1 ? (const unsigned char *)sidecar : song) + cursor[f], len);
    cursor[f] += len;
    bytes_read += len;
    return len;
}

int32_t aster_fs_close(aster_handle_t f)
{
    assert(f == 1 || f == 2);
    closed++;
    return 0;
}

static void le32(unsigned v)
{
    for (unsigned i = 0; i < 4; i++)
        song[size++] = v >> (8 * i);
}

static unsigned block(unsigned type, unsigned n)
{
    unsigned at = size;
    song[size++] = type;
    song[size++] = n >> 16;
    song[size++] = n >> 8;
    song[size++] = n;
    return at;
}

static unsigned fixture(const char **fields, unsigned count)
{
    memset(song, 0, sizeof(song));
    memcpy(song, "fLaC", 4);
    size = 4;
    block(0, 34);
    size += 34;
    block(6, 18000);
    size += 18000;
    unsigned at = block(0x84, 0);
    le32(3);
    memcpy(song + size, "tag", 3);
    size += 3;
    le32(count);
    for (unsigned i = 0; i < count; i++) {
        unsigned n = strlen(fields[i]);
        le32(n);
        memcpy(song + size, fields[i], n);
        size += n;
    }
    unsigned length = size - at - 4;
    song[at + 2] = length >> 8;
    song[at + 3] = length;
    return at;
}

static void load(const char *lrc)
{
    lyrics_track("");
    sidecar = lrc;
    bytes_read = 0;
    lyrics_track("/music/test.flac");
    for (unsigned i = 0; i < 4096 && lyric_file; i++)
        lyrics_read();
    assert(!lyric_file && bytes_read < 12000);
}

int main(void)
{
    const char *tags[] = {"TITLE=LYRICS=fake", "lyrics=First line\r\n第二句",
                          "sYnCeDlYrIcS=[00:01.50]First\n[00:08.00]<00:08.10>第二句"};
    fixture(tags, 3);
    load(NULL);
    assert(lyrics.count == 2 && !plain_lyrics && lrc_active(&lyrics, 1499) == -1);
    assert(lrc_active(&lyrics, 1500) == 0 && lrc_active(&lyrics, 8000) == 1);
    assert(lyrics.cue[1].word_count == 1);
    load("invalid sidecar");
    assert(lyrics.count == 2 && !plain_lyrics);
    load("[00:00]My edit");
    assert(lyrics.count == 1 && !strcmp(lyrics.text, "My edit"));
    fixture(tags, 2);
    load(NULL);
    assert(plain_lyrics && lyrics.count == 2);
    assert(!strcmp(lyrics.text, "First line"));
    assert(!strcmp(lyrics.text + lyrics.cue[1].text_offset, "第二句"));
    content_w = 60; /* Narrow the viewport to exercise a wrapped paragraph. */
    scroll_plain(1);
    assert(!plain_cue && plain_byte > 0);
    scroll_plain(-1);
    assert(!plain_cue && !plain_byte);
    scroll_plain(12);
    assert(plain_cue == 1);
    scroll_plain(-12);
    assert(!plain_cue && !plain_byte);
    fixture(tags, 1);
    load(NULL);
    assert(!lyric_error && !lyrics.count);
    unsigned at = fixture(tags, 3);
    song[at + 1] = 127; /* Huge last block outside this file. */
    load(NULL);
    assert(lyric_error && !lyrics.count);
    at = fixture(tags, 3);
    song[at + 4] = 0xff; /* Vendor crosses the end of this block. */
    load(NULL);
    assert(lyric_error && !lyrics.count);
    fixture(tags, 3);
    lyrics_track("");
    lyrics_track("/music/test.flac");
    assert(lyric_file);
    unsigned before = closed;
    lyrics_track("@bluetooth");
    assert(!lyric_file && closed == before + 1 && !lyrics.count && !plain_lyrics);
    puts("embedded FLAC lyric reader passed");
    return 0;
}
