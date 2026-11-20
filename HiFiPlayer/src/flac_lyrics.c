/* SPDX-License-Identifier: MIT */
#include "flac_lyrics.h"
#include "lrc.h"
#ifdef __wasm__
#include <aster_libc.h>
#else
#include <string.h>
#endif

/* Offsets let the reader skip embedded artwork and padding without allocating
 * them. Cache nearby tag headers to avoid a seek for each four-byte length. */
static bool peek(flac_lyrics_t *s, uint32_t at, void *dst, uint32_t n)
{
    if (at > s->end || n > s->end - at || n > sizeof(s->cache))
        return false;
    if (at < s->cache_at || at - s->cache_at > s->cached ||
        n > s->cached - (at - s->cache_at)) {
        if (aster_fs_seek(s->file, at, ASTER_FS_SEEK_SET) < 0)
            return false;
        s->cache_at = at;
        s->cached = 0;
        uint32_t want = s->end - at;
        if (want > sizeof(s->cache))
            want = sizeof(s->cache);
        while (s->cached < n) {
            int got = aster_fs_read(s->file, s->cache + s->cached, want - s->cached);
            if (got <= 0 || (uint32_t)got > want - s->cached)
                return false;
            s->cached += got;
        }
    }
    memcpy(dst, s->cache + (at - s->cache_at), n);
    return true;
}

static bool read_u32(flac_lyrics_t *s, uint32_t at, uint32_t *out)
{
    uint8_t b[4];
    if (!peek(s, at, b, sizeof(b)))
        return false;
    *out = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return true;
}

bool flac_lyrics_begin(flac_lyrics_t *s, aster_handle_t file, uint32_t size)
{
    *s = (flac_lyrics_t){.file = file, .end = size > INT32_MAX ? INT32_MAX : size, .block = 4};
    char magic[4];
    return peek(s, 0, magic, sizeof(magic)) && !memcmp(magic, "fLaC", 4);
}

static unsigned lyric_key(uint8_t *prefix, unsigned n)
{
    static const char *const keys[] = {
        "LYRICS", "LYRIC", "SYNCEDLYRICS", "UNSYNCEDLYRICS", "SYNCHRONIZEDLYRICS",
        "UNSYNCHRONIZEDLYRICS", "UNSYNCED LYRICS", "SYNCED LYRICS"
    };
    for (unsigned i = 0; i < n; i++) {
        if (prefix[i] >= 'a' && prefix[i] <= 'z')
            prefix[i] -= 'a' - 'A';
        if (prefix[i] != '=')
            continue;
        for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
            if (strlen(keys[k]) == i && !memcmp(prefix, keys[k], i))
                return i + 1;
        break;
    }
    return 0;
}

enum flac_result flac_lyrics_next(flac_lyrics_t *s, lyric_span_t *value)
{
    for (unsigned step = 0; step < 12; step++) {
        if (s->remaining) {
            uint32_t len;
            if (s->comments_end - s->comment < 4 || !read_u32(s, s->comment, &len))
                return FLAC_INVALID;
            uint32_t start = s->comment + 4;
            if (len > s->comments_end - start)
                return FLAC_INVALID;
            s->remaining--;
            s->comment = start + len;
            uint8_t prefix[32];
            unsigned n = len < sizeof(prefix) ? len : sizeof(prefix);
            if (!n)
                continue;
            if (!peek(s, start, prefix, n))
                return FLAC_INVALID;
            unsigned key = lyric_key(prefix, n);
            if (key && len > key && len - key <= LRC_FILE_LIMIT) {
                *value = (lyric_span_t){start + key, len - key};
                return FLAC_FOUND;
            }
            continue;
        }
        if (s->last)
            return FLAC_DONE;
        uint8_t h[4];
        if (!peek(s, s->block, h, sizeof(h)))
            return FLAC_INVALID;
        uint32_t body = s->block + 4;
        uint32_t len = (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
        if (len > s->end - body)
            return FLAC_INVALID;
        s->block = body + len;
        s->last = (h[0] & 128) != 0;
        if ((h[0] & 127) != 4)
            continue;
        uint32_t vendor;
        if (len < 8 || !read_u32(s, body, &vendor) || vendor > len - 8 ||
            !read_u32(s, body + 4 + vendor, &s->remaining))
            return FLAC_INVALID;
        s->comment = body + 8 + vendor;
        s->comments_end = s->block;
        if (s->remaining > (s->comments_end - s->comment) / 4)
            return FLAC_INVALID;
    }
    return FLAC_MORE;
}
