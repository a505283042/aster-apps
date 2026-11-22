/* SPDX-License-Identifier: MIT
 * Memory-bounded tag grouping. MusicCatalog paths remain on SD, while
 * grouping uses 2-byte track-to-entity IDs and compact entity tables.
 * APP browse groups are NOT playback queues: all tracks use one catalog ID.
 *
 * An S31-style two-slot CRC snapshot restores already grouped metadata
 * without re-reading every MP3/FLAC tag on each app launch.
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

/* V1 cached group snapshot. All on-disk structures are versioned and
 * validated before publication. Never overwrite a previous generation's
 * completed slot while building a replacement. */
enum { GC_MAGIC = 0x31474348u, GC_VERSION = 1, GC_STEP_BYTES = 4096 };
enum gc_state { GC_PROBE, GC_READ, GC_WRITE, GC_VERIFY, GC_IDLE };
typedef struct {
    uint32_t magic, version, generation, track_count;
    uint32_t artist_count, album_count, decade_count, name_bytes;
    uint32_t payload_bytes, payload_crc, header_crc;
} gc_header_t;
typedef struct {
    uint8_t *p;
    uint32_t bytes;
} gc_span_t;
static const char *const gc_paths[2] = {
    "/data/hifi_groups_a.bin", "/data/hifi_groups_b.bin"
};
static gc_header_t gc_header;
static enum gc_state gc_mode;
static aster_handle_t gc_file;
static uint32_t gc_crc, gc_span_index, gc_span_offset, gc_active_slot;
static uint8_t gc_verify_buf[GC_STEP_BYTES];

