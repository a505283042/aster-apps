/* SPDX-License-Identifier: MIT */
#include "lyric_layout.h"
#include "paper.h"
#include "paper_font.h"

typedef struct {
    int x0, y0, x1, y1, em, tilt;
} track_t;

typedef struct {
    unsigned tracks;
    bool vertical;
    track_t path[2];
} composition_t;

/* Endpoints are the centers of the first and last glyph in each reading track.
 * Vertical pairs read top-to-bottom down the right column, then the left. */
static const composition_t compositions[LYRIC_EFFECT_COUNT][LYRIC_VARIANTS] =
    {
        [CUT_PAPER] =
            {
                {2, false, {{70, 145, 388, 152, 77, -4}, {112, 294, 348, 266, 101, 3}}},
                {2, true, {{331, 84, 322, 342, 66, 2}, {154, 119, 140, 337, 84, -4}}},
                {2, false, {{96, 138, 372, 114, 68, 4}, {106, 300, 332, 276, 100, 4}}},
                {2, false, {{66, 108, 371, 209, 80, 12}, {112, 261, 389, 346, 86, 10}}},
                {2, true, {{298, 80, 362, 354, 68, -9}, {115, 122, 163, 359, 82, -9}}},
                {2, false, {{75, 190, 343, 117, 69, -12}, {125, 338, 389, 252, 94, -14}}},
            },
        [FOLD_PLANES] =
            {
                {1, false, {{58, 312, 400, 144, 50, -18}}},
                {1, true, {{232, 52, 232, 406, 50, 0}}},
                {1, false, {{61, 231, 397, 231, 52, 0}}},
                {2, false, {{66, 111, 340, 239, 62, 19}, {141, 251, 391, 351, 72, 17}}},
                {2, true, {{312, 70, 346, 362, 65, -7}, {134, 114, 162, 353, 83, -7}}},
                {2, false, {{67, 204, 345, 142, 67, -10}, {134, 334, 394, 270, 80, -12}}},
            },
        [CAMERA_PUSH] =
            {
                {2, false, {{72, 154, 387, 118, 80, -9}, {108, 301, 334, 255, 97, -11}}},
                {1, false, {{63, 242, 393, 242, 53, 4}}},
                {2, true, {{312, 80, 312, 353, 68, 4}, {143, 128, 143, 348, 83, -3}}},
                {2, false, {{76, 112, 379, 148, 70, 7}, {94, 257, 353, 328, 96, 12}}},
                {1, false, {{61, 306, 399, 158, 53, -16}}},
                {2, true, {{303, 78, 349, 347, 70, -7}, {125, 125, 154, 346, 84, -5}}},
            },
};

bool lyric_layout_vertical(unsigned scene)
{
    return compositions[scene / LYRIC_VARIANTS][scene % LYRIC_VARIANTS].vertical;
}

static int distance(int a, int b) { return a > b ? a - b : b - a; }

pose_t lyric_layout_pose(unsigned scene, unsigned count, unsigned index)
{
    const composition_t *layout = &compositions[scene / LYRIC_VARIANTS][scene % LYRIC_VARIANTS];
    unsigned tracks = count <= 3 ? 1 : layout->tracks;
    if (count > tracks * 8)
        tracks = (count + 7) / 8;
    unsigned row = 0, slot = index, n = (count + tracks - 1) / tracks;
    while (slot >= n) {
        slot -= n;
        row++;
        n = (count + tracks - 1 - row) / tracks;
    }
    track_t path = layout->path[row < layout->tracks ? row : 0];
    if (tracks > layout->tracks) {
        int center = (int)(2 * row + 1) * 384 / (2 * (int)tracks) + 38;
        if (layout->vertical)
            path = (track_t){460 - center, 58, 460 - center, 402, 62, path.tilt / 2};
        else
            path = (track_t){56, center + 8, 404, center - 8, 62, path.tilt / 2};
    } else if (count <= 3) {
        path = layout->vertical ? (track_t){230, 98, 230, 362, 106, path.tilt}
                                : (track_t){94, 247, 366, 213, 108, path.tilt};
    }
    int den = n > 1 ? (int)n - 1 : 2;
    int pos = n > 1 ? (int)slot : 1;
    int x = path.x0 + (path.x1 - path.x0) * pos / den;
    int y = path.y0 + (path.y1 - path.y0) * pos / den;
    int span = layout->vertical ? distance(path.y0, path.y1) : distance(path.x0, path.x1);
    int em = n > 1 && span / den < path.em ? span / den : path.em;
    if (scene == FOLD_PLANES * LYRIC_VARIANTS && slot == n / 2)
        em += 6;
    if (scene == CAMERA_PUSH * LYRIC_VARIANTS + 1)
        y += 13 * (int)(slot % 2);
    return (pose_t){x, y, em, path.tilt};
}

