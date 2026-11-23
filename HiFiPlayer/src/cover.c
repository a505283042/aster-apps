/* SPDX-License-Identifier: MIT */
#include "cover.h"
#include "cover_jpeg.h"

enum { MAX_EMBEDDED = 4 * 1024 * 1024 };

static const char scratch[] = "/cache/hifi-cover.img";
/* The visible pair is never touched by an in-flight JPEG or image decode.
 * Switch tracks atomically only after the new image is completely ready. */
static uint16_t pixels[COVER_EDGE * COVER_EDGE];
static uint16_t dim_pixels[COVER_EDGE * COVER_EDGE];
static uint16_t staging_pixels[COVER_EDGE * COVER_EDGE];
static _Alignas(8) uint8_t chunk[COVER_WORK_BYTES];
static uint32_t image_w, image_h, remaining;
static aster_handle_t input, output;
static char track_path[256];
enum cover_phase { IDLE, SIDECAR, EXTERNAL, EMBEDDED, COPY, DECODE_COPY, JPEG, DEFAULT };
static enum cover_phase phase, jpeg_fallback;
static unsigned extension_index;
static uint32_t remote_revision;
static bool selected_initialized;
static bool audio_priority;

void cover_set_audio_priority(bool active)
{
    audio_priority = active;
}

struct picture {
    uint32_t offset, length;
    bool jpeg;
};

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static bool synchsafe(const uint8_t *p, uint32_t *value)
{
    if ((p[0] | p[1] | p[2] | p[3]) & 128)
        return false;
    *value = (uint32_t)p[0] << 21 | (uint32_t)p[1] << 14 | (uint32_t)p[2] << 7 | p[3];
    return true;
}

static bool read_at(uint32_t at, void *dst, uint32_t count)
{
    return aster_fs_seek(input, at, ASTER_FS_SEEK_SET) >= 0 &&
           aster_fs_read(input, dst, count) == (int32_t)count;
}

