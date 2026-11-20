/* SPDX-License-Identifier: MIT */
#include "cover.h"

enum { COVER_EDGE = 208, COPY_CHUNK = 16384, MAX_EMBEDDED = 4 * 1024 * 1024 };

static const char scratch[] = "/cache/hifi-cover.img";
static uint16_t pixels[COVER_EDGE * COVER_EDGE];
static uint8_t chunk[COPY_CHUNK];
static uint32_t image_w, image_h, remaining;
static uint16_t tint;
static aster_handle_t input, output;
static char track_path[256];
static bool pending;
static uint32_t remote_revision;

struct picture {
    uint32_t offset, length;
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
        *out = (struct picture){at, length};
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

static void close_copy(void)
{
    if (input)
        aster_fs_close(input);
    if (output) {
        aster_fs_close(output);
        aster_fs_unlink(scratch, sizeof(scratch) - 1);
    }
    input = output = remaining = 0;
}

static bool begin_copy(void)
{
    aster_stat_t st = {.size = sizeof(st), .version = 1};
    uint32_t n = strlen(track_path);
    if (aster_fs_stat(track_path, n, &st, sizeof(st)) < 0 || st.filesize < 12 ||
        aster_fs_open(track_path, n, ASTER_FS_O_RDONLY, &input) < 0)
        return false;
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
    if (picture.length && aster_fs_seek(input, picture.offset, ASTER_FS_SEEK_SET) >= 0) {
        aster_fs_mkdir("/cache", 6);
        if (aster_fs_open(scratch, sizeof(scratch) - 1,
                          ASTER_FS_O_WRONLY | ASTER_FS_O_CREAT | ASTER_FS_O_TRUNC, &output) >= 0) {
            remaining = picture.length;
            return true;
        }
    }
    close_copy();
    return false;
}

static bool decode(const char *path, unsigned cap)
{
    if (cap > COVER_EDGE)
        cap = COVER_EDGE;
    if (!cap)
        return false;
    aster_img_render_args_t args = {.size = sizeof(args),
                                    .version = 1,
                                    .dst_off = (uint32_t)(uintptr_t)pixels,
                                    .dst_span = sizeof(pixels),
                                    .cap_w = cap,
                                    .cap_h = cap};
    if (aster_img_render(path, strlen(path), &args, sizeof(args)) < 0 || !args.out_w ||
        !args.out_h || args.out_w > cap || args.out_h > cap)
        return false;
    image_w = args.out_w;
    image_h = args.out_h;
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
    aster_printf("HiFi cover %ux%u", image_w, image_h);
    return true;
}

static bool sidecar(unsigned cap)
{
    static const char *extensions[] = {".jpg", ".jpeg", ".png", ".bmp"};
    const char *ext = NULL;
    for (const char *p = track_path; *p; p++)
        if (*p == '.')
            ext = p;
    if (!ext)
        return false;
    size_t prefix = ext - track_path;
    char path[256];
    if (prefix + 6 > sizeof(path))
        return false;
    memcpy(path, track_path, prefix);
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        memcpy(path + prefix, extensions[i], strlen(extensions[i]) + 1);
        if (decode(path, cap))
            return true;
    }
    return false;
}

void cover_select(const char *track)
{
    if (!remote_revision && !strcmp(track, track_path))
        return;
    remote_revision = 0;
    close_copy();
    aster_snprintf(track_path, sizeof(track_path), "%s", track);
    image_w = image_h = 0;
    pending = *track != 0;
}

void cover_select_remote(const char *path, uint32_t revision)
{
    if (!revision || !*path) {
        cover_select("");
        return;
    }
    if (remote_revision == revision && !strcmp(path, track_path))
        return;
    close_copy();
    aster_snprintf(track_path, sizeof(track_path), "%s", path);
    remote_revision = revision;
    image_w = image_h = 0;
    pending = true;
}

bool cover_tick(unsigned cap)
{
    if (pending) {
        pending = false;
        if (remote_revision)
            return decode(track_path, cap);
        if (!strcmp(track_path, "/assets/audio/demo.flac"))
            return decode("/assets/art/moonlight.bmp", cap);
        if (sidecar(cap))
            return true;
        begin_copy();
    }
    if (!remaining)
        return false;
    uint32_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    if (aster_fs_read(input, chunk, n) != (int32_t)n ||
        aster_fs_write(output, chunk, n) != (int32_t)n) {
        close_copy();
        return false;
    }
    remaining -= n;
    if (remaining)
        return false;
    aster_fs_close(output);
    output = 0;
    aster_fs_close(input);
    input = 0;
    bool ok = decode(scratch, cap);
    aster_fs_unlink(scratch, sizeof(scratch) - 1);
    return ok;
}

void cover_close(void)
{
    close_copy();
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

bool cover_paint(aster_handle_t surface, unsigned left, unsigned top, unsigned edge, uint16_t base,
                 uint16_t accent)
{
    if (!cover_ready())
        return false;
    aster_surface_buffer_t buf = {.size = sizeof(buf), .version = 1};
    if (aster_surface_acquire(surface, &buf, sizeof(buf)) < 0 || !buf.ptr ||
        buf.format != ASTER_PIXEL_RGB565 || left + edge > buf.width || top + edge > buf.height)
        return false;
    unsigned pitch = buf.pitch / sizeof(uint16_t);
    unsigned ambient_h = edge * 225 / 460;
    unsigned max_dimension = image_w > image_h ? image_w : image_h;
    unsigned h = image_h * edge * 208 / 460 / max_dimension;
    unsigned w = image_w * edge * 208 / 460 / max_dimension;
    if (!w)
        w = 1;
    if (!h)
        h = 1;
    unsigned ax = left + (edge - w) / 2, ay = top + (ambient_h - h) / 2;
    uint16_t *dest = buf.ptr;
    uint16_t glow = cover_mix(cover_mix(base, accent, 75), tint, 44);
    uint16_t *base_row = dest + (top + ambient_h) * pitch;
    for (unsigned x = 0; x < buf.width; x++)
        base_row[x] = base;
    for (unsigned y = 0; y < buf.height; y++) {
        uint16_t *row = dest + y * pitch;
        if (y < top || y >= top + ambient_h) {
            if (row != base_row)
                memcpy(row, base_row, buf.width * sizeof(*row));
            continue;
        }
        unsigned depth = top + ambient_h - y;
        uint16_t edge_tone = cover_mix(base, glow, 1 + depth * 113 / ambient_h);
        uint16_t center_tone = cover_mix(base, glow, 1 + depth * 179 / ambient_h);
        for (unsigned x = 0; x < buf.width; x++) {
            unsigned offset = x < left ? edge : x - left;
            unsigned distance = offset < edge / 2 ? offset : offset < edge ? edge - offset : 0;
            row[x] = cover_mix(edge_tone, center_tone, distance * 512 / edge);
        }
        if (y < ay || y >= ay + h)
            continue;
        const uint16_t *src = pixels + (y - ay) * image_h / h * image_w;
        if (w == image_w)
            memcpy(row + ax, src, w * sizeof(*src));
        else
            for (unsigned x = 0; x < w; x++)
                row[ax + x] = src[x * image_w / w];
    }
    return true;
}
