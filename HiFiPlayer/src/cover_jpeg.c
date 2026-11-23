/* SPDX-License-Identifier: MIT */
#include "cover_jpeg.h"
#include <aster_imports.h>
#include "../vendor/tjpgd/tjpgd.h"

/* FakePod_S31 reference: 2/4/8 KB cooperative artwork SD slices.
 * Aster does not yet have a readable FLAC prefetch-ring watermark, so only
 * adopt the bounded small reads here; do not copy the 90% threshold. */
enum { READ_AHEAD = 4096, PLAYING_READ_CHUNK = 2048,
       IDLE_SLICE_MS = 10, PLAYING_SLICE_MS = 2 };
static bool audio_priority;
void cover_jpeg_set_audio_priority(bool active)
{
    audio_priority = active;
}
static JDEC decoder;
static struct {
    aster_handle_t file;
    uint8_t *cache;
    uint32_t next, end;
    unsigned pos, count;
} stream;
static uint16_t *output;
static uint16_t sample_x[COVER_EDGE], sample_y[COVER_EDGE];
static unsigned out_w, out_h, mcu_x, mcu_y;

static size_t read_stream(JDEC *jd, uint8_t *dst, size_t n)
{
    (void)jd;
    unsigned available = stream.count - stream.pos;
    if (!dst) {
        if (n <= available) {
            stream.pos += n;
        } else {
            size_t skip = n - available;
            if (skip > stream.end - stream.next ||
                aster_fs_seek(stream.file, stream.next + skip, ASTER_FS_SEEK_SET) < 0)
                return 0;
            stream.next += skip;
            stream.pos = stream.count = 0;
        }
        return n;
    }
    size_t done = 0;
    while (done < n) {
        if (stream.pos == stream.count) {
            unsigned left = stream.end - stream.next;
            if (!left)
                break;
            /* Never hold a large SD read in one APP frame while audio plays.
             * This is a best-effort cap per fs_read, not a ring-aware gate. */
            unsigned limit = audio_priority ? PLAYING_READ_CHUNK : READ_AHEAD;
            unsigned want = left < limit ? left : limit;
            int got = aster_fs_read(stream.file, stream.cache, want);
            if (got <= 0 || (unsigned)got > want)
                break;
            stream.next += got;
            stream.pos = 0;
            stream.count = got;
        }
        unsigned take = stream.count - stream.pos;
        if (take > n - done)
            take = n - done;
        memcpy(dst + done, stream.cache + stream.pos, take);
        stream.pos += take;
        done += take;
    }
    return done;
}

static void sample_axis(uint16_t *samples, unsigned n, unsigned src)
{
    for (unsigned i = 0; i < n; i++)
        samples[i] = (2 * i + 1) * src / (2 * n);
}

bool cover_jpeg_begin(aster_handle_t file, uint32_t offset, uint32_t length, unsigned cap,
                      uint16_t *pixels, uint8_t *workspace)
{
    if (!cap || cap > COVER_EDGE || offset > INT32_MAX || length > INT32_MAX - offset ||
        aster_fs_seek(file, offset, ASTER_FS_SEEK_SET) < 0)
        return false;
    stream.file = file;
    stream.cache = workspace + COVER_WORK_BYTES - READ_AHEAD;
    stream.next = offset;
    stream.end = offset + length;
    stream.pos = stream.count = 0;
    if (jd_prepare(&decoder, read_stream, workspace, COVER_WORK_BYTES - READ_AHEAD, NULL) != JDR_OK ||
        !decoder.width || !decoder.height)
        return false;

    unsigned w = decoder.width, h = decoder.height, edge = w > h ? w : h;
    unsigned target = edge < cap ? edge : cap;
    out_w = (w * target + edge / 2) / edge;
    out_h = (h * target + edge / 2) / edge;
    if (!out_w)
        out_w = 1;
    if (!out_h)
        out_h = 1;
    unsigned shift = 0;
    while (shift < 3 && (w >> (shift + 1)) >= out_w && (h >> (shift + 1)) >= out_h)
        shift++;
    decoder.scale = shift;
    sample_axis(sample_x, out_w, w >> shift);
    sample_axis(sample_y, out_h, h >> shift);
    mcu_x = mcu_y = 0;
    output = pixels;
    return true;
}

static unsigned first_sample(const uint16_t *samples, unsigned n, unsigned coord)
{
    unsigned low = 0, high = n;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        if (samples[mid] < coord)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

static int paint_mcu(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    const uint16_t *block = bitmap;
    unsigned pitch = rect->right - rect->left + 1;
    unsigned x0 = first_sample(sample_x, out_w, rect->left);
    unsigned y0 = first_sample(sample_y, out_h, rect->top);
    for (unsigned y = y0; y < out_h && sample_y[y] <= rect->bottom; y++) {
        const uint16_t *row = block + (sample_y[y] - rect->top) * pitch;
        for (unsigned x = x0; x < out_w && sample_x[x] <= rect->right; x++) {
            /* This TJpgDec build packs BGR565. The cover canvas uses RGB565. */
            uint16_t bgr = row[sample_x[x] - rect->left];
            output[y * out_w + x] = (uint16_t)((bgr << 11) | (bgr & 0x07e0) | (bgr >> 11));
        }
    }
    return 1;
}

int cover_jpeg_step(uint32_t *width, uint32_t *height)
{
    uint32_t start = aster_time_now_ms();
    /* TJpgDec's baseline scan can pause between minimum coded units. Keep
     * entropy state, predictors and the restart sequence across APP frames. */
    do {
        if (decoder.nrst && decoder.rst++ == decoder.nrst) {
            if (jd_restart(&decoder, decoder.rsc++) != JDR_OK)
                return -1;
            decoder.rst = 1;
        }
        if (jd_mcu_load(&decoder) != JDR_OK ||
            jd_mcu_output(&decoder, paint_mcu, mcu_x, mcu_y) != JDR_OK)
            return -1;
        mcu_x += decoder.msx * 8;
        if (mcu_x >= decoder.width) {
            mcu_x = 0;
            mcu_y += decoder.msy * 8;
        }
        if (mcu_y >= decoder.height) {
            *width = out_w;
            *height = out_h;
            return 1;
        }
    } while ((uint32_t)(aster_time_now_ms() - start) <
             (audio_priority ? PLAYING_SLICE_MS : IDLE_SLICE_MS));
    return 0;
}
