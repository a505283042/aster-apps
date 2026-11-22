/* SPDX-License-Identifier: MIT
 * HIFI Folder Index V1 (HFD1): persisted, generation-bound directory tree.
 *
 * The HIF2 Catalog remains the one and only source of track IDs. A single
 * background pass derives folder nodes and contiguous child-reference lists,
 * then writes a two-slot CRC snapshot. Opening a directory only accesses
 * in-memory node and reference arrays: no full-catalog SD traversal.
 *
 * No separate queues are created for artist, album, decade or folders.
 */
#include "music_browser.h"
#include <aster_imports.h>

enum {
    FIDX_MAGIC=0x31444648u, FIDX_VERSION=1,
    FIDX_MAX_FOLDERS=1024, FIDX_HASH_SLOTS=4096,
    FIDX_MAX_REFS=HIFI_CATALOG_TRACK_MAX+FIDX_MAX_FOLDERS-1,
    FIDX_STEP=16, FIDX_CHUNK=4096, FIDX_BUDGET_MS=6
};
#define FIDX_FOLDER_FLAG 0x80000000u

typedef struct {
    uint32_t magic, version, generation, catalog_count;
    uint32_t folder_count, ref_count, payload_crc, header_crc;
} fidx_header_t;
typedef struct {
    uint32_t parent, first, count, songs;
    char name[HIFI_CATALOG_PATH_MAX];
} folder_node_t;
_Static_assert(sizeof(fidx_header_t)==32, "folder header layout");
_Static_assert(sizeof(folder_node_t)==168, "folder node layout");

enum fidx_phase {
    FIDX_WAIT, FIDX_LOAD, FIDX_BUILD, FIDX_WRITE, FIDX_VERIFY_WRITE,
    FIDX_READY, FIDX_MEMORY_ONLY, FIDX_FAILED
};
static enum fidx_phase phase;
static const char *const index_paths[2]={
    "/data/hifi_folders_a.bin", "/data/hifi_folders_b.bin"
};
/* Approx. 274 KB of static linear memory, rather than a fresh file scan per
 * page. Directory browsing after loading this snapshot does not read SD. */
static folder_node_t nodes[FIDX_MAX_FOLDERS];
static uint32_t references[FIDX_MAX_REFS];
static uint16_t song_parent[HIFI_CATALOG_TRACK_MAX];
static uint16_t hash_slots[FIDX_HASH_SLOTS];
static uint32_t write_offsets[FIDX_MAX_FOLDERS];
static uint8_t verify_buffer[FIDX_CHUNK];

static uint32_t node_count, ref_count, build_cursor, current_id;
static uint32_t expected_generation, expected_count;
static uint32_t transfer_at, transfer_crc;
static uint32_t read_slot, write_slot;
static fidx_header_t pending;
static aster_handle_t io_handle;
static char folder_path[HIFI_CATALOG_PATH_MAX];
static bool failed;

