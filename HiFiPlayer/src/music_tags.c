/* SPDX-License-Identifier: MIT
 * MP3 ID3v2.2/2.3/2.4 (plus ID3v1 fallback) and FLAC Vorbis Comments.
 * Based on the tag choices/precedence in FakePod_S31 media_metadata.cpp.
 *
 * This parser intentionally skips APIC/PICTURE and lyric payloads by offset,
 * never copies full tags or large embedded artwork into WASM memory.
 * Supports UTF-8, Latin-1 and UTF-16 text up to 63 UTF-8 bytes per field.
 */
#include "music_tags.h"
#include <aster_imports.h>

enum { TAG_READ_CAP = 512, TAG_FRAME_LIMIT = 256, TAG_COMMENT_LIMIT = 256 };
static uint8_t scratch[TAG_READ_CAP];

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | p[0];
}
static uint32_t sync32(const uint8_t *p)
{
    return ((uint32_t)(p[0]&127) << 21) | ((uint32_t)(p[1]&127) << 14) |
           ((uint32_t)(p[2]&127) << 7) | (p[3]&127);
}

static bool read_at(aster_handle_t handle, uint32_t offset, void *ptr, uint32_t size)
{
    if (offset > INT32_MAX || aster_fs_seek(handle, (int32_t)offset,
                                             ASTER_FS_SEEK_SET) < 0) return false;
    uint32_t have = 0;
    while (have < size) {
        int n = aster_fs_read(handle, (uint8_t *)ptr + have, size - have);
        if (n <= 0) return false;
        have += (uint32_t)n;
    }
    return true;
}

