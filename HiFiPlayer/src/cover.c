/* SPDX-License-Identifier: MIT */
#include "cover.h"
#include "cover_jpeg.h"

enum { MAX_EMBEDDED = 4 * 1024 * 1024 };

static const char scratch[] = "/cache/hifi-cover.img";
static uint16_t pixels[COVER_EDGE * COVER_EDGE];
static _Alignas(8) uint8_t chunk[COVER_WORK_BYTES];
static uint32_t image_w, image_h, remaining;
static uint16_t tint;
static aster_handle_t input, output;
static char track_path[256];
enum cover_phase { IDLE, SIDECAR, EXTERNAL, EMBEDDED, COPY, DECODE_COPY, JPEG };
static enum cover_phase phase, jpeg_fallback;
static unsigned extension_index;
static uint32_t remote_revision;

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

static void begin_jpeg(uint32_t offset, uint32_t length, unsigned cap, enum cover_phase fallback)
{
    if (cover_jpeg_begin(input, offset, length, cap, pixels, chunk)) {
        jpeg_fallback = fallback;
        phase = JPEG;
    } else {
        close_files();
        phase = fallback;
    }
}

static void begin_embedded(unsigned cap)
{
    phase = IDLE;
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
        begin_jpeg(picture.offset, picture.length, cap, IDLE);
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
    image_w = w;
    image_h = h;
    phase = IDLE;
    unsigned r = 0, g = 0, b = 0, count = 0;
    for (unsigned y = 0; y < image_h; y += 4) {
        for (unsigned x = 0; x < image_w; x += 4) {
            uint16_t p = pixels[y * image_w + x];
            r += p >> 11;
            g += (p >> 5) & 63;
            b += p & 31;
            count++;
        }
    }
    tint = (r / count) << 11 | (g / count) << 5 | b / count;
    aster_printf("HiFi cover %ux%u\n", image_w, image_h);
    return true;
}

static bool decode_native(const char *path, unsigned cap, aster_handle_t surface)
{
    aster_img_render_args_t args = {.size = sizeof(args),
                                    .version = 1,
                                    .dst_off = (uint32_t)(uintptr_t)pixels,
                                    .dst_span = sizeof(pixels),
                                    .cap_w = cap,
                                    .cap_h = cap};
    if (aster_img_render(path, strlen(path), &args, sizeof(args)) < 0 || !args.out_w ||
        !args.out_h || args.out_w > cap || args.out_h > cap)
        return false;
    if (args.out_w == cap && args.out_h == cap)
        return finish_image(cap, cap);

    /* The native PNG/BMP decoder fits the source inside cap x cap. Resample
     * its centre square using the already allocated APP surface as scratch.
     * No third RGB565 buffer is needed, including for portrait covers. */
    aster_surface_buffer_t buf = {.size = sizeof(buf), .version = 1};
    if (aster_surface_acquire(surface, &buf, sizeof(buf)) < 0 || !buf.ptr ||
        buf.format != ASTER_PIXEL_RGB565 || buf.width < cap || buf.height < cap)
        return finish_image(args.out_w, args.out_h);
    uint16_t *scratch = buf.ptr;
    unsigned pitch = buf.pitch / sizeof(uint16_t);
    unsigned w = args.out_w, h = args.out_h;
    unsigned side = w < h ? w : h;
    unsigned ox = (w - side) / 2, oy = (h - side) / 2;
    for (unsigned y = 0; y < cap; y++) {
        const uint16_t *src = pixels + (oy + (uint64_t)y * side / cap) * w;
        uint16_t *dst = scratch + (size_t)y * pitch;
        for (unsigned x = 0; x < cap; x++)
            dst[x] = src[ox + (uint64_t)x * side / cap];
    }
    for (unsigned y = 0; y < cap; y++)
        memcpy(pixels + (size_t)y * cap, scratch + (size_t)y * pitch,
               cap * sizeof(uint16_t));
    return finish_image(cap, cap);
}

