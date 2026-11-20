/* SPDX-License-Identifier: MIT */
#ifndef HIFI_LRC_H
#define HIFI_LRC_H

#include <stddef.h>
#include <stdint.h>

enum { LRC_FILE_LIMIT = 48 * 1024, LRC_CUE_LIMIT = 512, LRC_TEXT_LIMIT = 24 * 1024,
       LRC_WORD_LIMIT = 2048 };
enum { LRC_GBK_BYTES = 126 * 190 * 2 };
enum lrc_encoding { LRC_UTF8, LRC_UTF16_LE, LRC_UTF16_BE, LRC_GBK };

typedef struct {
    uint32_t time_ms;
    uint16_t text_offset;
    uint16_t first_word, word_count;
} lrc_cue_t;

typedef struct {
    uint32_t delta_ms;
    uint16_t byte_offset;
} lrc_word_t;

typedef struct {
    lrc_cue_t cue[LRC_CUE_LIMIT];
    lrc_word_t words[LRC_WORD_LIMIT];
    char text[LRC_TEXT_LIMIT];
    char title[192], artist[128];
    unsigned count, used, words_used;
    int32_t offset_ms;
} lrc_t;

enum lrc_encoding lrc_detect(const unsigned char *bytes, size_t size);
void lrc_parse(lrc_t *out, const unsigned char *bytes, size_t size, const unsigned char *gbk);
void lrc_parse_plain_utf8(lrc_t *out, const unsigned char *bytes, size_t size);
int lrc_active(const lrc_t *lyrics, uint32_t position_ms);
unsigned lrc_sung_bytes(const lrc_t *lyrics, unsigned cue, uint32_t position_ms);

#endif
