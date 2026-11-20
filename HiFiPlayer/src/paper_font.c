/* SPDX-License-Identifier: MIT */
#include <aster_imports.h>
#include "paper_font.h"

enum { HEADER = 8, ENTRY = 8, SUBROWS = 3, ROWS = PAPER_FONT_CELL * SUBROWS, CROSSINGS = 96 };
static uint16_t crossing[ROWS][CROSSINGS];
static uint8_t row_count[ROWS];

/* Keep both indexes resident after their first use. A change of layout then
 * needs only the packets for the new lyric. */
static uint8_t index_bytes[2][8192 * ENTRY];
static uint16_t cached_count[2];

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }

static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p) | (uint32_t)le16(p + 2) << 16; }

static bool read_all(aster_handle_t handle, uint8_t *out, size_t bytes)
{
    while (bytes) {
        int32_t n = aster_fs_read(handle, out, bytes);
        if (n <= 0 || (size_t)n > bytes)
            return false;
        out += n;
        bytes -= n;
    }
    return true;
}

bool paper_font_open(paper_font_t *font, bool bold)
{
    static const char regular[] = "/assets/text/paper-serif.bin";
    static const char strong[] = "/assets/text/paper-bold.bin";
    const char *path = bold ? strong : regular;
    if (aster_fs_open(path, strlen(path), ASTER_FS_O_RDONLY, &font->handle) < 0)
        return false;
    if (!cached_count[bold]) {
        uint8_t header[HEADER];
        if (!read_all(font->handle, header, sizeof(header)) || memcmp(header, "ALV1", 4) ||
            le16(header + 4) != PAPER_FONT_CELL) {
            paper_font_close(font);
            return false;
        }
        uint16_t count = le16(header + 6);
        size_t bytes = (size_t)count * ENTRY;
        if (!count || bytes > sizeof(index_bytes[bold]) ||
            !read_all(font->handle, index_bytes[bold], bytes)) {
            paper_font_close(font);
            return false;
        }
        cached_count[bold] = count;
    }
    font->count = cached_count[bold];
    font->bold = bold;
    return true;
}

void paper_font_close(paper_font_t *font)
{
    aster_fs_close(font->handle);
    font->handle = 0;
}

static const uint8_t *find_glyph(const paper_font_t *font, uint32_t codepoint)
{
    unsigned lo = 0, hi = font->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        const uint8_t *entry = index_bytes[font->bold] + mid * ENTRY;
        unsigned cp = le16(entry);
        if (cp == codepoint)
            return entry;
        if (cp < codepoint)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

/* Use a nonzero winding fill so overlapping components and counters are
 * preserved. Each subrow contributes one third of a pixel's ink coverage. */
static bool add_edge(const uint8_t *a, const uint8_t *b)
{
    if (a[1] == b[1])
        return true;
    bool descending = a[1] > b[1];
    if (descending) {
        const uint8_t *swap = a;
        a = b;
        b = swap;
    }
    int start = (a[1] * ROWS + 127) / 255;
    int end = (b[1] * ROWS + 127) / 255;
    int dy = b[1] - a[1], dx = b[0] - a[0];
    for (int row = start; row < end; row++) {
        int local_y = (2 * row + 1) * 255 - 2 * ROWS * a[1];
        int x = (a[0] * 256 + (int)((int64_t)local_y * dx * 128 / (ROWS * dy))) * PAPER_FONT_CELL /
                (255 * 8);
        if (row_count[row] == CROSSINGS)
            return false;
        crossing[row][row_count[row]++] = (uint16_t)(2 * x + descending);
    }
    return true;
}

static void rasterize(uint8_t *mask)
{
    memset(mask, 0, PAPER_FONT_CELL * PAPER_FONT_CELL);
    for (int row = 0; row < ROWS; row++) {
        uint16_t *list = crossing[row];
        unsigned n = row_count[row];
        for (unsigned i = 1; i < n; i++) {
            uint16_t item = list[i];
            unsigned j = i;
            while (j && list[j - 1] > item) {
                list[j] = list[j - 1];
                j--;
            }
            list[j] = item;
        }
        int winding = 0, begin = 0;
        uint8_t *line = mask + row / SUBROWS * PAPER_FONT_CELL;
        for (unsigned i = 0; i < n; i++) {
            int end = list[i] >> 1;
            if (winding && begin < end) {
                for (int x = begin / 32; x < (end + 31) / 32 && x < PAPER_FONT_CELL; x++) {
                    int lo = x * 32 > begin ? x * 32 : begin;
                    int hi = x * 32 + 32 < end ? x * 32 + 32 : end;
                    unsigned value = line[x] + (hi - lo) * 85 / 32;
                    line[x] = value > 255 ? 255 : value;
                }
            }
            winding += list[i] & 1 ? -1 : 1;
            begin = end;
        }
    }
}

bool paper_font_read(const paper_font_t *font, uint32_t codepoint,
                     uint8_t mask[PAPER_FONT_CELL * PAPER_FONT_CELL])
{
    const uint8_t *entry = find_glyph(font, codepoint);
    if (!entry)
        return false;
    uint8_t packets[4096];
    unsigned length = le16(entry + 2);
    uint32_t data_start = HEADER + font->count * ENTRY;
    uint32_t offset = le32(entry + 4);
    if (length > sizeof(packets) || offset > INT32_MAX - data_start ||
        aster_fs_seek(font->handle, data_start + offset, ASTER_FS_SEEK_SET) < 0 ||
        !read_all(font->handle, packets, length))
        return false;

    memset(row_count, 0, sizeof(row_count));
    unsigned at = 0;
    while (at < length) {
        if (length - at < 2)
            return false;
        unsigned vertices = le16(packets + at);
        at += 2;
        if (vertices < 3 || vertices > (length - at) / 2)
            return false;
        const uint8_t *contour = packets + at;
        for (unsigned i = 0, j = vertices - 1; i < vertices; j = i++)
            if (!add_edge(contour + j * 2, contour + i * 2))
                return false;
        at += vertices * 2;
    }
    rasterize(mask);
    return true;
}
