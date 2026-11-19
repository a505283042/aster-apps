/* SPDX-License-Identifier: MIT */
#include "lrc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static lrc_t lyrics;

static const char *line(int index)
{
    assert(index >= 0 && index < (int)lyrics.count);
    return lyrics.text + lyrics.cue[index].text_offset;
}

int main(void)
{
    static const unsigned char utf8[] = "\xef\xbb\xbf[ar:电台]\r\n[ti:月光]\n"
        "[00:05.24][00:12.150]第一句\n[00:01.005]开场\r[00:12.150]译文\n"
        "[00:60.00]无效时间\n[00:][00:00.]\n[offset:+100]\n[00:20.2]";
    lrc_parse(&lyrics, utf8, sizeof(utf8)-1, NULL);
    assert(lyrics.count == 5);
    assert(!strcmp(lyrics.title, "月光") && !strcmp(lyrics.artist, "电台"));
    assert(lrc_active(&lyrics, 904) == -1);
    assert(!strcmp(line(lrc_active(&lyrics, 905)), "开场"));
    assert(!strcmp(line(lrc_active(&lyrics, 5140)), "第一句"));
    assert(lrc_active(&lyrics, 12050) == 2);
    assert(!strcmp(line(3), "译文"));
    assert(!strcmp(line(lrc_active(&lyrics, 20200)), ""));
    assert(lrc_active(&lyrics, 2000) == 0); /* seek backward */

    static const unsigned char utf16le[] = {
        0xff,0xfe,'[',0,'0',0,':',0,'0',0,'2',0,']',0,0x08,0x67,0x49,0x51
    };
    lrc_parse(&lyrics, utf16le, sizeof(utf16le), NULL);
    assert(lyrics.count == 1 && !strcmp(line(lrc_active(&lyrics, 2000)), "月光"));

    static const unsigned char utf16be[] = {
        0xfe,0xff,0,'[',0,'0',0,':',0,'0',0,'2',0,']',0x67,0x08,0x51,0x49
    };
    lrc_parse(&lyrics, utf16be+2, sizeof(utf16be)-2, NULL);
    assert(lyrics.count == 1 && !strcmp(line(lrc_active(&lyrics, 2000)), "月光"));

    static unsigned char gbk[LRC_GBK_BYTES];
    FILE *map = fopen("text/gbk.bin", "rb");
    assert(map && fread(gbk, 1, sizeof(gbk), map) == sizeof(gbk));
    fclose(map);
    static const unsigned char lyrics_gbk[] = "[ti:\xb5\xbe\xcf\xe3]\n[00:02.00]\xb5\xbe\xcf\xe3";
    assert(lrc_detect(lyrics_gbk, sizeof(lyrics_gbk)-1) == LRC_GBK);
    lrc_parse(&lyrics, lyrics_gbk, sizeof(lyrics_gbk)-1, gbk);
    assert(!strcmp(lyrics.title, "稻香"));
    assert(!strcmp(line(lrc_active(&lyrics, 2000)), "稻香"));

    static const unsigned char enhanced[] = "[offset:+100]\n"
        "[00:02.00][00:08.00]<00:02.15>沿<00:02.50>着<00:02.90>夜色<00:03.30>\n"
        "[00:04.00]普通歌词\n[00:11.00]<00:11.50>错<00:11.10>序";
    lrc_parse(&lyrics, enhanced, sizeof(enhanced)-1, NULL);
    assert(lyrics.count == 4 && !strcmp(line(0), "沿着夜色"));
    assert(lrc_sung_bytes(&lyrics, 0, 2049) == 0);
    assert(lrc_sung_bytes(&lyrics, 0, 2050) == strlen("沿"));
    assert(lrc_sung_bytes(&lyrics, 0, 2400) == strlen("沿着"));
    assert(lrc_sung_bytes(&lyrics, 2, 8800) == strlen("沿着夜色"));
    assert(lrc_sung_bytes(&lyrics, 1, 3900) == strlen("普通歌词"));
    assert(!strcmp(line(3), "错序") && lrc_sung_bytes(&lyrics, 3, 10900) == strlen("错序"));

    lrc_parse(&lyrics, NULL, 0, NULL);
    assert(!lyrics.count && !lyrics.title[0] && lrc_active(&lyrics, 2000) == -1);
    return 0;
}