static bool open_image(const char *path, unsigned cap, enum cover_phase fallback,
                       aster_handle_t surface)
{
    phase = fallback;
    unsigned n = strlen(path);
    if (aster_fs_open(path, n, ASTER_FS_O_RDONLY, &input) < 0)
        return false;
    uint8_t magic[2];
    if (read_at(0, magic, sizeof(magic)) && magic[0] == 255 && magic[1] == 216) {
        aster_stat_t st = {.size = sizeof(st), .version = 1};
        if (aster_fs_stat(path, n, &st, sizeof(st)) == 0 && st.filesize <= INT32_MAX)
            begin_jpeg(0, (uint32_t)st.filesize, cap, fallback);
        else
            close_files();
        return false;
    }
    close_files();
    return decode_native(path, cap, surface);
}

static bool sidecar(unsigned cap, aster_handle_t surface)
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
    return open_image(path, cap, SIDECAR, surface);
}

void cover_select(const char *track)
{
    if (!remote_revision && !strcmp(track, track_path))
        return;
    remote_revision = 0;
    close_files();
    aster_snprintf(track_path, sizeof(track_path), "%s", track);
    image_w = image_h = 0;
    extension_index = 0;
    phase = !*track ? IDLE : !strcmp(track, "/assets/audio/demo.flac") ? EXTERNAL : SIDECAR;
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
    aster_snprintf(track_path, sizeof(track_path), "%s", path);
    remote_revision = revision;
    image_w = image_h = 0;
    phase = EXTERNAL;
}

bool cover_tick(aster_handle_t surface, unsigned cap)
{
    if (cap > COVER_EDGE)
        cap = COVER_EDGE;
    if (!cap || phase == IDLE)
        return false;
    if (phase == SIDECAR)
        return sidecar(cap, surface);
    if (phase == EXTERNAL)
        return open_image(remote_revision ? track_path : "/assets/art/moonlight.bmp", cap, IDLE,
                          surface);
    if (phase == EMBEDDED) {
        begin_embedded(cap);
        return false;
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
        bool ok = decode_native(scratch, cap, surface);
        aster_fs_unlink(scratch, sizeof(scratch) - 1);
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

/* The unmodified 460 x 460 RGB565 cover stays in 'pixels'. The existing APP
 * surface is the only dimmed/display buffer. Repainting restores the complete
 * background before labels and controls are redrawn, so no ghosts accumulate. */
bool cover_paint(aster_handle_t surface, unsigned left, unsigned top, unsigned edge, uint16_t base,
                 uint16_t accent)
{
    if (!cover_ready())
        return false;
    aster_surface_buffer_t buf = {.size = sizeof(buf), .version = 1};
    if (aster_surface_acquire(surface, &buf, sizeof(buf)) < 0 || !buf.ptr ||
        buf.format != ASTER_PIXEL_RGB565 || left + edge > buf.width || top + edge > buf.height)
        return false;
    uint16_t *dest = buf.ptr;
    unsigned pitch = buf.pitch / sizeof(uint16_t);
    unsigned side = image_w < image_h ? image_w : image_h;
    unsigned ox = (image_w - side) / 2, oy = (image_h - side) / 2;
    uint16_t shade_base = cover_mix(base, accent, 12);
    for (unsigned y = 0; y < edge; y++) {
        const uint16_t *src = pixels + (oy + (uint64_t)y * side / edge) * image_w;
        uint16_t *dst = dest + (top + y) * pitch + left;
        /* Keep the cover recognisable in the top half; darken progressively
         * behind the title, timeline and five touch controls below. */
        unsigned shade = 104 + 44 * y / edge;
        if (y > edge / 2)
            shade += 65 * (y - edge / 2) / (edge - edge / 2);
        for (unsigned x = 0; x < edge; x++) {
            uint16_t original = src[ox + (uint64_t)x * side / edge];
            dst[x] = cover_mix(original, shade_base, shade);
        }
    }
    return true;
}
