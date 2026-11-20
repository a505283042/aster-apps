/* SPDX-License-Identifier: MIT */
/* gcc -std=c11 -Isrc -I../../aster-sdk/include -I../../aster-sdk/lib/aster
 * tests/cover_jpeg_test.c src/cover_jpeg.c vendor/tjpgd/tjpgd.c */
#include "cover_jpeg.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { PREFIX = 37 };
static uint8_t song[100000];
static unsigned tail, pos, ticks;
static _Alignas(8) uint8_t work[COVER_WORK_BYTES];
static uint16_t canvas[COVER_EDGE * COVER_EDGE + 2];

static void byte(unsigned n)
{
    assert(tail < sizeof(song));
    song[tail++] = n;
}

static void word(unsigned n)
{
    byte(n >> 8);
    byte(n);
}

static void marker(unsigned n)
{
    byte(0xff);
    byte(n);
}

static void table(unsigned kind)
{
    marker(0xc4);
    word(kind ? 20 : 21);
    byte(kind);
    for (int i = 0; i < 16; i++)
        byte(i == 0 || (!kind && i == 1));
    byte(0);
    if (!kind)
        byte(6);
}

/* Baseline grayscale JPEG in a FLAC-like byte range. A restart marker at
 * every MCU exercises the entropy state across slices. Alternate DC values
 * spatially so a missing row, stride error or incorrect restart is visible. */
static void fixture(unsigned w, unsigned h)
{
    tail = PREFIX;
    marker(0xd8);
    marker(0xe1);
    word(5002);
    memset(song + tail, 0xaa, 5000);
    tail += 5000;

    marker(0xdb);
    word(67);
    byte(0);
    for (unsigned i = 0; i < 64; i++)
        byte(8);

    marker(0xc0);
    word(11);
    byte(8);
    word(h);
    word(w);
    byte(1);
    byte(1);
    byte(0x11);
    byte(0);
    table(0);
    table(0x10);

    marker(0xdd);
    word(4);
    word(1);
    marker(0xda);
    word(8);
    byte(1);
    byte(1);
    byte(0);
    byte(0);
    byte(63);
    byte(0);
    unsigned seq = 0;
    for (unsigned y = 0; y < h; y += 8) {
        for (unsigned x = 0; x < w; x += 8) {
            if (seq)
                marker(0xd0 + ((seq - 1) & 7));
            if (((x / 8) ^ (y / 8)) & 1) {
                byte(0xa0);
                byte(0x7f);
            } else {
                byte(0x3f);
            }
            seq++;
        }
    }
    marker(0xd9);
    assert(tail < sizeof(song));
}

int32_t aster_fs_read(aster_handle_t f, void *dst, uint32_t n)
{
    assert(f == 1 && pos >= PREFIX && n <= tail - pos);
    if (n > 179)
        n = 179; /* Deliberately short reads. */
    memcpy(dst, song + pos, n);
    pos += n;
    return n;
}

int32_t aster_fs_seek(aster_handle_t f, int32_t at, uint32_t whence)
{
    assert(f == 1 && whence == ASTER_FS_SEEK_SET && at >= PREFIX && (unsigned)at <= tail);
    pos = at;
    return 0;
}

uint32_t aster_time_now_ms(void)
{
    return ticks++;
}

static void check(unsigned w, unsigned h)
{
    fixture(w, h);
    memset(canvas, 0xff, sizeof(canvas));
    assert(cover_jpeg_begin(1, PREFIX, tail - PREFIX, COVER_EDGE, canvas + 1, work));
    unsigned ow = 0, oh = 0, slices = 0;
    int result;
    do {
        result = cover_jpeg_step(&ow, &oh);
        slices++;
    } while (!result);
    assert(result == 1 && ow && oh && ow <= COVER_EDGE && oh <= COVER_EDGE);
    unsigned edge = w > h ? w : h;
    unsigned target = edge < COVER_EDGE ? edge : COVER_EDGE;
    assert(ow == (w * target + edge / 2) / edge && oh == (h * target + edge / 2) / edge);
    unsigned shift = 0;
    while (shift < 3 && (w >> (shift + 1)) >= ow && (h >> (shift + 1)) >= oh)
        shift++;
    for (unsigned y = 0; y < oh; y++) {
        for (unsigned x = 0; x < ow; x++) {
            unsigned sx = (((2 * x + 1) * (w >> shift)) / (2 * ow)) << shift;
            unsigned sy = (((2 * y + 1) * (h >> shift)) / (2 * oh)) << shift;
            unsigned grey = ((sx / 8) ^ (sy / 8)) & 1 ? 160 : 128;
            unsigned rgb565 = (grey >> 3) << 11 | (grey >> 2) << 5 | (grey >> 3);
            assert(canvas[1 + y * ow + x] == rgb565);
        }
    }
    assert(canvas[0] == 0xffff && canvas[1 + ow * oh] == 0xffff);
    if (w > 500)
        assert(slices > 1);
}

int main(void)
{
    check(1243, 601);
    check(81, 133);
    check(241, 1800);
    fixture(1243, 601);
    assert(cover_jpeg_begin(1, PREFIX, tail - PREFIX, COVER_EDGE, canvas, work));
    unsigned w, h;
    assert(cover_jpeg_step(&w, &h) == 0);
    check(17, 9); /* Switching tracks drops a partially decoded image. */
    fixture(193, 141);
    tail -= 220;
    assert(cover_jpeg_begin(1, PREFIX, tail - PREFIX, COVER_EDGE, canvas, work));
    int result;
    do {
        result = cover_jpeg_step(&w, &h);
    } while (!result);
    assert(result < 0);
    puts("sliced JPEG covers passed");
}
