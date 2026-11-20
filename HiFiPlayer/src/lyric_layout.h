/* SPDX-License-Identifier: MIT */
#ifndef HIFI_LYRIC_LAYOUT_H
#define HIFI_LYRIC_LAYOUT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x, y, em, degrees;
} pose_t;

/* The LRC reader accepts a line of up to 767 UTF-8 bytes. */
enum { LYRIC_LINE_CAP = 768 };
typedef struct {
    uint32_t codepoint;
    pose_t pose;
    uint16_t mask_slot, word_end, row;
    uint8_t advance;
    int8_t center_offset;
} lyric_glyph_t;

bool lyric_latin(uint32_t cp);
void lyric_layout_words(unsigned scene, lyric_glyph_t *glyphs, unsigned count);
bool lyric_layout_vertical(unsigned scene);
pose_t lyric_layout_pose(unsigned scene, unsigned count, unsigned index);
#endif