bool lyric_latin(uint32_t cp)
{
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 0xc0 && cp <= 0x24f && cp != 0xd7 && cp != 0xf7);
}

static bool ideograph(uint32_t cp)
{
    return (cp >= 0x3040 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7ff) || cp >= 0x20000;
}

/* Keep an English run (including apostrophes, hyphens and punctuation) together.
 * In mixed lyrics a Han character provides another possible line break. */
static bool word_break(uint32_t left, uint32_t right)
{
    if (ideograph(right))
        return left != '(' && left != 0x201c && left != 0x300a;
    return ideograph(left) && (lyric_latin(right) || (right >= '0' && right <= '9'));
}

typedef struct {
    unsigned first, end, width, gap;
} word_t;

static word_t words[LYRIC_LINE_CAP];

static unsigned collect_words(lyric_glyph_t *glyphs, unsigned count)
{
    unsigned n = 0, gap = 0;
    for (unsigned i = 0; i < count;) {
        if (glyphs[i].codepoint == ' ') {
            gap = glyphs[i].advance;
            glyphs[i].word_end = i + 1;
            i++;
            continue;
        }
        word_t *word = &words[n++];
        word->first = i;
        word->width = 0;
        word->gap = gap;
        gap = 0;
        do {
            word->width += glyphs[i++].advance;
        } while (i < count && glyphs[i].codepoint != ' ' &&
                 !word_break(glyphs[i - 1].codepoint, glyphs[i].codepoint));
        word->end = i;
        for (unsigned j = word->first; j < i; j++)
            glyphs[j].word_end = i;
    }
    return n;
}

static unsigned row_width(unsigned start, unsigned count, unsigned capacity, unsigned *next)
{
    unsigned width = words[start].width, i = start + 1;
    while (i < count && width + words[i].gap + words[i].width <= capacity) {
        width += words[i].gap + words[i].width;
        i++;
    }
    *next = i;
    return width;
}

static unsigned count_rows(unsigned count, unsigned capacity)
{
    unsigned rows = 0;
    for (unsigned i = 0; i < count; rows++)
        row_width(i, count, capacity, &i);
    return rows;
}

void lyric_layout_words(unsigned scene, lyric_glyph_t *glyphs, unsigned count)
{
    enum { WIDTH = 388, HEIGHT = 354 };
    const unsigned n = collect_words(glyphs, count);
    if (!n)
        return;
    unsigned em = 78, rows, capacity;
    for (;;) {
        capacity = WIDTH * PAPER_FONT_EM / em;
        rows = count_rows(n, capacity);
        if (rows * (em + em / 4) <= HEIGHT || em <= 8)
            break;
        em -= 2;
    }
    /* Balance whole words across the chosen number of rows, avoiding a tiny
     * trailing line. A single oversized word gets its own fitted row. */
    unsigned low = 1, high = capacity;
    while (low < high) {
        unsigned middle = (low + high) / 2;
        if (count_rows(n, middle) <= rows)
            high = middle;
        else
            low = middle + 1;
    }
    const unsigned effect = scene / LYRIC_VARIANTS, variant = scene % LYRIC_VARIANTS;
    const int step = (int)(em + em / 4);
    for (unsigned start = 0, row = 0; start < n; row++) {
        unsigned end, width = row_width(start, n, low, &end);
        unsigned row_em = em;
        if (effect == CUT_PAPER && rows > 1 && row + 1 < rows)
            row_em = em * 86 / 100;
        if (row_em * width > WIDTH * PAPER_FONT_EM)
            row_em = WIDTH * PAPER_FONT_EM / width;
        if (!row_em)
            row_em = 1;
        int span = (int)(row_em * width / PAPER_FONT_EM);
        if (span > WIDTH)
            span = WIDTH;
        int x = (460 - span) / 2;
        if (effect == CUT_PAPER)
            x += ((int)((row + variant) % 2) * 2 - 1) * (WIDTH - span) / 5;
        const int center_y = 230 + (2 * (int)row + 1 - (int)rows) * step / 2;
        const int tilt = effect == CUT_PAPER     ? -3
                         : effect == FOLD_PLANES ? ((row + variant) % 2 ? 4 : -4)
                                                 : 0;
        unsigned cursor = 0;
        for (unsigned w = start; w < end; w++) {
            if (w > start)
                cursor += words[w].gap;
            for (unsigned i = words[w].first; i < words[w].end; i++) {
                lyric_glyph_t *g = &glyphs[i];
                int offset = (int)(cursor + g->advance / 2) + g->center_offset;
                int cx = x + offset * span / (int)width;
                g->pose = (pose_t){cx, center_y + (cx - 230) * tilt / 57, row_em, tilt};
                g->row = row;
                cursor += g->advance;
            }
        }
        start = end;
    }
}
