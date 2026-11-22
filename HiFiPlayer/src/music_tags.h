/* SPDX-License-Identifier: MIT
 * Metadata fields, adapted from FakePod_S31 media_metadata.(h|cpp).
 * Tags are optional: an empty field means "not present", not a guess
 * derived from the filename or folder.
 */
#ifndef HIFI_MUSIC_TAGS_H
#define HIFI_MUSIC_TAGS_H
#include <stdbool.h>
#include <stdint.h>
enum { HIFI_TAG_TEXT_MAX = 64 };
typedef struct {
    char title[HIFI_TAG_TEXT_MAX];
    char artist[HIFI_TAG_TEXT_MAX];
    char album[HIFI_TAG_TEXT_MAX];
    char album_artist[HIFI_TAG_TEXT_MAX];
    uint16_t year, original_year, track_number, disc_number;
} hifi_tags_t;
/* The caller supplies a relative path within /music. */
bool hifi_tags_read(const char *relative_path, hifi_tags_t *out);
#endif
