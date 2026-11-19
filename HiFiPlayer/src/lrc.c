/* SPDX-License-Identifier: MIT */
#include "lrc.h"
#include <stdbool.h>
#ifdef __wasm__
#include <aster_libc.h>
#else
#include <string.h>
#endif

static bool digit(char c) { return c >= '0' && c <= '9'; }

static const char *timestamp(const char *p, char close, uint32_t *ms)
{
    const char *start = p;
    unsigned minutes = 0, fraction = 0, scale = 100;
    while (digit(*p) && p - start < 5) minutes = minutes * 10 + (*p++ - '0');
    if (start == p || *p++ != ':' || !digit(p[0]) || !digit(p[1])) return NULL;
    unsigned seconds = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (seconds >= 60 || minutes > 71581) return NULL;
    if (*p == '.' || *p == ':') {
        p++;
        if (!digit(*p)) return NULL;
        for (; digit(*p); p++, scale /= 10) fraction += (*p - '0') * scale;
    }
    if (*p != close) return NULL;
    *ms = (minutes * 60 + seconds) * 1000 + fraction;
    return p + 1;
}

static int32_t read_offset(const char *start, const char *end)
{
    bool negative = *start == '-';
    if (*start == '-' || *start == '+') start++;
    int32_t value = 0;
    while (start < end && digit(*start)) {
        if (value >= 360000) return 0;
        value = value * 10 + (*start++ - '0');
    }
    return start == end ? (negative ? -value : value) : 0;
}

static void parse_line(lrc_t *out, char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '[') return;
    uint32_t time;
    const char *end = timestamp(line + 1, ']', &time);
    if (!end) {
        const char *close = strchr(line, ']');
        if (!close) return;
        if (!strncmp(line, "[offset:", 8)) {
            out->offset_ms = read_offset(line + 8, close);
        } else if (!strncmp(line, "[ti:", 4) || !strncmp(line, "[ar:", 4)) {
            size_t length = (size_t)(close - (line + 4));
            char *field = line[1] == 't' ? out->title : out->artist;
            size_t cap = line[1] == 't' ? sizeof(out->title) : sizeof(out->artist);
            if (length < cap) { memcpy(field, line + 4, length); field[length] = 0; }
        }
        return;
    }

    unsigned first = out->count;
    const char *body = line;
    do {
        if (out->count < LRC_CUE_LIMIT)
            out->cue[out->count++] = (lrc_cue_t){.time_ms = time, .text_offset = out->used};
        body = end;
        end = *body == '[' ? timestamp(body + 1, ']', &time) : NULL;
    } while (end);
    if (first == out->count) return;
    while (*body == ' ' || *body == '\t') body++;
    char *clean = line;
    unsigned mark = out->words_used;
    uint32_t line_start = out->cue[first].time_ms, previous_ms = 0;
    bool valid = true;
    while (*body) {
        uint32_t absolute;
        const char *tag = *body == '<' ? timestamp(body + 1, '>', &absolute) : NULL;
        if (!tag) { *clean++ = *body++; continue; }
        body = tag;
        uint32_t delta = absolute > line_start ? absolute-line_start : 0;
        if (delta < previous_ms || out->words_used == LRC_WORD_LIMIT) valid = false;
        if (valid) out->words[out->words_used++] = (lrc_word_t){delta, (uint16_t)(clean-line)};
        previous_ms = delta;
    }
    *clean = 0;
    size_t length = (size_t)(clean-line) + 1;
    if (length > sizeof(out->text) - out->used) {
        out->count = first;
        out->words_used = mark;
        return;
    }
    if (!valid) out->words_used = mark;
    for (unsigned i = first; i < out->count; i++) {
        out->cue[i].first_word = mark;
        out->cue[i].word_count = out->words_used-mark;
    }
    memcpy(out->text + out->used, line, length);
    out->used += length;
}

enum lrc_encoding lrc_detect(const unsigned char *bytes, size_t size)
{
    if (size >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) return LRC_UTF16_LE;
    if (size >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) return LRC_UTF16_BE;
    if (size >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) return LRC_UTF8;
    for (size_t at = 0; at + 1 < size; at += 2) {
        unsigned a = bytes[at], b = bytes[at+1];
        if ((a == '[' && b == 0) || (a == 0 && b == '[')) return a ? LRC_UTF16_LE : LRC_UTF16_BE;
        unsigned c = a | b;
        if ((a && b) || (c != ' ' && c != '\t' && c != '\r' && c != '\n')) break;
    }
    for (size_t at = 0; at < size;) {
        unsigned c = bytes[at++];
        if (c < 0x80) continue;
        unsigned tail = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 :
                        c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!tail || tail > size - at) return LRC_GBK;
        unsigned next = bytes[at];
        if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) ||
            (c == 0xf0 && next < 0x90) || (c == 0xf4 && next >= 0x90)) return LRC_GBK;
        while (tail--) if ((bytes[at++] & 0xc0) != 0x80) return LRC_GBK;
    }
    return LRC_UTF8;
}

