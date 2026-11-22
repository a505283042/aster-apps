/* SPDX-License-Identifier: MIT
 * Memory-bounded tag grouping. MusicCatalog paths remain on SD, while
 * grouping uses 2-byte track-to-entity IDs and compact entity tables.
 *
 * Unlike FakePod_S31's full metadata V2 Catalog, this alpha snapshot is
 * rebuilt in RAM on each app launch. No false group labels are fabricated
 * from folder names. Proper metadata persistence comes in a later stage.
 */
#include "music_groups.h"
#include "music_tags.h"
#include <aster_imports.h>

enum { ART_MAX = 3072, ALB_MAX = 4096, YEAR_MAX = 32, SLOT_COUNT = 8192,
       NAME_POOL_BYTES = 512 * 1024,
       META_PERIOD_IDLE_MS = 26, META_PERIOD_PLAY_MS = 240 };
/* S31-style string interning: avoid allocating 128 bytes per album row.
 * A zero offset is the empty string. Store album artist by artist ID so
 * multi-track albums do not duplicate the same name hundreds of times. */
typedef struct {
    uint32_t name_off, hash, count;
} artist_row_t;
typedef struct {
    uint32_t title_off, hash, count;
    uint16_t album_artist_id, reserved;
} album_row_t;
typedef struct {
    uint16_t decade;
    uint16_t reserved;
    uint32_t count;
} decade_row_t;

static char name_pool[NAME_POOL_BYTES];
static uint32_t name_pool_used;
static artist_row_t artists[ART_MAX];
static album_row_t albums[ALB_MAX];
static decade_row_t decades[YEAR_MAX];
static uint16_t artist_slots[SLOT_COUNT], album_slots[SLOT_COUNT];
static uint16_t track_artists[HIFI_CATALOG_TRACK_MAX];
static uint16_t track_album_artists[HIFI_CATALOG_TRACK_MAX];
static uint16_t track_albums[HIFI_CATALOG_TRACK_MAX];
static uint16_t track_decades[HIFI_CATALOG_TRACK_MAX];
static uint16_t selected_tracks[HIFI_CATALOG_TRACK_MAX];
static uint32_t artist_count, album_count, decade_count;
static uint32_t processed, expected_count, expected_generation, errors;
static uint32_t selected_count, last_tick;
static bool ready, blocked;

