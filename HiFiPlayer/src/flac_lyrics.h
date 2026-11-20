/* SPDX-License-Identifier: MIT */
#ifndef HIFI_FLAC_LYRICS_H
#define HIFI_FLAC_LYRICS_H

#include <aster/fs.h>
#include <stdbool.h>

typedef struct {
    uint32_t offset, length;
} lyric_span_t;

typedef struct {
    aster_handle_t file;
    uint32_t end, block, comment, comments_end, remaining;
    uint32_t cache_at, cached;
    uint8_t cache[256];
    bool last;
} flac_lyrics_t;

enum flac_result { FLAC_INVALID, FLAC_DONE, FLAC_MORE, FLAC_FOUND };
bool flac_lyrics_begin(flac_lyrics_t *scan, aster_handle_t file, uint32_t size);
enum flac_result flac_lyrics_next(flac_lyrics_t *scan, lyric_span_t *value);

#endif
