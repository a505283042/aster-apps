/* SPDX-License-Identifier: MIT */
#ifndef HIFI_PAPER_H
#define HIFI_PAPER_H

#include <aster_gfx_engine.h>

#define PAPER_TURN_MS 360u

void paper_draw(aster_ge_t *gfx, int left, int top, int extent, unsigned style,
                unsigned previous_style, uint32_t elapsed, const char *lyrics,
                unsigned highlighted, const char *status);

#endif