static uint32_t hash_string(uint32_t hash, const char *text)
{
    while (*text) hash = (hash ^ (uint8_t)*text++) * 16777619u;
    return hash;
}
static uint32_t store_name(const char *name)
{
    unsigned count=(unsigned)strlen(name)+1u;
    if (count>NAME_POOL_BYTES-name_pool_used) return UINT32_MAX;
    uint32_t offset=name_pool_used;
    memcpy(name_pool+offset,name,count);
    name_pool_used+=count;
    return offset;
}
static const char *pool_name(uint32_t off)
{
    return off<name_pool_used ? name_pool+off : "";
}
static uint16_t group_artist(const char *name)
{
    uint32_t hash=hash_string(2166136261u,name);
    uint32_t slot=hash&(SLOT_COUNT-1u);
    for (uint32_t i=0;i<SLOT_COUNT;i++) {
        uint16_t val=artist_slots[slot];
        if (!val) {
            if (artist_count>=ART_MAX) return HIFI_GROUP_NONE;
            uint32_t off=store_name(name);
            if (off==UINT32_MAX) return HIFI_GROUP_NONE;
            artist_row_t *row=&artists[artist_count];
            row->name_off=off;row->hash=hash;row->count=0;
            artist_slots[slot]=(uint16_t)(++artist_count);
            return (uint16_t)(artist_count-1);
        }
        artist_row_t *row=&artists[val-1u];
        if (row->hash==hash && !strcmp(pool_name(row->name_off),name))
            return (uint16_t)(val-1u);
        slot=(slot+1u)&(SLOT_COUNT-1u);
    }
    return HIFI_GROUP_NONE;
}
static uint16_t group_album(const char *title, uint16_t album_artist_id)
{
    if (album_artist_id==HIFI_GROUP_NONE || album_artist_id>=artist_count)
        return HIFI_GROUP_NONE;
    const char *album_artist=pool_name(artists[album_artist_id].name_off);
    uint32_t hash=hash_string(2166136261u,title);
    hash=hash_string(hash,"/");
    hash=hash_string(hash,album_artist);
    uint32_t slot=hash&(SLOT_COUNT-1u);
    for (uint32_t i=0;i<SLOT_COUNT;i++) {
        uint16_t val=album_slots[slot];
        if (!val) {
            if (album_count>=ALB_MAX) return HIFI_GROUP_NONE;
            uint32_t off=store_name(title);
            if (off==UINT32_MAX) return HIFI_GROUP_NONE;
            album_row_t *row=&albums[album_count];
            row->title_off=off;row->hash=hash;row->count=0;
            row->album_artist_id=album_artist_id;
            album_slots[slot]=(uint16_t)(++album_count);
            return (uint16_t)(album_count-1);
        }
        album_row_t *row=&albums[val-1u];
        if (row->hash==hash && row->album_artist_id==album_artist_id &&
            !strcmp(pool_name(row->title_off),title))
            return (uint16_t)(val-1u);
        slot=(slot+1u)&(SLOT_COUNT-1u);
    }
    return HIFI_GROUP_NONE;
}
static uint16_t group_decade(uint16_t year)
{
    uint16_t decade = year >= 1000u && year <= 9999u ?
                      (uint16_t)(year/10u*10u) : 0;
    for (uint32_t i=0;i<decade_count;i++)
        if (decades[i].decade==decade)return (uint16_t)i;
    if (decade_count>=YEAR_MAX)return HIFI_GROUP_NONE;
    decades[decade_count]=(decade_row_t){.decade=decade,.count=0};
    return (uint16_t)decade_count++;
}

