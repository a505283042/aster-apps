/* SPDX-License-Identifier: MIT */
#include <aster_imports.h>
#include "paper.h"

#define RGB(r,g,b) ((uint16_t)(((r) & 0xf8) << 8 | ((g) & 0xfc) << 3 | (b) >> 3))

struct palette { uint16_t base, sheet, fold, edge, ink, pending, shadow; };
static const struct palette palettes[] = {
    {RGB(241,225,202), RGB(230,193,179), RGB(172,65,79), RGB(189,114,115),
     RGB(105,30,51), RGB(137,79,82), RGB(199,146,140)},
    {RGB(36,32,63), RGB(81,65,130), RGB(56,45,97), RGB(113,96,166),
     RGB(255,229,178), RGB(173,159,205), RGB(51,39,81)},
    {RGB(239,229,208), RGB(165,184,211), RGB(92,121,159), RGB(103,131,172),
     RGB(33,54,93), RGB(80,101,137), RGB(137,156,188)}
};

struct scene {
    aster_ge_t *gfx;
    int left, top, size;
    const struct palette *color;
};

static int scale(const struct scene *s, int v) { return v * s->size / 460; }

static void fold(const struct scene *s, int x, int y, int x2, int y2, int x3, int y3, uint16_t c)
{
    aster_ge_tri(s->gfx, s->left+scale(s,x), s->top+scale(s,y),
                 s->left+scale(s,x2), s->top+scale(s,y2),
                 s->left+scale(s,x3), s->top+scale(s,y3), c);
}

static void paper_background(const struct scene *s, unsigned style, unsigned previous, uint32_t elapsed)
{
    const struct palette *p = s->color;
    aster_ge_clear(s->gfx, p->base);
    if (style == 1) {
        fold(s, 84,0, 405,0, 345,459, p->sheet);
        fold(s, 84,0, 345,459, 9,459, p->sheet);
        fold(s, 0,365, 218,365, 0,459, p->fold);
        fold(s, 425,80, 459,64, 459,459, p->fold);
    } else {
        fold(s, 0,275, 459,50, 459,264, p->sheet);
        fold(s, 0,275, 459,264, 0,449, p->sheet);
        fold(s, 0,387, 459,129, 459,155, p->edge);
        fold(s, 0,387, 459,155, 0,420, p->edge);
        fold(s, 360,0, 459,0, 459,155, p->fold);
    }
    if (elapsed >= PAPER_TURN_MS || previous == style) return;
    for (unsigned i = 0; i < 5; i++) {
        int local = (int)elapsed - (int)i * 24;
        int reveal = local <= 0 ? 0 : local >= 240 ? s->size : (int64_t)s->size * local / 240;
        if (reveal >= s->size) continue;
        int remain = s->size - reveal;
        int y = s->top + s->size*i/5, row_h = s->top + s->size*(i+1)/5 - y;
        int x = s->left + (i & 1 ? 0 : reveal);
        aster_ge_rect_r(s->gfx, x, y, remain, row_h, palettes[previous].base, 0, false);
        int edge = i & 1 ? x+remain-1 : x;
        aster_ge_line_aa(s->gfx, edge, y, edge, y+row_h-1, p->edge);
    }
}

static int advance(int px, const char *word, unsigned bytes)
{
    int value = aster_gfx_text_w(px, word, bytes);
    return value >= 0 ? value : px;
}

static void laid_out_text(const struct scene *s, int px, int x, int baseline,
                          const char *body, unsigned length, unsigned lit)
{
    char text[768];
    memcpy(text, body, length);
    text[length] = 0;
    aster_ge_text(s->gfx, x+scale(s,3), baseline+scale(s,4), px,
                  s->color->shadow, ASTER_GFX_NOBG, text);
    if (lit >= length) {
        aster_ge_text(s->gfx, x, baseline, px, s->color->ink, ASTER_GFX_NOBG, text);
        return;
    }
    if (lit) {
        char boundary = text[lit];
        text[lit] = 0;
        aster_ge_text(s->gfx, x, baseline, px, s->color->ink, ASTER_GFX_NOBG, text);
        text[lit] = boundary;
        x += advance(px, text, lit);
    }
    aster_ge_text(s->gfx, x, baseline, px, s->color->pending, ASTER_GFX_NOBG, text+lit);
}

