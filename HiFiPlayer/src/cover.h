/* SPDX-License-Identifier: MIT */
#ifndef HIFI_COVER_H
#define HIFI_COVER_H

#include <stdbool.h>
#include <aster_imports.h>

void cover_select(const char *track);
void cover_select_remote(const char *path, uint32_t revision);
bool cover_tick(unsigned cap);
void cover_close(void);
bool cover_ready(void);
uint16_t cover_mix(uint16_t base, uint16_t tint, unsigned weight);
bool cover_paint(aster_handle_t surface, unsigned left, unsigned top, unsigned edge,
                 uint16_t base, bool dimmed);

#endif
