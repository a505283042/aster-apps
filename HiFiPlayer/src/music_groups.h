/* SPDX-License-Identifier: MIT
 * Aster adaptation of FakePod_S31 media_groups_v2: ID3/Vorbis-derived
 * artist, album, decade grouping and per-scope virtual track IDs.
 */
#ifndef HIFI_MUSIC_GROUPS_H
#define HIFI_MUSIC_GROUPS_H
#include <stdbool.h>
#include <stdint.h>
#include "music_catalog.h"

enum { HIFI_GROUP_ARTISTS = 1, HIFI_GROUP_ALBUMS = 2,
       HIFI_GROUP_DECADES = 3, HIFI_GROUP_NONE = 0xffff };
typedef struct {
    char title[64];
    char artist[64];
    uint32_t songs;
    uint16_t decade;
    bool unknown_year;
} hifi_group_info_t;
void hifi_groups_start(void);
void hifi_groups_tick(bool playback_active);
bool hifi_groups_busy(void);
bool hifi_groups_ready(void);
uint32_t hifi_groups_processed(void);
uint32_t hifi_groups_total(void);
uint32_t hifi_groups_errors(void);
uint32_t hifi_groups_count(int mode);
bool hifi_groups_get(int mode, uint32_t group_index, hifi_group_info_t *out);
bool hifi_groups_select(int mode, uint32_t group_index);
uint32_t hifi_groups_selection_count(void);
bool hifi_groups_selection_track(uint32_t ordinal, uint32_t *catalog_id);
#endif
