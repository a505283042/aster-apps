/* SPDX-License-Identifier: MIT */
#ifndef HIFI_COVER_JPEG_H
#define HIFI_COVER_JPEG_H

#include <aster/fs.h>
#include <stdbool.h>

enum { COVER_EDGE = 460, COVER_WORK_BYTES = 16384 };
/* Caller owns the open file and the aligned workspace until the last step or
 * a track change. Only the selected picture's byte range can be read. */
bool cover_jpeg_begin(aster_handle_t file, uint32_t offset, uint32_t length, unsigned cap,
                      uint16_t *pixels, uint8_t *workspace);
/* Active local audio: reduce each SD read to 2 KB and cooperative MCU
 * work target to 2 ms; no effect on paused, idle, or remote playback. */
void cover_jpeg_set_audio_priority(bool active);
int cover_jpeg_step(uint32_t *width, uint32_t *height);

#endif
