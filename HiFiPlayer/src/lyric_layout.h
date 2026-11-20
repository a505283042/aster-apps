/* SPDX-License-Identifier: MIT */
#ifndef HIFI_LYRIC_LAYOUT_H
#define HIFI_LYRIC_LAYOUT_H
#include <stdbool.h>

typedef struct {
    int x, y, em, degrees;
} pose_t;

bool lyric_layout_vertical(unsigned scene);
pose_t lyric_layout_pose(unsigned scene, unsigned count, unsigned index);
#endif