static unsigned emit_cp(char *dst, unsigned used, unsigned cap, uint32_t cp)
{
    if (!cp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
        return used;
    unsigned needed = cp < 0x80u ? 1u : cp < 0x800u ? 2u :
                      cp < 0x10000u ? 3u : 4u;
    if (needed >= cap - used) return used;
    if (needed == 1) dst[used++] = (char)cp;
    else {
        if (needed == 2) dst[used++] = (char)(0xc0u | (cp >> 6));
        else if (needed == 3) dst[used++] = (char)(0xe0u | (cp >> 12));
        else dst[used++] = (char)(0xf0u | (cp >> 18));
        if (needed == 4) dst[used++] = (char)(0x80u | ((cp >> 12) & 63u));
        if (needed >= 3) dst[used++] = (char)(0x80u | ((cp >> 6) & 63u));
        dst[used++] = (char)(0x80u | (cp & 63u));
    }
    dst[used] = 0;
    return used;
}
static void copy_utf8(char *dst, unsigned cap, const uint8_t *src, uint32_t n)
{
    unsigned used = 0;
    for (uint32_t i = 0; i < n && src[i];) {
        uint8_t c = src[i];
        uint32_t len = c < 0x80u ? 1u : (c & 0xe0u) == 0xc0u ? 2u :
                       (c & 0xf0u) == 0xe0u ? 3u :
                       (c & 0xf8u) == 0xf0u ? 4u : 1u;
        if (len > n-i || used+len >= cap) break;
        bool valid = true;
        for (uint32_t j=1; j<len; ++j)
            if ((src[i+j] & 0xc0u) != 0x80u) valid = false;
        if (!valid) { i++; continue; }
        for (uint32_t j=0; j<len; ++j) dst[used++] = (char)src[i+j];
        i += len;
    }
    dst[used] = 0;
}
static void decode_text(char *dst, unsigned cap, const uint8_t *src, uint32_t n,
                        uint32_t encoding)
{
    if (!cap) return;
    dst[0] = 0;
    if (encoding == 3u) {
        copy_utf8(dst, cap, src, n);
        return;
    }
    unsigned used=0;
    if (encoding == 0u) {
        for (uint32_t i=0; i<n && src[i]; ++i)
            used=emit_cp(dst, used, cap, src[i]);
        return;
    }
    if (encoding != 1u && encoding != 2u) return;
    bool little = encoding == 1u;
    uint32_t pos=0;
    if (n>=2 && src[0]==0xff && src[1]==0xfe) {little=true; pos=2;}
    else if (n>=2 && src[0]==0xfe && src[1]==0xff) {little=false; pos=2;}
    for (; pos+1<n; pos+=2) {
        uint32_t cp = little ? (src[pos]|((uint32_t)src[pos+1]<<8)) :
                                   ((uint32_t)src[pos]<<8)|src[pos+1];
        if (!cp) break;
        if (cp>=0xd800 && cp<=0xdbff && pos+3<n) {
            uint32_t second = little ? (src[pos+2]|((uint32_t)src[pos+3]<<8)) :
                                               ((uint32_t)src[pos+2]<<8)|src[pos+3];
            if (second>=0xdc00 && second<=0xdfff) {
                cp = 0x10000u + ((cp-0xd800u)<<10) + (second-0xdc00u);
                pos+=2;
            }
        }
        used=emit_cp(dst, used, cap, cp);
    }
}
static uint16_t year_from(const char *text)
{
    uint32_t val = 0;
    for (unsigned i=0;i<4 && text[i];++i) {
        if (text[i]<'0' || text[i]>'9') return 0;
        val = val*10u+(uint32_t)(text[i]-'0');
    }
    return val>=1000u && val<=9999u ? (uint16_t)val : 0;
}
static uint16_t positive_from(const char *text)
{
    uint32_t val=0;
    for (unsigned i=0;i<5 && text[i]>='0' && text[i]<='9';++i)
        val=val*10u+(uint32_t)(text[i]-'0');
    return (uint16_t)(val<=65535u ? val : 0);
}
static void apply_field(hifi_tags_t *m, const char *id, const uint8_t *value,
                        uint32_t bytes, uint32_t encoding, bool short_id)
{
    char tmp[HIFI_TAG_TEXT_MAX] = {0};
    decode_text(tmp, sizeof(tmp), value, bytes, encoding);
    if (!tmp[0]) return;
    if (!strcmp(id, short_id ? "TT2" : "TIT2"))
        aster_snprintf(m->title, sizeof(m->title), "%s", tmp);
    else if (!strcmp(id, short_id ? "TP1" : "TPE1"))
        aster_snprintf(m->artist, sizeof(m->artist), "%s", tmp);
    else if (!strcmp(id, short_id ? "TP2" : "TPE2"))
        aster_snprintf(m->album_artist, sizeof(m->album_artist), "%s", tmp);
    else if (!strcmp(id, short_id ? "TAL" : "TALB"))
        aster_snprintf(m->album, sizeof(m->album), "%s", tmp);
    else if (!strcmp(id, "TDRC") || !strcmp(id, "TYER") ||
             !strcmp(id, "TYE")) m->year = year_from(tmp);
    else if (!strcmp(id, "TDOR") || !strcmp(id, "TORY"))
        m->original_year = year_from(tmp);
    else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK"))
        m->track_number = positive_from(tmp);
    else if (!strcmp(id, "TPOS") || !strcmp(id, "TPA"))
        m->disc_number = positive_from(tmp);
}
static void parse_id3(aster_handle_t file, uint32_t bytes, hifi_tags_t *m)
{
    uint8_t head[10];
    if (bytes<10 || !read_at(file,0,head,sizeof(head)) ||
        memcmp(head,"ID3",3) || head[3]<2 || head[3]>4) return;
    uint32_t version=head[3], end=10u+sync32(head+6), pos=10;
    if (end>bytes) end=bytes;
    if (head[5]&0x40u) {
        uint8_t ext[4];
        if (!read_at(file,pos,ext,4)) return;
        uint32_t n=version==4u ? sync32(ext) : be32(ext)+4u;
        if (n<4 || n>end-pos) return;
        pos+=n;
    }
    for (unsigned frames=0;frames<TAG_FRAME_LIMIT && pos+6<=end;frames++) {
        uint8_t hdr[10]={0};
        uint32_t hs=version==2u ? 6u:10u;
        if (end-pos<hs || !read_at(file,pos,hdr,hs)) break;
        uint32_t id_len=version==2u?3u:4u;
        bool valid=true;
        for (unsigned i=0;i<id_len;i++)
            if (!((hdr[i]>='A'&&hdr[i]<='Z')||(hdr[i]>='0'&&hdr[i]<='9'))) valid=false;
        if (!valid) break;
        uint32_t size=version==2u? ((uint32_t)hdr[3]<<16)|((uint32_t)hdr[4]<<8)|hdr[5]:
                      version==4u?sync32(hdr+4):be32(hdr+4);
        if (size>end-pos-hs) break;
        char id[5]={0};
        memcpy(id,hdr,id_len);
        uint32_t payload=pos+hs;
        bool text=id[0]=='T' && strcmp(id,"TXXX") && strcmp(id,"TXX");
        if (text && size>1 && size<=TAG_READ_CAP &&
            read_at(file,payload,scratch,size)) {
            uint8_t encoding=scratch[0];
            apply_field(m,id,scratch+1,size-1,encoding,version==2u);
        }
        pos=payload+size;
    }
}
static void parse_id3v1(aster_handle_t file, uint32_t size, hifi_tags_t *m)
{
    uint8_t tail[128];
    if (size<128 || !read_at(file,size-128,tail,128) || memcmp(tail,"TAG",3)) return;
    char tmp[HIFI_TAG_TEXT_MAX]={0};
    if (!m->title[0]) {
        decode_text(tmp,sizeof(tmp),tail+3,30,0);
        aster_snprintf(m->title,sizeof(m->title),"%s",tmp);
    }
    if (!m->artist[0]) {
        decode_text(tmp,sizeof(tmp),tail+33,30,0);
        aster_snprintf(m->artist,sizeof(m->artist),"%s",tmp);
    }
    if (!m->album[0]) {
        decode_text(tmp,sizeof(tmp),tail+63,30,0);
        aster_snprintf(m->album,sizeof(m->album),"%s",tmp);
    }
    if (!m->year) {
        char year[5]={0};
        memcpy(year,tail+93,4);
        m->year=year_from(year);
    }
}
static bool key_equal(const char *a, const char *b)
{
    while (*a && *b) {
        char l=*a++,r=*b++;
        if (l>='a'&&l<='z') l-='a'-'A';
        if (r>='a'&&r<='z') r-='a'-'A';
        if (l!=r) return false;
    }
    return !*a && !*b;
}
static void apply_comment(hifi_tags_t *m, const uint8_t *buf, uint32_t bytes)
{
    uint32_t equals=0;
    while(equals<bytes && buf[equals]!='=') equals++;
    if (equals==bytes || equals>30) return;
    char key[32]={0}, val[HIFI_TAG_TEXT_MAX]={0};
    memcpy(key,buf,equals);
    copy_utf8(val,sizeof(val),buf+equals+1,bytes-equals-1);
    if (!val[0]) return;
    if (key_equal(key,"TITLE"))
        aster_snprintf(m->title,sizeof(m->title),"%s",val);
    else if(key_equal(key,"ARTIST"))
        aster_snprintf(m->artist,sizeof(m->artist),"%s",val);
    else if(key_equal(key,"ALBUM"))
        aster_snprintf(m->album,sizeof(m->album),"%s",val);
    else if(key_equal(key,"ALBUMARTIST") || key_equal(key,"ALBUM ARTIST"))
        aster_snprintf(m->album_artist,sizeof(m->album_artist),"%s",val);
    else if(key_equal(key,"ORIGINALDATE") || key_equal(key,"ORIGINALYEAR"))
        m->original_year=year_from(val);
    else if(key_equal(key,"DATE") || key_equal(key,"YEAR"))
        m->year=year_from(val);
    else if(key_equal(key,"TRACKNUMBER"))
        m->track_number=positive_from(val);
    else if(key_equal(key,"DISCNUMBER"))
        m->disc_number=positive_from(val);
}
static void parse_flac(aster_handle_t file, uint32_t bytes, hifi_tags_t *m)
{
    uint8_t magic[4];
    uint32_t pos=0;
    if (bytes<4 || !read_at(file,0,magic,4)) return;
    if (memcmp(magic,"fLaC",4)) {
        uint8_t id3[10];
        if (bytes<14 || !read_at(file,0,id3,10) || memcmp(id3,"ID3",3))return;
        pos=10u+sync32(id3+6)+(id3[5]&0x10u?10u:0u);
        if (pos>bytes-4 || !read_at(file,pos,magic,4) ||
            memcmp(magic,"fLaC",4)) return;
    }
    pos+=4;
    for (unsigned blocks=0;blocks<32 && pos+4<=bytes;blocks++) {
        uint8_t h[4];
        if (!read_at(file,pos,h,4)) return;
        uint32_t block=((uint32_t)h[1]<<16)|((uint32_t)h[2]<<8)|h[3];
        uint32_t begin=pos+4u;
        if (block>bytes-begin) return;
        if ((h[0]&0x7fu)==4 && block>=8) {
            uint8_t value[4];
            if (!read_at(file,begin,value,4)) return;
            uint32_t vendor=le32(value), cursor=begin+4u;
            if (vendor>block-8u) return;
            cursor+=vendor;
            if (!read_at(file,cursor,value,4))return;
            uint32_t count=le32(value);cursor+=4;
            if (count>TAG_COMMENT_LIMIT) count=TAG_COMMENT_LIMIT;
            for (uint32_t i=0;i<count && cursor+4<=begin+block;i++) {
                if (!read_at(file,cursor,value,4))return;
                uint32_t n=le32(value);cursor+=4;
                if (n>begin+block-cursor)break;
                if (n>0 && n<=TAG_READ_CAP && read_at(file,cursor,scratch,n))
                    apply_comment(m,scratch,n);
                cursor+=n;
            }
            return;
        }
        pos=begin+block;
        if (h[0]&0x80u) break;
    }
}
bool hifi_tags_read(const char *relative_path, hifi_tags_t *out)
{
    if (!out) return false;
    *out=(hifi_tags_t){0};
    if (!relative_path || !*relative_path || relative_path[0]=='/' ||
        strlen(relative_path)>=152u) return false;
    char full[160];
    aster_snprintf(full,sizeof(full),"/music/%s",relative_path);
    aster_stat_t stat={.size=sizeof(stat),.version=1};
    unsigned len=(unsigned)strlen(full);
    if (aster_fs_stat(full,len,&stat,sizeof(stat))<0 ||
        (stat.mode&ASTER_STAT_DIR)) return false;
    aster_handle_t f=0;
    if (aster_fs_open(full,len,ASTER_FS_O_RDONLY,&f)<0) return false;
    uint32_t file_bytes=(uint32_t)(stat.filesize>0x7fffffffULL ? 0x7fffffffu : stat.filesize);
    const char *ext=relative_path;
    for (const char *p=relative_path;*p;p++)if(*p=='.')ext=p+1;
    char extension[6]={0};
    for(unsigned i=0;i<5 && ext[i];i++){
        char v=ext[i]; extension[i]=v>='A'&&v<='Z'?(char)(v+32):v;
    }
    if (!strcmp(extension,"mp3")) {
        parse_id3(f,file_bytes,out);
        parse_id3v1(f,file_bytes,out);
    } else if (!strcmp(extension,"flac"))
        parse_flac(f,file_bytes,out);
    aster_fs_close(f);
    return true;
}
