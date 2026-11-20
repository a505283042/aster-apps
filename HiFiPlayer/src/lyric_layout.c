/* SPDX-License-Identifier: MIT */
#include "lyric_layout.h"
#include "paper.h"

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