static uint32_t read_code(const unsigned char *bytes, size_t size, size_t *at,
                          enum lrc_encoding encoding, const unsigned char *gbk)
{
    uint32_t a = bytes[(*at)++];
    if (encoding == LRC_UTF8) return a;
    if (encoding == LRC_GBK) {
        if (a < 0x80) return a;
        if (a == 0x80) return 0x20ac;
        if (*at == size || a == 0xff || !gbk) return 0xfffd;
        unsigned trail = bytes[*at];
        if (trail < 0x40 || trail == 0x7f || trail == 0xff) return 0xfffd;
        (*at)++;
        unsigned index = 2 * ((a-0x81) * 190 + trail - (trail < 0x7f ? 0x40 : 0x41));
        uint32_t cp = gbk[index] | (uint32_t)gbk[index+1] << 8;
        return cp ? cp : 0xfffd;
    }
    if (*at == size) return 0xfffd;
    uint32_t b = bytes[(*at)++];
    return encoding == LRC_UTF16_LE ? a | b << 8 : a << 8 | b;
}

static unsigned utf8(char out[4], uint32_t code)
{
    if (code <= 0x7f) { out[0] = code; return 1; }
    if (code <= 0x7ff) {
        out[0] = 0xc0 | code >> 6; out[1] = 0x80 | (code & 63); return 2;
    }
    if (code <= 0xffff) {
        out[0] = 0xe0 | code >> 12; out[1] = 0x80 | (code >> 6 & 63);
        out[2] = 0x80 | (code & 63); return 3;
    }
    out[0] = 0xf0 | code >> 18; out[1] = 0x80 | (code >> 12 & 63);
    out[2] = 0x80 | (code >> 6 & 63); out[3] = 0x80 | (code & 63); return 4;
}

void lrc_parse(lrc_t *out, const unsigned char *bytes, size_t size, const unsigned char *gbk)
{
    out->count = out->used = out->words_used = 0;
    out->title[0] = out->artist[0] = 0;
    out->offset_ms = 0;
    size_t at = 0, length = 0;
    enum lrc_encoding encoding = lrc_detect(bytes, size);
    if ((encoding == LRC_UTF16_LE && bytes[0] == 0xff && bytes[1] == 0xfe) ||
        (encoding == LRC_UTF16_BE && bytes[0] == 0xfe && bytes[1] == 0xff)) at = 2;
    else if (encoding == LRC_UTF8 && size >= 3 && bytes[0] == 0xef &&
             bytes[1] == 0xbb && bytes[2] == 0xbf) at = 3;

    char line[768];
    bool oversized = false;
    while (at < size) {
        uint32_t code = read_code(bytes, size, &at, encoding, gbk);
        if (code == '\r' || code == '\n') {
            if (length && !oversized) { line[length] = 0; parse_line(out, line); }
            length = 0;
            oversized = false;
            continue;
        }
        if (code == 0 || oversized) continue;
        char encoded[4];
        unsigned n = 1;
        if (encoding == LRC_UTF8) encoded[0] = code;
        else {
            if (code >= 0xd800 && code <= 0xdbff && size - at >= 2) {
                size_t next_at = at;
                uint32_t next = read_code(bytes, size, &next_at, encoding, gbk);
                if (next >= 0xdc00 && next <= 0xdfff) {
                    code = 0x10000 + ((code - 0xd800) << 10) + next - 0xdc00;
                    at = next_at;
                }
            }
            if (code >= 0xd800 && code <= 0xdfff) code = 0xfffd;
            n = utf8(encoded, code);
        }
        if (length + n >= sizeof(line)) { oversized = true; continue; }
        memcpy(line + length, encoded, n);
        length += n;
    }
    if (length && !oversized) { line[length] = 0; parse_line(out, line); }

    /* Files are usually already sorted. Stable insertion also handles repeated
     * tags and leaves a translation following its original line. */
    for (unsigned i = 1; i < out->count; i++) {
        lrc_cue_t cue = out->cue[i];
        unsigned j = i;
        while (j && out->cue[j - 1].time_ms > cue.time_ms) {
            out->cue[j] = out->cue[j - 1];
            j--;
        }
        out->cue[j] = cue;
    }
}

int lrc_active(const lrc_t *lyrics, uint32_t position_ms)
{
    int64_t clock = (int64_t)position_ms + lyrics->offset_ms;
    unsigned first = 0, end = lyrics->count;
    while (first < end) {
        unsigned middle = first + (end - first) / 2;
        if (lyrics->cue[middle].time_ms <= clock) first = middle + 1;
        else end = middle;
    }
    if (!first) return -1;
    unsigned current = first - 1;
    while (current && lyrics->cue[current - 1].time_ms == lyrics->cue[current].time_ms) current--;
    return (int)current;
}

unsigned lrc_sung_bytes(const lrc_t *lyrics, unsigned index, uint32_t position_ms)
{
    const lrc_cue_t *cue = &lyrics->cue[index];
    unsigned length = strlen(lyrics->text + cue->text_offset);
    if (!cue->word_count) return length;
    int64_t elapsed = (int64_t)position_ms + lyrics->offset_ms - cue->time_ms;
    const lrc_word_t *words = lyrics->words + cue->first_word;
    for (unsigned i = 0; i < cue->word_count; i++) {
        if (elapsed < words[i].delta_ms) return words[i].byte_offset;
    }
    return length;
}