static unsigned large_chars(const char *body, unsigned *offset, unsigned cap)
{
    unsigned n = 0, at = 0;
    while (body[at] && n < cap) {
        uint32_t width = aster_utf8_len(body+at);
        if (width != 3 || (unsigned char)body[at+1] < 128 || (unsigned char)body[at+2] < 128)
            return 0;
        const uint32_t cp = aster_utf8_cp(body+at, width);
        if (cp < 0x3000 || cp > 0x9fff) return 0;
        offset[n++] = at;
        at += width;
    }
    return body[at] ? 0 : n;
}

static void large_line(const struct scene *s, const char *body, const unsigned *offset,
                       unsigned count, unsigned style, unsigned lit, int lift)
{
    unsigned first = (count+1)/2;
    int px = aster_text_snap(scale(s,48));
    int asc = aster_text_asc(px), line_h = aster_text_line_h(px);
    for (unsigned i = 0; i < count; i++) {
        bool head = i < first;
        unsigned column = head ? i : i-first, n = head ? first : count-first;
        int x, y;
        if (style == 1) {
            x = head ? 318 : 146;
            y = 56 + 348*(2*column+1)/(2*n);
        } else {
            x = 20 + 420*(2*column+1)/(2*n);
            y = (head ? 154 : 301) + (style == 2 ? -(int)column*8 : (int)(column & 1)*6);
        }
        int baseline = s->top+scale(s,y+lift) + asc-line_h/2;
        int width = advance(px, body+offset[i], 3);
        laid_out_text(s, px, s->left+scale(s,x)-width/2, baseline, body+offset[i], 3,
                      offset[i]+3 <= lit ? 3 : 0);
    }
}

static void wrapped_line(const struct scene *s, const char *text, unsigned lit, int lift)
{
    static const uint8_t faces[] = {48,40,32,28,24,20,18,16,14,12};
    struct { unsigned at, length; } rows[24];
    unsigned length = strlen(text), count = 0;
    int px = 12, step = 0, cap = scale(s,420);
    for (unsigned f = 0; f < sizeof(faces); f++) {
        px = aster_text_snap(scale(s, faces[f]));
        step = aster_text_line_h(px)+scale(s,5);
        unsigned at = 0;
        count = 0;
        while (at < length && count < sizeof(rows)/sizeof(rows[0]) &&
               (int)(count+1)*step <= scale(s,409)) {
            uint32_t bytes;
            unsigned skip = aster_text_line(px, text+at, length-at, cap, &bytes);
            if (!skip) break;
            rows[count].at = at;
            rows[count++].length = bytes;
            at += skip;
        }
        if (at == length) break;
    }
    int y = s->top+(s->size-(int)count*step)/2 + aster_text_asc(px)+scale(s,lift);
    for (unsigned i = 0; i < count; i++, y += step) {
        unsigned at = rows[i].at, n = rows[i].length;
        unsigned sung = lit <= at ? 0 : lit-at < n ? lit-at : n;
        int x = s->left+(s->size-advance(px,text+at,n))/2;
        laid_out_text(s, px, x, y, text+at, n, sung);
    }
}

void paper_draw(aster_ge_t *gfx, int left, int top, int extent, unsigned style,
                unsigned previous, uint32_t elapsed, const char *lyric,
                unsigned highlighted, const char *status)
{
    struct scene s = {gfx, left, top, extent, &palettes[style]};
    paper_background(&s, style, previous, elapsed);
    int lift = elapsed >= 200 ? 0 : (200-(int)elapsed)*(style == 1 ? -12 : 15)/200;
    if (!lyric || !*lyric) {
        if (status && *status) wrapped_line(&s, status, strlen(status), 0);
        return;
    }
    unsigned offset[10];
    unsigned count = large_chars(lyric, offset, sizeof(offset)/sizeof(offset[0]));
    if (count >= 4) large_line(&s, lyric, offset, count, style, highlighted, lift);
    else wrapped_line(&s, lyric, highlighted, lift);
}