static uint32_t gc_update(uint32_t crc, const uint8_t *data, uint32_t count)
{
    for (uint32_t i=0;i<count;i++) {
        crc ^= data[i];
        for (unsigned bit=0;bit<8;bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
static uint32_t gc_header_crc(gc_header_t *h)
{
    return ~gc_update(0xffffffffu, (const uint8_t *)h,
                      (uint32_t)(sizeof(*h) - sizeof(h->header_crc)));
}
static void gc_close(void)
{
    if (gc_file) aster_fs_close(gc_file);
    gc_file=0;
}
static bool gc_read(aster_handle_t file, uint8_t *dst, uint32_t bytes)
{
    uint32_t done=0;
    while (done<bytes) {
        int got=aster_fs_read(file,dst+done,bytes-done);
        if (got<=0)return false;
        done+=(uint32_t)got;
    }
    return true;
}
static bool gc_write(aster_handle_t file, const uint8_t *src, uint32_t bytes)
{
    uint32_t done=0;
    while (done<bytes) {
        int got=aster_fs_write(file,src+done,bytes-done);
        if (got<=0)return false;
        done+=(uint32_t)got;
    }
    return true;
}
static gc_span_t gc_span(uint32_t idx)
{
    switch(idx) {
    case 0: return (gc_span_t){(uint8_t *)artists,
                               artist_count*(uint32_t)sizeof(artist_row_t)};
    case 1: return (gc_span_t){(uint8_t *)albums,
                               album_count*(uint32_t)sizeof(album_row_t)};
    case 2: return (gc_span_t){(uint8_t *)decades,
                               decade_count*(uint32_t)sizeof(decade_row_t)};
    case 3: return (gc_span_t){(uint8_t *)track_artists,
                               expected_count*(uint32_t)sizeof(uint16_t)};
    case 4: return (gc_span_t){(uint8_t *)track_album_artists,
                               expected_count*(uint32_t)sizeof(uint16_t)};
    case 5: return (gc_span_t){(uint8_t *)track_albums,
                               expected_count*(uint32_t)sizeof(uint16_t)};
    case 6: return (gc_span_t){(uint8_t *)track_decades,
                               expected_count*(uint32_t)sizeof(uint16_t)};
    case 7: return (gc_span_t){(uint8_t *)name_pool,name_pool_used};
    default:return (gc_span_t){0};
    }
}
static uint32_t gc_bytes(void)
{
    uint32_t sum=0;
    for(uint32_t i=0;i<8;i++)sum+=gc_span(i).bytes;
    return sum;
}
static void gc_reset_groups(void)
{
    artist_count=album_count=decade_count=processed=errors=selected_count=0;
    name_pool_used=1;
    name_pool[0]=0;
    memset(artist_slots,0,sizeof(artist_slots));
    memset(album_slots,0,sizeof(album_slots));
    ready=blocked=false;
}
static void gc_discard(bool erase)
{
    gc_close();
    if (erase) {
        const char *path=gc_paths[gc_active_slot];
        (void)aster_fs_unlink(path,(uint32_t)strlen(path));
    }
    gc_mode=GC_IDLE;
}
static bool gc_validate_payload(void)
{
    if (!name_pool_used || name_pool[0] ||
        name_pool[name_pool_used-1] != 0) return false;
    for (uint32_t i=0;i<artist_count;i++) {
        if (artists[i].name_off>=name_pool_used) return false;
    }
    for (uint32_t i=0;i<album_count;i++) {
        if (albums[i].title_off>=name_pool_used ||
            albums[i].album_artist_id>=artist_count) return false;
    }
    for (uint32_t i=0;i<expected_count;i++) {
        if (track_artists[i]>=artist_count ||
            track_album_artists[i]>=artist_count ||
            track_albums[i]>=album_count ||
            track_decades[i]>=decade_count) return false;
    }
    return true;
}
static void gc_probe(void)
{
    gc_mode=GC_IDLE;
    /* Try the current generation's parity slot first. The older slot is
     * still a valid rollback when power is lost during snapshot writing. */
    for (unsigned attempt=0;attempt<2;attempt++) {
        uint32_t slot=((expected_generation&1u)^attempt);
        const char *path=gc_paths[slot];
        aster_stat_t stat={.size=sizeof(stat),.version=1};
        if (aster_fs_stat(path,(uint32_t)strlen(path),&stat,sizeof(stat))<0)
            continue;
        aster_handle_t file=0;
        if (aster_fs_open(path,(uint32_t)strlen(path),
                          ASTER_FS_O_RDONLY,&file)<0)continue;
        gc_header_t h={0};
        bool valid=gc_read(file,(uint8_t *)&h,(uint32_t)sizeof(h));
        if (valid) {
            valid=h.magic==GC_MAGIC && h.version==GC_VERSION &&
                  h.generation==expected_generation &&
                  h.track_count==expected_count &&
                  h.artist_count<=ART_MAX && h.album_count<=ALB_MAX &&
                  h.decade_count<=YEAR_MAX &&
                  h.name_bytes>0 && h.name_bytes<=NAME_POOL_BYTES &&
                  h.header_crc==gc_header_crc(&h) &&
                  stat.filesize==(uint64_t)sizeof(h)+h.payload_bytes;
        }
        if (valid) {
            artist_count=h.artist_count;
            album_count=h.album_count;
            decade_count=h.decade_count;
            name_pool_used=h.name_bytes;
            valid=(h.payload_bytes==gc_bytes());
        }
        if (!valid) {
            aster_fs_close(file);
            gc_reset_groups();
            continue;
        }
        gc_header=h;
        gc_file=file;
        gc_active_slot=slot;
        gc_crc=0xffffffffu;
        gc_span_index=gc_span_offset=0;
        gc_mode=GC_READ;
        return;
    }
}
static bool gc_step_data(bool writing)
{
    uint32_t budget=GC_STEP_BYTES;
    while(budget && gc_span_index<8u) {
        gc_span_t span=gc_span(gc_span_index);
        if (gc_span_offset>=span.bytes) {
            gc_span_index++;gc_span_offset=0;
            continue;
        }
        uint32_t n=span.bytes-gc_span_offset;
        if(n>budget)n=budget;
        uint8_t *cursor=span.p+gc_span_offset;
        bool ok=writing?gc_write(gc_file,cursor,n):
                        gc_read(gc_file,cursor,n);
        if(!ok)return false;
        gc_crc=gc_update(gc_crc,cursor,n);
        gc_span_offset+=n;
        budget-=n;
    }
    return true;
}
static void gc_restore_tick(void)
{
    if (!gc_step_data(false)) {
        aster_printf("HiFi groups cache read failure; parse original tags");
        gc_discard(false);
        gc_reset_groups();
        return;
    }
    if(gc_span_index<8u)return;
    bool valid=(~gc_crc)==gc_header.payload_crc && gc_validate_payload();
    gc_close();
    gc_mode=GC_IDLE;
    if (!valid) {
        aster_printf("HiFi groups cache CRC invalid; rebuild metadata");
        gc_reset_groups();
        return;
    }
    ready=true;
    processed=expected_count;
    aster_printf("HiFi groups restored cached=%u artists=%u albums=%u decades=%u",
                 processed,artist_count,album_count,decade_count);
}
static void gc_begin_save(void)
{
    gc_active_slot=expected_generation&1u;
    const char *path=gc_paths[gc_active_slot];
    gc_mode=GC_IDLE;
    gc_close();
    if(aster_fs_open(path,(uint32_t)strlen(path),
        ASTER_FS_O_RDWR|ASTER_FS_O_CREAT|ASTER_FS_O_TRUNC,&gc_file)<0)
        return;
    /* An invalid header precedes all payload records, so a partial power
     * loss cannot expose a half-written group snapshot as a valid cache. */
    gc_header_t blank={0};
    if(!gc_write(gc_file,(const uint8_t *)&blank,sizeof(blank))) {
        gc_discard(true);
        return;
    }
    gc_header=(gc_header_t){.magic=GC_MAGIC,.version=GC_VERSION,
        .generation=expected_generation,.track_count=expected_count,
        .artist_count=artist_count,.album_count=album_count,
        .decade_count=decade_count,.name_bytes=name_pool_used,
        .payload_bytes=gc_bytes()};
    gc_crc=0xffffffffu;
    gc_span_index=gc_span_offset=0;
    gc_mode=GC_WRITE;
}
static void gc_write_tick(void)
{
    if (!gc_step_data(true)) {
        aster_printf("HiFi groups cache write failure");
        gc_discard(true);
        return;
    }
    if (gc_span_index<8u)return;
    gc_header.payload_crc=~gc_crc;
    if (aster_fs_seek(gc_file,(int32_t)sizeof(gc_header),
                      ASTER_FS_SEEK_SET)<0) {
        gc_discard(true);
        return;
    }
    gc_crc=0xffffffffu;
    gc_span_index=gc_span_offset=0;
    gc_mode=GC_VERIFY;
}
static void gc_verify_tick(void)
{
    uint32_t budget=GC_STEP_BYTES;
    while(budget && gc_span_index<8u) {
        gc_span_t span=gc_span(gc_span_index);
        if(gc_span_offset>=span.bytes) {
            gc_span_index++;gc_span_offset=0;
            continue;
        }
        uint32_t n=span.bytes-gc_span_offset;
        if(n>budget)n=budget;
        if(!gc_read(gc_file,gc_verify_buf,n)) {
            gc_discard(true);
            return;
        }
        gc_crc=gc_update(gc_crc,gc_verify_buf,n);
        gc_span_offset+=n;
        budget-=n;
    }
    if(gc_span_index<8u)return;
    if((~gc_crc)!=gc_header.payload_crc) {
        gc_discard(true);
        return;
    }
    gc_header.header_crc=gc_header_crc(&gc_header);
    if (aster_fs_seek(gc_file,0,ASTER_FS_SEEK_SET)<0 ||
        !gc_write(gc_file,(const uint8_t *)&gc_header,sizeof(gc_header))) {
        gc_discard(true);
        return;
    }
    gc_close();
    gc_mode=GC_IDLE;
    aster_printf("HiFi groups cache committed tracks=%u gen=%u",
                 expected_count,expected_generation);
}
void hifi_groups_invalidate_cache(void)
{
    gc_close();
    for (unsigned i=0;i<2;i++)
        (void)aster_fs_unlink(gc_paths[i],(uint32_t)strlen(gc_paths[i]));
    gc_mode=GC_IDLE;
}

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
    gc_close();
    expected_count=s.count;
    expected_generation=s.generation;
    last_tick=0;
    gc_reset_groups();
    gc_mode=GC_PROBE;
}
void hifi_groups_tick(bool playback_active)
{
    hifi_catalog_status_t s=hifi_catalog_status();
    if (s.scanning) return; /* Only classify a complete, CRC-verified catalog. */
    if (s.generation!=expected_generation || s.count!=expected_count)
        hifi_groups_start();
    /* Never write large metadata snapshots while music is playing:
     * defer disk cache save/verify until idle, avoiding new audio glitches. */
    if (gc_mode==GC_PROBE) {gc_probe();return;}
    if (gc_mode==GC_READ) {gc_restore_tick();return;}
    if (gc_mode==GC_WRITE) {if(!playback_active)gc_write_tick();return;}
    if (gc_mode==GC_VERIFY) {if(!playback_active)gc_verify_tick();return;}
    if (ready||blocked)return;
    if (processed>=expected_count) {
        ready=true;
        aster_printf("HiFi tags grouped artists=%u albums=%u decades=%u files=%u errors=%u",
                     artist_count,album_count,decade_count,processed,errors);
        if (!playback_active) gc_begin_save();
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