static bool picture_at(uint32_t at, uint32_t length, bool front, struct picture *out)
{
    static const uint8_t png[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    uint8_t magic[8];
    if (length < sizeof(magic) || length > MAX_EMBEDDED || !read_at(at, magic, sizeof(magic)))
        return false;
    if (memcmp(magic, png, sizeof(png)) && !(magic[0] == 255 && magic[1] == 216) &&
        memcmp(magic, "BM", 2))
        return false;
    if (!out->length || front)
        *out = (struct picture){at, length, magic[0] == 255 && magic[1] == 216};
    return front;
}

static bool id3_picture(uint32_t at, uint32_t length, bool legacy, struct picture *out)
{
    uint8_t head[768];
    unsigned n = length < sizeof(head) ? length : sizeof(head);
    if (n < 6 || !read_at(at, head, n) || head[0] > 3)
        return false;
    unsigned pos = legacy ? 4 : 1;
    if (!legacy) {
        while (pos < n && head[pos])
            pos++;
        pos++;
    }
    if (pos >= n)
        return false;
    bool front = head[pos++] == 3;
    unsigned unit = head[0] == 1 || head[0] == 2 ? 2 : 1;
    for (; pos + unit <= n; pos += unit) {
        if (head[pos] || (unit == 2 && head[pos + 1]))
            continue;
        pos += unit;
        return picture_at(at + pos, length - pos, front, out);
    }
    return false;
}

static void id3(uint32_t at, uint32_t end, struct picture *out)
{
    uint8_t head[10];
    uint32_t size;
    if (end - at < sizeof(head) || !read_at(at, head, sizeof(head)) || memcmp(head, "ID3", 3) ||
        !synchsafe(head + 6, &size) || size > end - at - sizeof(head))
        return;
    unsigned version = head[3], flags = head[5];
    if (version < 2 || version > 4 || (version < 4 && (flags & 128)) ||
        (version == 2 && (flags & 64)))
        return;
    at += sizeof(head);
    end = at + size;
    if (version >= 3 && (flags & 64)) {
        if (end - at < 4 || !read_at(at, head, 4))
            return;
        if (version == 3) {
            size = be32(head);
            if (size > end - at - 4)
                return;
            size += 4;
        } else if (!synchsafe(head, &size) || size < 6 || size > end - at)
            return;
        at += size;
    }
    unsigned header_size = version == 2 ? 6 : 10;
    for (unsigned i = 0; i < 256 && end - at >= header_size; i++) {
        if (!read_at(at, head, header_size) || !head[0])
            break;
        if (version == 2)
            size = (uint32_t)head[3] << 16 | (uint32_t)head[4] << 8 | head[5];
        else if (version == 3)
            size = be32(head + 4);
        else if (!synchsafe(head + 4, &size))
            break;
        at += header_size;
        if (size > end - at)
            break;
        if (version == 2 ? !memcmp(head, "PIC", 3) : !memcmp(head, "APIC", 4)) {
            unsigned extra = 0;
            bool encoded = false;
            if (version == 3) {
                encoded = (head[9] & 0xc0) != 0;
                extra = (head[9] & 0x20) != 0;
            } else if (version == 4) {
                encoded = (head[9] & 0x0e) != 0 || (flags & 128);
                extra = ((head[9] & 0x40) != 0) + ((head[9] & 1) ? 4 : 0);
            }
            if (!encoded && size > extra &&
                id3_picture(at + extra, size - extra, version == 2, out))
                break;
        }
        at += size;
    }
}

static bool flac_picture(uint32_t at, uint32_t length, struct picture *out)
{
    uint8_t fields[20];
    if (length < 32 || !read_at(at, fields, 8))
        return false;
    bool front = be32(fields) == 3;
    uint32_t mime_size = be32(fields + 4), end = at + length;
    at += 8;
    if (mime_size > end - at - 4)
        return false;
    at += mime_size;
    if (!read_at(at, fields, 4))
        return false;
    uint32_t description = be32(fields);
    at += 4;
    if (description > end - at || end - at - description < sizeof(fields))
        return false;
    at += description;
    if (!read_at(at, fields, sizeof(fields)))
        return false;
    at += sizeof(fields);
    uint32_t data = be32(fields + 16);
    return data <= end - at && picture_at(at, data, front, out);
}

static void flac(uint32_t end, struct picture *out)
{
    uint32_t at = 4;
    for (unsigned i = 0; i < 128 && end - at >= 4; i++) {
        uint8_t head[4];
        if (!read_at(at, head, sizeof(head)))
            return;
        uint32_t length = be32(head) & 0xffffffu;
        at += sizeof(head);
        if (length > end - at || ((head[0] & 127) == 6 && flac_picture(at, length, out)) ||
            (head[0] & 128))
            return;
        at += length;
    }
}

static void wave(uint32_t end, struct picture *out)
{
    uint32_t at = 12;
    for (unsigned i = 0; i < 128 && end - at >= 8; i++) {
        uint8_t head[8];
        if (!read_at(at, head, sizeof(head)))
            return;
        uint32_t length = le32(head + 4);
        at += sizeof(head);
        if (length > end - at)
            return;
        if (!memcmp(head, "id3 ", 4) || !memcmp(head, "ID3 ", 4)) {
            id3(at, at + length, out);
            if (out->length)
                return;
        }
        if (length + (length & 1) > end - at)
            return;
        at += length + (length & 1);
    }
}

static void close_files(void)
{
    if (input)
        aster_fs_close(input);
    if (output)
        aster_fs_close(output);
    if (output || phase == DECODE_COPY)
        aster_fs_unlink(scratch, sizeof(scratch) - 1);
    input = output = remaining = 0;
}

/* Optional ABI V2 uses the exact first 40 bytes of the existing
 * aster_img_render_args_t. Declared locally so an installed SDK 4.5.0 can
 * still compile HiFi. Old firmware rejects version=2 and keeps TJpgDec.
 * No new mandatory WASM import or second guest-side cover buffer. */
typedef struct {
    uint32_t size, version;
    uint32_t dst_off, dst_span, cap_w, cap_h, out_w, out_h, flags, reserved;
    uint32_t src_offset, src_length, dim_off, dim_span, result_flags, reserved2;
} hifi_cover_hw_args_t;
_Static_assert(sizeof(hifi_cover_hw_args_t) == 64, "cover V2 ABI size changed");
_Static_assert(sizeof(aster_img_render_args_t) == 40, "cover V1 ABI prefix changed");

static bool hardware_jpeg(const char *path, uint32_t offset, uint32_t length, unsigned cap)
{
    if (!path || !*path || !cap || cap > COVER_EDGE)
        return false;
    hifi_cover_hw_args_t args = {
        .size = sizeof(args),
        .version = 2,
        .dst_off = (uint32_t)(uintptr_t)pixels,
        .dst_span = sizeof(pixels),
        .dim_off = (uint32_t)(uintptr_t)dim_pixels,
        .dim_span = sizeof(dim_pixels),
        .cap_w = cap,
        .cap_h = cap,
        .src_offset = offset,
        .src_length = length
    };
    /* The same imported function accepts the larger structure only on new
     * S31 firmware. Neither destination is modified on a failed V2 call. */
    if (aster_img_render(path, strlen(path), (aster_img_render_args_t *)&args,
                         sizeof(args)) < 0 ||
        !args.out_w || !args.out_h || args.out_w > cap || args.out_h > cap ||
        !(args.result_flags & 1u))
        return false;
    image_w = args.out_w;
    image_h = args.out_h;
    phase = IDLE;
    aster_printf("HiFi cover swap %ux%u backend=JPEG_HW dim=%s\n",
                 image_w, image_h, (args.result_flags & 2u) ? "PPA" : "CPU");
    return true;
}

/* A JPEG sidecar is tried before any music-file embedded APIC/PICTURE.
 * Hardware failure preserves the old cover while legacy incremental
 * TJpgDec reads the exact same already-open offset/length region. */
static bool begin_jpeg(const char *source, uint32_t offset, uint32_t length,
                       unsigned cap, enum cover_phase fallback)
{
    /* A synchronous JPEG HW + PSRAM/PPA operation can occupy 100+ ms.
     * Keep the native fast path when idle/paused; while local music plays,
     * TJpgDec yields after bounded MCU slices and shares the existing file
     * handle, leaving the audio worker frequent opportunities for SD I/O. */
    if (!audio_priority && hardware_jpeg(source, offset, length, cap)) {
        close_files();
        return true;
    }
    if (cover_jpeg_begin(input, offset, length, cap, staging_pixels, chunk)) {
        jpeg_fallback = fallback;
        phase = JPEG;
    } else {
        close_files();
        phase = fallback;
    }
    return false;
}

static void begin_embedded(unsigned cap)
{
    phase = DEFAULT;
    aster_stat_t st = {.size = sizeof(st), .version = 1};
    uint32_t n = strlen(track_path);
    if (aster_fs_stat(track_path, n, &st, sizeof(st)) < 0 || st.filesize < 12 ||
        aster_fs_open(track_path, n, ASTER_FS_O_RDONLY, &input) < 0)
        return;
    uint32_t size = st.filesize > INT32_MAX ? INT32_MAX : (uint32_t)st.filesize;
    uint8_t head[12];
    struct picture picture = {0};
    if (read_at(0, head, sizeof(head))) {
        if (!memcmp(head, "fLaC", 4))
            flac(size, &picture);
        else if (!memcmp(head, "ID3", 3))
            id3(0, size, &picture);
        else if (!memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVE", 4))
            wave(size, &picture);
    }
    if (picture.jpeg) {
        (void)begin_jpeg(track_path, picture.offset, picture.length, cap, DEFAULT);
        return;
    }
    if (picture.length && aster_fs_seek(input, picture.offset, ASTER_FS_SEEK_SET) >= 0) {
        aster_fs_mkdir("/cache", 6);
        if (aster_fs_open(scratch, sizeof(scratch) - 1,
                          ASTER_FS_O_WRONLY | ASTER_FS_O_CREAT | ASTER_FS_O_TRUNC, &output) >= 0) {
            remaining = picture.length;
            phase = COPY;
            return;
        }
    }
    close_files();
}

static bool finish_image(uint32_t w, uint32_t h)
{
    /* No intermediate default frame: decode first, swap completed cover once. */
    unsigned total = w * h;
    if (!w || !h || w > COVER_EDGE || h > COVER_EDGE)
        return false;
    memcpy(pixels, staging_pixels, total * sizeof(uint16_t));
    for (unsigned i = 0; i < total; i++) {
        uint16_t color = pixels[i];
        dim_pixels[i] = (uint16_t)((((color >> 11) * 105 / 255) << 11) |
                                   ((((color >> 5) & 63) * 105 / 255) << 5) |
                                   ((color & 31) * 105 / 255));
    }
    image_w = w;
    image_h = h;
    phase = IDLE;
    aster_printf("HiFi cover swap %ux%u\n", image_w, image_h);
    return true;
}

static bool decode_native(const char *path, unsigned cap)
{
    /* JPEG default artwork also takes the bounded S31 fast path first.
     * Non-JPEG PNG/BMP still use the unchanged firmware V1 decoder. */
    const char *ext = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '.') ext = p;
    if (ext && (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) &&
        hardware_jpeg(path, 0, 0, cap))
        return true;
    aster_img_render_args_t args = {.size = sizeof(args),
                                    .version = 1,
                                    .dst_off = (uint32_t)(uintptr_t)staging_pixels,
                                    .dst_span = sizeof(staging_pixels),
                                    .cap_w = cap,
                                    .cap_h = cap};
    if (aster_img_render(path, strlen(path), &args, sizeof(args)) < 0 || !args.out_w ||
        !args.out_h || args.out_w > cap || args.out_h > cap)
        return false;
    return finish_image(args.out_w, args.out_h);
}

static bool open_image(const char *path, unsigned cap, enum cover_phase fallback)
{
    phase = fallback;
    unsigned n = strlen(path);
    if (aster_fs_open(path, n, ASTER_FS_O_RDONLY, &input) < 0)
        return false;
    uint8_t magic[2];
    if (read_at(0, magic, sizeof(magic)) && magic[0] == 255 && magic[1] == 216) {
        aster_stat_t st = {.size = sizeof(st), .version = 1};
        if (aster_fs_stat(path, n, &st, sizeof(st)) == 0 && st.filesize <= INT32_MAX)
            return begin_jpeg(path, 0, (uint32_t)st.filesize, cap, fallback);
        close_files();
        return false;
    }
    close_files();
    return decode_native(path, cap);
}

static bool sidecar(unsigned cap)
{
    static const char *extensions[] = {".jpg", ".jpeg", ".png", ".bmp"};
    const char *ext = NULL;
    for (const char *p = track_path; *p; p++)
        if (*p == '.')
            ext = p;
    if (!ext || extension_index >= sizeof(extensions) / sizeof(extensions[0])) {
        phase = EMBEDDED;
        return false;
    }
    size_t prefix = ext - track_path;
    char path[256];
    if (prefix + 6 > sizeof(path)) {
        phase = EMBEDDED;
        return false;
    }
    memcpy(path, track_path, prefix);
    const char *extension = extensions[extension_index++];
    memcpy(path + prefix, extension, strlen(extension) + 1);
    return open_image(path, cap, SIDECAR);
}

void cover_select(const char *track)
{
    if (!remote_revision && selected_initialized && !strcmp(track, track_path))
        return;
    selected_initialized = true;
    remote_revision = 0;
    close_files();
    aster_snprintf(track_path, sizeof(track_path), "%s", track);
    /* Keep current pixels visible while the next album cover is loading. */
    extension_index = 0;
    phase = !*track ? DEFAULT : !strcmp(track, "/assets/audio/demo.flac") ? EXTERNAL : SIDECAR;
}

void cover_select_remote(const char *path, uint32_t revision)
{
    if (!revision || !*path) {
        cover_select("");
        return;
    }
    if (remote_revision == revision && !strcmp(path, track_path))
        return;
    close_files();
    selected_initialized = true;
    aster_snprintf(track_path, sizeof(track_path), "%s", path);
    remote_revision = revision;
    /* Remote thumbnail may arrive much later; retain the last cover. */
    phase = EXTERNAL;
}

bool cover_tick(unsigned cap)
{
    if (cap > COVER_EDGE)
        cap = COVER_EDGE;
    if (!cap || phase == IDLE)
        return false;
    if (phase == SIDECAR)
        return sidecar(cap);
    if (phase == EXTERNAL)
        return open_image(remote_revision ? track_path : "/assets/art/no_cover_artwork.jpg", cap, DEFAULT);
    if (phase == DEFAULT) {
        phase = IDLE;
        /* Prefer the requested bundled cover when available, otherwise allow
         * an SD /music override. Never flash a placeholder on a track change. */
        if (decode_native("/assets/art/no_cover_artwork.jpg", cap))
            return true;
        if (decode_native("/music/no_cover_artwork.jpg", cap))
            return true;
        return decode_native("/assets/art/moonlight.bmp", cap);
    }
    if (phase == EMBEDDED) {
        begin_embedded(cap);
        /* begin_embedded may finish a hardware JPEG synchronously. The
         * completed pair must redraw on THIS tick, not on a stale frame. */
        return phase == IDLE;
    }
    if (phase == JPEG) {
        uint32_t w, h;
        int result = cover_jpeg_step(&w, &h);
        if (!result)
            return false;
        close_files();
        phase = jpeg_fallback;
        return result > 0 && finish_image(w, h);
    }
    if (phase == DECODE_COPY) {
        phase = IDLE;
        bool ok = decode_native(scratch, cap);
        aster_fs_unlink(scratch, sizeof(scratch) - 1);
        if (!ok)
            phase = DEFAULT;
        return ok;
    }
    if (phase != COPY)
        return false;
    uint32_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    if (aster_fs_read(input, chunk, n) != (int32_t)n ||
        aster_fs_write(output, chunk, n) != (int32_t)n) {
        close_files();
        phase = IDLE;
        return false;
    }
    remaining -= n;
    if (remaining)
        return false;
    aster_fs_close(output);
    output = 0;
    aster_fs_close(input);
    input = 0;
    phase = DECODE_COPY;
    return false;
}

void cover_close(void)
{
    close_files();
    phase = IDLE;
    selected_initialized = false;
    remote_revision = 0;
    image_w = image_h = 0;
    track_path[0] = 0;
}

bool cover_ready(void)
{
    return image_w != 0;
}

uint16_t cover_mix(uint16_t base, uint16_t color, unsigned weight)
{
    unsigned keep = 256 - weight;
    unsigned r = ((base >> 11) * keep + (color >> 11) * weight) >> 8;
    unsigned g = (((base >> 5) & 63) * keep + ((color >> 5) & 63) * weight) >> 8;
    unsigned b = ((base & 31) * keep + (color & 31) * weight) >> 8;
    return (r << 11) | (g << 5) | b;
}

/* Fill the 460x460 square directly. Both views use the same decoded image,
 * center-cropped to cover the display (rather than the old 208px inset).
 * Selection of the dim variant is a pointer switch, not a new decode. */
bool cover_paint(aster_handle_t surface, unsigned left, unsigned top, unsigned edge, uint16_t base,
                 bool dimmed)
{
    if (!cover_ready() || !edge)
        return false;
    aster_surface_buffer_t buf = {.size = sizeof(buf), .version = 1};
    if (aster_surface_acquire(surface, &buf, sizeof(buf)) < 0 || !buf.ptr ||
        buf.format != ASTER_PIXEL_RGB565 || left + edge > buf.width || top + edge > buf.height)
        return false;

    uint16_t *dest = buf.ptr;
    unsigned pitch = buf.pitch / sizeof(uint16_t);
    const uint16_t *art = dimmed ? dim_pixels : pixels;
    /* Typical square album art is decoded at exactly 460x460. In this
     * common case only copy the already-baked original/dimmed RGB565 surface. */
    if (image_w == edge && image_h == edge && left == 0 && top == 0 &&
        buf.width == edge && buf.height == edge && pitch == edge) {
        memcpy(dest, art, edge * edge * sizeof(uint16_t));
        return true;
    }
    unsigned crop = image_w < image_h ? image_w : image_h;
    unsigned x_crop = (image_w - crop) / 2;
    unsigned y_crop = (image_h - crop) / 2;

    for (unsigned y = 0; y < buf.height; y++) {
        uint16_t *row = dest + y * pitch;
        if (y < top || y >= top + edge) {
            for (unsigned x = 0; x < buf.width; x++)
                row[x] = base;
            continue;
        }
        unsigned source_y = y_crop + (y - top) * crop / edge;
        const uint16_t *source_row = art + source_y * image_w;
        for (unsigned x = 0; x < buf.width; x++) {
            if (x < left || x >= left + edge)
                row[x] = base;
            else
                row[x] = source_row[x_crop + (x - left) * crop / edge];
        }
    }
    return true;
}