static uint32_t crc_step(uint32_t crc,const void *data,uint32_t size)
{
    const uint8_t *src=data;
    for(uint32_t i=0;i<size;i++){
        crc^=src[i];
        for(unsigned j=0;j<8;j++)
            crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return crc;
}
static uint32_t header_crc(const fidx_header_t *h)
{
    return ~crc_step(0xffffffffu,h,sizeof(*h)-sizeof(h->header_crc));
}
static bool read_all(aster_handle_t file,void *ptr,uint32_t size)
{
    uint8_t *dst=ptr;
    uint32_t done=0;
    while(done<size){
        int got=aster_fs_read(file,dst+done,size-done);
        if(got<=0)return false;
        done+=(uint32_t)got;
    }
    return true;
}
static bool write_all(aster_handle_t file,const void *ptr,uint32_t size)
{
    const uint8_t *src=ptr;
    uint32_t done=0;
    while(done<size){
        int wrote=aster_fs_write(file,src+done,size-done);
        if(wrote<=0)return false;
        done+=(uint32_t)wrote;
    }
    return true;
}
static void close_io(void)
{
    if(io_handle)aster_fs_close(io_handle);
    io_handle=0;
}
static uint32_t payload_size(uint32_t folders,uint32_t refs)
{
    return folders*(uint32_t)sizeof(folder_node_t)+refs*(uint32_t)sizeof(uint32_t);
}
static void *payload_at(uint32_t offset,uint32_t *span)
{
    uint32_t node_bytes=node_count*(uint32_t)sizeof(folder_node_t);
    if(offset<node_bytes){
        *span=node_bytes-offset;
        return ((uint8_t *)nodes)+offset;
    }
    offset-=node_bytes;
    *span=ref_count*(uint32_t)sizeof(uint32_t)-offset;
    return ((uint8_t *)references)+offset;
}
static bool ready(void)
{
    return phase==FIDX_READY || phase==FIDX_MEMORY_ONLY ||
           phase==FIDX_WRITE || phase==FIDX_VERIFY_WRITE;
}
static void clear_tree(void)
{
    current_id=0;
    folder_path[0]=0;
    node_count=0;
    ref_count=0;
}
static void start_build(void)
{
    close_io();
    clear_tree();
    memset(hash_slots,0,sizeof(hash_slots));
    memset(nodes,0,sizeof(folder_node_t));
    node_count=1;                   /* Directory 0: /music root. */
    build_cursor=0;
    phase=FIDX_BUILD;
    failed=false;
    aster_printf("HiFi folder index build catalog=%u generation=%u",
                 expected_count,expected_generation);
}
static bool open_snapshot(void)
{
    aster_stat_t stat={.size=sizeof(stat),.version=1};
    fidx_header_t headers[2]={{0},{0}};
    bool valid[2]={false,false};
    /* Both snapshots are bound to exactly one committed HIF2 generation. */
    for(unsigned slot=0;slot<2;slot++){
        if(aster_fs_stat(index_paths[slot],strlen(index_paths[slot]),
                         &stat,sizeof(stat))<0)
            continue;
        aster_handle_t f=0;
        if(aster_fs_open(index_paths[slot],strlen(index_paths[slot]),
                         ASTER_FS_O_RDONLY,&f)<0)continue;
        fidx_header_t h={0};
        bool ok=read_all(f,&h,sizeof(h));
        aster_fs_close(f);
        if(!ok || h.magic!=FIDX_MAGIC || h.version!=FIDX_VERSION ||
           h.catalog_count!=expected_count ||
           h.generation!=expected_generation ||
           !h.folder_count || h.folder_count>FIDX_MAX_FOLDERS ||
           h.ref_count!=h.catalog_count+h.folder_count-1 ||
           h.ref_count>FIDX_MAX_REFS || h.header_crc!=header_crc(&h) ||
           stat.filesize!=(uint64_t)sizeof(h)+
                          payload_size(h.folder_count,h.ref_count))
            continue;
        headers[slot]=h;
        valid[slot]=true;
    }
    unsigned slot=valid[1] && !valid[0]?1u:0u;
    if(!valid[slot])return false;
    if(aster_fs_open(index_paths[slot],strlen(index_paths[slot]),
                     ASTER_FS_O_RDONLY,&io_handle)<0)return false;
    pending=headers[slot];
    node_count=pending.folder_count;
    ref_count=pending.ref_count;
    read_slot=slot;
    transfer_at=0;
    transfer_crc=0xffffffffu;
    if(aster_fs_seek(io_handle,(int32_t)sizeof(pending),ASTER_FS_SEEK_SET)<0){
        close_io();return false;
    }
    phase=FIDX_LOAD;
    return true;
}
void hifi_browser_close(void)
{
    close_io();
    phase=FIDX_WAIT;
    clear_tree();
}
void hifi_browser_start(void)
{
    close_io();
    clear_tree();
    failed=false;
    hifi_catalog_status_t catalog=hifi_catalog_status();
    expected_generation=catalog.generation;
    expected_count=catalog.count;
    phase=FIDX_WAIT;
    if(!catalog.has_saved)return;
    if(!open_snapshot())start_build();
}
static uint32_t folder_hash(uint32_t parent,const char *name,unsigned length)
{
    uint32_t h=2166136261u ^ parent;
    for(unsigned i=0;i<length;i++)
        h=(h^(uint8_t)name[i])*16777619u;
    return h;
}
static int ensure_child(uint32_t parent,const char *name,unsigned length)
{
    if(!length || length>=HIFI_CATALOG_PATH_MAX)return -1;
    uint32_t slot=folder_hash(parent,name,length)&(FIDX_HASH_SLOTS-1u);
    for(unsigned probe=0;probe<FIDX_HASH_SLOTS;probe++){
        uint16_t cached=hash_slots[slot];
        if(!cached){
            if(node_count>=FIDX_MAX_FOLDERS)return -1;
            uint32_t id=node_count++;
            folder_node_t *n=&nodes[id];
            memset(n,0,sizeof(*n));
            n->parent=parent;
            memcpy(n->name,name,length);
            n->name[length]=0;
            nodes[parent].count++;
            hash_slots[slot]=(uint16_t)(id+1);
            return (int)id;
        }
        uint32_t id=(uint32_t)cached-1u;
        if(nodes[id].parent==parent && strlen(nodes[id].name)==length &&
           !strncmp(nodes[id].name,name,length))
            return (int)id;
        slot=(slot+1u)&(FIDX_HASH_SLOTS-1u);
    }
    return -1;
}
static bool process_song(uint32_t song_id,const char *name)
{
    if(!name || !*name || name[0]=='/' || name[0]=='\\')return false;
    uint32_t dir=0;
    const char *p=name;
    for(const char *at=name;*at;at++){
        if(*at!='/')continue;
        int child=ensure_child(dir,p,(unsigned)(at-p));
        if(child<0)return false;
        dir=(uint32_t)child;
        p=at+1;
    }
    if(!*p || song_id>=HIFI_CATALOG_TRACK_MAX)return false;
    song_parent[song_id]=(uint16_t)dir;
    nodes[dir].count++;        /* One immediate file entry in this folder. */
    for(uint32_t at=dir;;at=nodes[at].parent){
        nodes[at].songs++;
        if(!at)break;
    }
    return true;
}
static bool finish_tree(void)
{
    uint32_t running=0;
    for(uint32_t id=0;id<node_count;id++){
        nodes[id].first=running;
        write_offsets[id]=running;
        running+=nodes[id].count;
        if(running>FIDX_MAX_REFS)return false;
    }
    if(running!=expected_count+node_count-1u)return false;
    ref_count=running;
    for(uint32_t id=1;id<node_count;id++){
        uint32_t parent=nodes[id].parent;
        references[write_offsets[parent]++]=FIDX_FOLDER_FLAG|id;
    }
    for(uint32_t id=0;id<expected_count;id++){
        uint32_t parent=song_parent[id];
        if(parent>=node_count)return false;
        references[write_offsets[parent]++]=id;
    }
    for(uint32_t id=0;id<node_count;id++)
        if(write_offsets[id]!=nodes[id].first+nodes[id].count)
            return false;
    return true;
}
static bool path_terminated(const char *value,unsigned capacity)
{
    for(unsigned i=0;i<capacity;i++)
        if(!value[i])return true;
    return false;
}
static bool validate_tree(void)
{
    if(!node_count || nodes[0].parent!=0 || nodes[0].name[0] ||
       nodes[0].songs!=expected_count)return false;
    uint32_t running=0;
    for(uint32_t id=0;id<node_count;id++){
        const folder_node_t *n=&nodes[id];
        if(id && (n->parent>=id || !path_terminated(n->name,sizeof(n->name))))
            return false;
        if(n->first!=running || n->count>ref_count-running)return false;
        running+=n->count;
    }
    if(running!=ref_count)return false;
    for(uint32_t i=0;i<ref_count;i++){
        uint32_t ref=references[i];
        if(ref&FIDX_FOLDER_FLAG){
            if((ref&~FIDX_FOLDER_FLAG)==0 ||
               (ref&~FIDX_FOLDER_FLAG)>=node_count)return false;
        }else if(ref>=expected_count)return false;
    }
    return true;
}
static void memory_only(bool error)
{
    close_io();
    failed=error;
    phase=node_count && validate_tree() ? FIDX_MEMORY_ONLY : FIDX_FAILED;
}
static void begin_write(void)
{
    /* Write only the inactive slot. Header stays invalid until full read-back. */
    write_slot=read_slot==0u?1u:0u;
    close_io();
    const char *path=index_paths[write_slot];
    if(aster_fs_open(path,strlen(path),ASTER_FS_O_RDWR |
                     ASTER_FS_O_CREAT | ASTER_FS_O_TRUNC,&io_handle)<0){
        memory_only(true);
        return;
    }
    fidx_header_t blank={0};
    if(!write_all(io_handle,&blank,sizeof(blank))){
        memory_only(true);
        return;
    }
    pending=(fidx_header_t){.magic=FIDX_MAGIC,.version=FIDX_VERSION,
          .generation=expected_generation,.catalog_count=expected_count,
          .folder_count=node_count,.ref_count=ref_count};
    transfer_at=0;
    transfer_crc=0xffffffffu;
    phase=FIDX_WRITE;
}
static void transfer_tick(bool writing)
{
    uint32_t total=payload_size(node_count,ref_count);
    uint32_t offset=transfer_at, span=0;
    if(offset<total){
        void *src=payload_at(offset,&span);
        uint32_t chunk=span<FIDX_CHUNK?span:FIDX_CHUNK;
        bool ok;
        if(writing){
            ok=write_all(io_handle,src,chunk);
            if(ok)transfer_crc=crc_step(transfer_crc,src,chunk);
        }else{
            ok=read_all(io_handle,verify_buffer,chunk);
            if(ok)transfer_crc=crc_step(transfer_crc,verify_buffer,chunk);
        }
        if(!ok){memory_only(true);return;}
        transfer_at+=chunk;
    }
    if(transfer_at!=total)return;
    if(writing){
        pending.payload_crc=~transfer_crc;
        if(aster_fs_seek(io_handle,(int32_t)sizeof(pending),ASTER_FS_SEEK_SET)<0){
            memory_only(true);return;
        }
        transfer_at=0;
        transfer_crc=0xffffffffu;
        phase=FIDX_VERIFY_WRITE;
    }else{
        if((~transfer_crc)!=pending.payload_crc ||
           aster_fs_seek(io_handle,0,ASTER_FS_SEEK_SET)<0){
            memory_only(true);return;
        }
        pending.header_crc=header_crc(&pending);
        if(!write_all(io_handle,&pending,sizeof(pending))){
            memory_only(true);return;
        }
        close_io();
        read_slot=write_slot;
        phase=FIDX_READY;
        aster_printf("HiFi folder index committed dirs=%u tracks=%u generation=%u",
                     node_count,expected_count,expected_generation);
    }
}
static void load_tick(void)
{
    uint32_t total=payload_size(node_count,ref_count);
    if(transfer_at<total){
        uint32_t span=0;
        void *dst=payload_at(transfer_at,&span);
        uint32_t chunk=span<FIDX_CHUNK?span:FIDX_CHUNK;
        if(!read_all(io_handle,dst,chunk)){
            start_build();return;
        }
        transfer_crc=crc_step(transfer_crc,dst,chunk);
        transfer_at+=chunk;
    }
    if(transfer_at!=total)return;
    close_io();
    if((~transfer_crc)!=pending.payload_crc || !validate_tree()){
        aster_printf("HiFi folder snapshot damaged: rebuilding from HIF2");
        start_build();
        return;
    }
    phase=FIDX_READY;
    aster_printf("HiFi folder index restored dirs=%u tracks=%u",
                 node_count,expected_count);
}
void hifi_browser_tick(void)
{
    hifi_catalog_status_t c=hifi_catalog_status();
    if(!c.has_saved){
        if(phase!=FIDX_WAIT){
            close_io();clear_tree();phase=FIDX_WAIT;
        }
        return;
    }
    if(phase==FIDX_WAIT || expected_generation!=c.generation ||
       expected_count!=c.count){
        hifi_browser_start();
        return;
    }
    if(phase==FIDX_LOAD){load_tick();return;}
    if(phase==FIDX_WRITE || phase==FIDX_VERIFY_WRITE){
        transfer_tick(phase==FIDX_WRITE);return;
    }
    if(phase!=FIDX_BUILD)return;
    uint32_t started=aster_time_now_ms();
    for(unsigned n=0;n<FIDX_STEP && build_cursor<expected_count;n++){
        if(aster_time_now_ms()-started>=FIDX_BUDGET_MS)break;
        char path[HIFI_CATALOG_PATH_MAX];
        if(!hifi_catalog_path_at(build_cursor,path,sizeof(path)) ||
           !process_song(build_cursor,path)){
            aster_printf("HiFi folder index invalid path at %u",build_cursor);
            close_io();phase=FIDX_FAILED;failed=true;return;
        }
        build_cursor++;
    }
    if(build_cursor==expected_count){
        if(!finish_tree() || !validate_tree()){
            phase=FIDX_FAILED;failed=true;return;
        }
        begin_write();
    }
}
void hifi_browser_open(const char *relative_folder)
{
    if(!relative_folder || !*relative_folder){
        current_id=0;
        folder_path[0]=0;
        return;
    }
    /* Legacy path entry API: direct lookup over cached child references,
     * never a global walk. Normal navigation uses enter_folder(id). */
    if(!ready())return;
    hifi_browser_open("");
    const char *p=relative_folder;
    while(*p){
        const char *slash=strchr(p,'/');
        unsigned len=slash?(unsigned)(slash-p):(unsigned)strlen(p);
        bool found=false;
        folder_node_t parent=nodes[current_id];
        for(uint32_t i=0;i<parent.count;i++){
            uint32_t ref=references[parent.first+i];
            if(!(ref&FIDX_FOLDER_FLAG))continue;
            uint32_t id=ref&~FIDX_FOLDER_FLAG;
            if(strlen(nodes[id].name)==len &&
               !strncmp(nodes[id].name,p,len)){
                if(!hifi_browser_enter_folder(id))return;
                found=true;break;
            }
        }
        if(!found)break;
        if(!slash)break;
        p=slash+1;
    }
}
bool hifi_browser_enter_folder(uint32_t id)
{
    if(!ready() || !id || id>=node_count ||
       nodes[id].parent!=current_id)return false;
    const char *name=nodes[id].name;
    uint32_t length=(uint32_t)strlen(folder_path);
    uint32_t next=(uint32_t)strlen(name);
    if(length+next+(length?1u:0u)>=sizeof(folder_path))return false;
    if(length)folder_path[length++]='/';
    memcpy(folder_path+length,name,next+1u);
    current_id=id;
    return true;
}
void hifi_browser_up(void)
{
    if(!ready() || !current_id)return;
    current_id=nodes[current_id].parent;
    uint32_t len=(uint32_t)strlen(folder_path);
    while(len && folder_path[len-1]!='/')len--;
    if(len)len--;
    folder_path[len]=0;
}
const char *hifi_browser_folder(void){return folder_path;}
uint32_t hifi_browser_count(void){return ready()?nodes[current_id].count:0;}
bool hifi_browser_busy(void){return !ready() && phase!=FIDX_FAILED;}
bool hifi_browser_error(void){return phase==FIDX_FAILED;}
bool hifi_browser_ready(void){return ready();}
hifi_browser_progress_t hifi_browser_progress(void)
{
    hifi_browser_progress_t p={0};
    p.ready=ready();
    p.error=phase==FIDX_FAILED;
    switch(phase){
    case FIDX_WAIT:
        p.stage=HIFI_BROWSER_WAIT;break;
    case FIDX_LOAD:
        p.stage=HIFI_BROWSER_LOADING;
        p.done=transfer_at;
        p.total=payload_size(node_count,ref_count);
        break;
    case FIDX_BUILD:
        p.stage=HIFI_BROWSER_BUILDING;
        p.done=build_cursor;
        p.total=expected_count;
        break;
    case FIDX_WRITE: case FIDX_VERIFY_WRITE:
        p.stage=HIFI_BROWSER_SAVING;
        p.done=transfer_at;
        p.total=payload_size(node_count,ref_count);
        break;
    default:p.stage=ready()?HIFI_BROWSER_READY:HIFI_BROWSER_ERROR;break;
    }
    p.folders=node_count;
    return p;
}
bool hifi_browser_entry(uint32_t row,hifi_browser_entry_t *out)
{
    if(!out || !ready() || current_id>=node_count ||
       row>=nodes[current_id].count)return false;
    *out=(hifi_browser_entry_t){0};
    uint32_t ref=references[nodes[current_id].first+row];
    if(ref&FIDX_FOLDER_FLAG){
        uint32_t id=ref&~FIDX_FOLDER_FLAG;
        if(!id || id>=node_count)return false;
        out->folder=true;
        out->folder_id=id;
        out->songs=nodes[id].songs;
        aster_snprintf(out->name,sizeof(out->name),"%s",nodes[id].name);
        return true;
    }
    out->track_index=ref;
    char path[HIFI_CATALOG_PATH_MAX];
    if(!hifi_catalog_path_at(ref,path,sizeof(path)))return false;
    const char *filename=path;
    for(const char *p=path;*p;p++)if(*p=='/')filename=p+1;
    aster_snprintf(out->name,sizeof(out->name),"%s",filename);
    return true;
}
