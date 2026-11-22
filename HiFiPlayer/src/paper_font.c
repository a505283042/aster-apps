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
/* Source-aware lookup: a cached index from an old packaged asset must not be
 * used with a different shared font revision or fallback source. */
static uint8_t cached_source[2];
static bool missing_logged[2];

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

/* The shared ALV1 fonts are installed once on the SD card through USB MSC.
 * Aster FS resolves /sd to the SD root only for apps with the 'sd' permission.
 * Old fat APP bundles still work as a safe fallback during migration. */
bool paper_font_open(paper_font_t *font, bool bold)
{
    static const char *const shared[2] = {
        "/sd/aster/shared/hifi/fonts/paper-serif.bin",
        "/sd/aster/shared/hifi/fonts/paper-bold.bin"
    };
    static const char *const embedded[2] = {
        "/assets/text/paper-serif.bin",
        "/assets/text/paper-bold.bin"
    };
    const unsigned variant = bold ? 1u : 0u;
    font->handle = 0;
    font->count = 0;
    font->bold = bold;
    for (unsigned source = 1u; source <= 2u; source++) {
        const char *path = source == 1u ? shared[variant] : embedded[variant];
        unsigned n = (unsigned)strlen(path);
        aster_stat_t st = {.size = sizeof(st), .version = 1};
        if (aster_fs_stat(path, n, &st, sizeof(st)) < 0 ||
            (st.mode & ASTER_STAT_DIR) || st.filesize < HEADER + ENTRY)
            continue;
        aster_handle_t handle = 0;
        if (aster_fs_open(path, n, ASTER_FS_O_RDONLY, &handle) < 0)
            continue;
        uint8_t header[HEADER];
        bool valid = read_all(handle, header, sizeof(header)) &&
                     !memcmp(header, "ALV1", 4) &&
                     le16(header + 4) == PAPER_FONT_CELL;
        uint16_t count = valid ? le16(header + 6) : 0;
        uint32_t index_bytes_n = (uint32_t)count * ENTRY;
        valid = count > 0 && index_bytes_n <= sizeof(index_bytes[variant]) &&
                st.filesize >= (uint64_t)HEADER + index_bytes_n;
        if (valid && (cached_source[variant] != source ||
                      cached_count[variant] != count)) {
            /* A new source is validated before its glyph offsets can be used.
             * Packet contents are read on demand, never loaded as a 2.5MB blob. */
            cached_source[variant] = 0;
            cached_count[variant] = 0;
            valid = read_all(handle, index_bytes[variant], index_bytes_n);
            const uint64_t packet_bytes =
                st.filesize - (uint64_t)HEADER - index_bytes_n;
            uint32_t previous_cp = 0;
            uint32_t previous_offset = 0;
            for (uint32_t i = 0; valid && i < count; ++i) {
                const uint8_t *entry = index_bytes[variant] + i * ENTRY;
                uint32_t cp = le16(entry);
                uint32_t length = le16(entry + 2);
                uint32_t offset = le32(entry + 4);
                /* Blank glyphs (e.g. SPACE) correctly have zero packet bytes. */
                if (!cp || (i && cp <= previous_cp) ||
                    offset < previous_offset ||
                    (uint64_t)offset + length > packet_bytes)
                    valid = false;
                previous_cp = cp;
                previous_offset = offset;
            }
            if (valid) {
                cached_source[variant] = (uint8_t)source;
                cached_count[variant] = count;
            }
        } else if (valid) {
            /* File header matches the index already loaded for this source.
             * The shared file cannot be edited while the device holds the card. */
            valid = true;
        }
        if (valid) {
            font->handle = handle;
            font->count = count;
            missing_logged[variant] = false;
            return true;
        }
        aster_fs_close(handle);
    }
    if (!missing_logged[variant]) {
        aster_printf("HiFi paper font unavailable: %s; copy from APP source text/ to SD /aster/shared/hifi/fonts/",
                     bold ? "paper-bold.bin" : "paper-serif.bin");
        missing_logged[variant] = true;
    }
    return false;
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

/* Same pixel budget as the original 36 full-size masks. Only unusually diverse
 * lines need smaller cells. Compute ink metrics before downsampling. */
static uint8_t atlas[36 * PAPER_FONT_CELL * PAPER_FONT_CELL];
static struct {
    uint32_t cp;
    uint8_t left, right, advance;
    int8_t center_offset;
} shapes[LYRIC_LINE_CAP];
static int cell;

int paper_font_cell(void)
{
    return cell;
}

paper_mask_t paper_font_mask(unsigned slot)
{
    return (paper_mask_t){atlas + slot * cell * cell, shapes[slot].left, shapes[slot].right};
}

static bool load_shape(paper_font_t *font, unsigned slot)
{
    static uint8_t full[PAPER_FONT_CELL * PAPER_FONT_CELL];
    uint8_t *dst = atlas + slot * cell * cell;
    uint8_t *src = cell == PAPER_FONT_CELL ? dst : full;
    uint32_t cp = shapes[slot].cp;
    if (!paper_font_read(font, cp, src))
        return false;
    int left = PAPER_FONT_CELL, right = 0;
    for (int y = 0; y < PAPER_FONT_CELL; y++) {
        for (int x = 0; x < PAPER_FONT_CELL; x++) {
            if (src[y * PAPER_FONT_CELL + x] >= 24) {
                if (x < left)
                    left = x;
                if (x + 1 > right)
                    right = x + 1;
            }
        }
    }
    if (right <= left)
        left = right = PAPER_FONT_CELL / 2;
    shapes[slot].left = left;
    shapes[slot].right = right;
    shapes[slot].advance = cp == ' ' ? 22 : cp >= 0x3000 ? PAPER_FONT_EM : right - left + 5;
    shapes[slot].center_offset = cp >= 0x3000 ? 0 : (PAPER_FONT_CELL - left - right) / 2;
    if (src != dst) {
        int factor = PAPER_FONT_CELL / cell;
        for (int y = 0; y < cell; y++) {
            for (int x = 0; x < cell; x++) {
                unsigned sum = 0;
                for (int dy = 0; dy < factor; dy++)
                    for (int dx = 0; dx < factor; dx++)
                        sum += src[(y * factor + dy) * PAPER_FONT_CELL + x * factor + dx];
                dst[y * cell + x] = sum / (factor * factor);
            }
        }
    }
    return true;
}

bool paper_font_line(lyric_glyph_t *glyphs, unsigned count, bool bold)
{
    unsigned unique = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned slot = 0;
        while (slot < unique && shapes[slot].cp != glyphs[i].codepoint)
            slot++;
        if (slot == unique)
            shapes[unique++].cp = glyphs[i].codepoint;
        glyphs[i].mask_slot = slot;
    }
    cell = PAPER_FONT_CELL;
    while (unique * cell * cell > sizeof(atlas))
        cell /= 2;
    paper_font_t font;
    if (!paper_font_open(&font, bold))
        return false;
    unsigned loaded = 0;
    while (loaded < unique && load_shape(&font, loaded))
        loaded++;
    paper_font_close(&font);
    if (loaded != unique)
        return false;
    for (unsigned i = 0; i < count; i++) {
        unsigned slot = glyphs[i].mask_slot;
        glyphs[i].advance = shapes[slot].advance;
        glyphs[i].center_offset = shapes[slot].center_offset;
    }
    return true;
}