void hifi_groups_start(void)
{
    hifi_catalog_status_t s=hifi_catalog_status();
    artist_count=album_count=decade_count=processed=errors=selected_count=0;
    name_pool_used=1;
    name_pool[0]=0;
    expected_count=s.count;
    expected_generation=s.generation;
    last_tick=0;
    ready=blocked=false;
    memset(artist_slots,0,sizeof(artist_slots));
    memset(album_slots,0,sizeof(album_slots));
}
void hifi_groups_tick(bool playback_active)
{
    hifi_catalog_status_t s=hifi_catalog_status();
    if (s.scanning) return; /* Only classify a complete, CRC-verified catalog. */
    if (s.generation!=expected_generation || s.count!=expected_count)
        hifi_groups_start();
    if (ready||blocked)return;
    if (processed>=expected_count) {
        ready=true;
        aster_printf("HiFi tags grouped artists=%u albums=%u decades=%u files=%u errors=%u",
                     artist_count,album_count,decade_count,processed,errors);
        return;
    }
    uint32_t now=aster_time_now_ms();
    uint32_t period=playback_active?META_PERIOD_PLAY_MS:META_PERIOD_IDLE_MS;
    if (last_tick && (uint32_t)(now-last_tick)<period) return;
    last_tick=now;
    char relative[HIFI_CATALOG_PATH_MAX];
    uint32_t idx=processed;
    if (!hifi_catalog_path_at(idx,relative,sizeof(relative))) {
        blocked=true;
        aster_printf("HiFi tags catalog row unavailable=%u",idx);
        return;
    }
    hifi_tags_t tags={0};
    if(!hifi_tags_read(relative,&tags))errors++;
    const char *artist=tags.artist[0]?tags.artist:"未知歌手";
    const char *album=tags.album[0]?tags.album:"未知专辑";
    const char *album_artist=tags.album_artist[0]?tags.album_artist:artist;
    uint16_t a=group_artist(artist);
    uint16_t album_a=group_artist(album_artist);
    uint16_t b=group_album(album,album_a);
    uint16_t year=tags.original_year?tags.original_year:tags.year;
    uint16_t d=group_decade(year);
    if (a==HIFI_GROUP_NONE||album_a==HIFI_GROUP_NONE||
        b==HIFI_GROUP_NONE||d==HIFI_GROUP_NONE) {
        blocked=true;
        aster_printf("HiFi tags group capacity at %u artists=%u albums=%u",
                     idx,artist_count,album_count);
        return;
    }
    track_artists[idx]=a;track_album_artists[idx]=album_a;
    track_albums[idx]=b;track_decades[idx]=d;
    artists[a].count++;
    if (a!=album_a) artists[album_a].count++;
    albums[b].count++;decades[d].count++;
    processed++;
    if ((processed%256u)==0u)
        aster_printf("HiFi tags progress=%u/%u",processed,expected_count);
}
bool hifi_groups_busy(void) {return !ready && !blocked;}
bool hifi_groups_ready(void) {return ready;}
uint32_t hifi_groups_processed(void) {return processed;}
uint32_t hifi_groups_total(void) {return expected_count;}
uint32_t hifi_groups_errors(void) {return errors;}
uint32_t hifi_groups_count(int mode)
{
    if (!ready)return 0;
    if (mode==HIFI_GROUP_ARTISTS)return artist_count;
    if (mode==HIFI_GROUP_ALBUMS)return album_count;
    if (mode==HIFI_GROUP_DECADES)return decade_count;
    return 0;
}
bool hifi_groups_get(int mode,uint32_t group_index,hifi_group_info_t *out)
{
    if(!out || !ready)return false;
    *out=(hifi_group_info_t){0};
    if(mode==HIFI_GROUP_ARTISTS && group_index<artist_count){
        aster_snprintf(out->title,sizeof(out->title),"%s",
                       pool_name(artists[group_index].name_off));
        out->songs=artists[group_index].count;
    } else if(mode==HIFI_GROUP_ALBUMS && group_index<album_count) {
        aster_snprintf(out->title,sizeof(out->title),"%s",
                       pool_name(albums[group_index].title_off));
        aster_snprintf(out->artist,sizeof(out->artist),"%s",
                       pool_name(artists[albums[group_index].album_artist_id].name_off));
        out->songs=albums[group_index].count;
    } else if(mode==HIFI_GROUP_DECADES && group_index<decade_count){
        out->decade=decades[group_index].decade;
        out->unknown_year=!out->decade;
        if(out->unknown_year)
            aster_snprintf(out->title,sizeof(out->title),"未知年代");
        else
            aster_snprintf(out->title,sizeof(out->title),"%u 年代",out->decade);
        out->songs=decades[group_index].count;
    } else return false;
    return true;
}
bool hifi_groups_select(int mode,uint32_t group_index)
{
    if(!ready || group_index>=hifi_groups_count(mode)) return false;
    uint16_t match=(uint16_t)group_index;
    selected_count=0;
    for(uint32_t i=0;i<processed;i++){
        uint16_t key=mode==HIFI_GROUP_ARTISTS?track_artists[i]:
                     mode==HIFI_GROUP_ALBUMS?track_albums[i]:track_decades[i];
        if (key==match ||
            (mode==HIFI_GROUP_ARTISTS && track_album_artists[i]==match))
            selected_tracks[selected_count++]=(uint16_t)i;
    }
    return true;
}
uint32_t hifi_groups_selection_count(void){return selected_count;}
bool hifi_groups_selection_track(uint32_t ordinal,uint32_t *catalog_id)
{
    if(!catalog_id || ordinal>=selected_count)return false;
    *catalog_id=selected_tracks[ordinal];
    return true;
}
