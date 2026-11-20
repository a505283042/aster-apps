/* SPDX-License-Identifier: MIT */
#ifndef HIFI_PAPER_H
#define HIFI_PAPER_H

#include <aster_gfx_engine.h>
#include "lrc.h"

#define PAPER_TURN_MS 450u
enum lyric_effect { CUT_PAPER, FOLD_PLANES, CAMERA_PUSH, LYRIC_EFFECT_COUNT };
enum { LYRIC_VARIANTS = 6, LYRIC_ENTER_MS = 570, LYRIC_EXIT_MS = 320 };

typedef struct {
    uint32_t age_ms;
    bool leaving;
} lyric_transition_t;

void paper_prepare(const lrc_t *lyrics, int line, uint32_t duration, unsigned style);
bool paper_highlight(const lrc_t *lyrics, int line, uint32_t position);
void paper_invalidate(void);
int paper_draw(aster_ge_t *gfx, int left, int top, int extent, unsigned style,
               unsigned previous_style, uint32_t elapsed, const char *lyrics, const char *status,
               lyric_transition_t transition);

#endif
