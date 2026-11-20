/* SPDX-License-Identifier: MIT */
#ifndef HIFI_PAPER_FONT_H
#define HIFI_PAPER_FONT_H

#include <aster/fs.h>
#include <stdbool.h>

enum { PAPER_FONT_CELL = 80, PAPER_FONT_EM = 76 };

typedef struct {
    aster_handle_t handle;
    uint16_t count;
    bool bold;
} paper_font_t;

bool paper_font_open(paper_font_t *font, bool bold);
void paper_font_close(paper_font_t *font);
bool paper_font_read(const paper_font_t *font, uint32_t codepoint,
                     uint8_t mask[PAPER_FONT_CELL * PAPER_FONT_CELL]);

#endif
