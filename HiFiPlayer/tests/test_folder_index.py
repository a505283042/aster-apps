"""Native C regression test for HFD1 tree creation, persistence and navigation.

Run: python -m unittest HiFiPlayer.tests.test_folder_index -v
Requires a host C compiler (cc) but no ESP32 / SDK / actual TF card.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


@unittest.skipUnless(shutil.which("cc"), "C compiler unavailable")
class FolderIndex(unittest.TestCase):
    def test_snapshot_build_open_and_generation_refresh(self):
        code = (SRC / "music_browser.c").read_text(encoding="utf-8")
        code = code.replace('#include "music_browser.h"', "")
        code = code.replace("#include <aster_imports.h>", "")
        # Compile the actual production implementation with only the SDK FS
        # and clock imports replaced by deterministic host stubs.
        prelude = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "music_browser.h"
typedef uint32_t aster_handle_t;
typedef struct {
    uint32_t size, version, mode;
    uint64_t filesize;
} aster_stat_t;
#define ASTER_FS_O_RDONLY 1u
#define ASTER_FS_O_RDWR 2u
#define ASTER_FS_O_CREAT 4u
#define ASTER_FS_O_TRUNC 8u
#define ASTER_FS_SEEK_SET 0u
#define aster_snprintf snprintf
#define aster_printf(...) ((void)0)

static FILE *opened[16];
static const char *root;
static uint32_t generation=5, songs=5, clock_ms=0;
static const char *tracks[]={
    "a.flac", "A/b.mp3", "A/Nested/c.flac", "B/d.wav",
    "root.mp3", "C/new.mp3"
};
uint32_t aster_time_now_ms(void){return ++clock_ms;}
static void mapped(const char *source,char *dest,size_t size)
{
    const char *base=strrchr(source,'/');
    snprintf(dest,size,"%s/%s",root,base?base+1:source);
}
int32_t aster_fs_stat(const char *path,uint32_t len,
                      aster_stat_t *out,uint32_t n)
{
    (void)len;assert(n==sizeof(*out));
    char p[1024];mapped(path,p,sizeof(p));
    FILE *f=fopen(p,"rb");if(!f)return -1;
    fseek(f,0,SEEK_END);long size=ftell(f);fclose(f);
    *out=(aster_stat_t){.size=sizeof(*out),.version=1,
                        .filesize=(uint64_t)size};
    return 0;
}
int32_t aster_fs_open(const char *path,uint32_t len,uint32_t flags,
                      aster_handle_t *out)
{
    (void)len;char p[1024];mapped(path,p,sizeof(p));
    const char *mode=(flags&ASTER_FS_O_TRUNC)?"wb+":
                     (flags&ASTER_FS_O_RDWR)?"rb+":"rb";
    FILE *f=fopen(p,mode);if(!f)return -1;
    for(unsigned i=1;i<16;i++)if(!opened[i]){
        opened[i]=f;*out=i;return 0;
    }
    fclose(f);return -2;
}
int32_t aster_fs_close(aster_handle_t handle)
{
    assert(handle>0&&handle<16&&opened[handle]);
    fclose(opened[handle]);opened[handle]=NULL;return 0;
}
int32_t aster_fs_read(aster_handle_t handle,void *ptr,uint32_t n)
{
    assert(handle>0&&opened[handle]);
    return (int32_t)fread(ptr,1,n,opened[handle]);
}
int32_t aster_fs_write(aster_handle_t handle,const void *ptr,uint32_t n)
{
    assert(handle>0&&opened[handle]);
    return (int32_t)fwrite(ptr,1,n,opened[handle]);
}
int32_t aster_fs_seek(aster_handle_t handle,int32_t off,uint32_t whence)
{
    assert(handle>0&&opened[handle]);
    return fseek(opened[handle],off,whence==ASTER_FS_SEEK_SET?SEEK_SET:SEEK_CUR);
}
hifi_catalog_status_t hifi_catalog_status(void)
{
    hifi_catalog_status_t status={0};
    status.has_saved=true;
    status.generation=generation;
    status.count=songs;
    return status;
}
bool hifi_catalog_path_at(uint32_t index,char *out,unsigned size)
{
    if(index>=songs || strlen(tracks[index])+1>size)return false;
    strcpy(out,tracks[index]);return true;
}
'''
        checks = r'''
static void drive_until_ready(void)
{
    for(int i=0;i<20000&&!hifi_browser_ready();i++)
        hifi_browser_tick();
    assert(hifi_browser_ready() && !hifi_browser_error());
}
int main(int argc,char **argv)
{
    assert(argc==2);root=argv[1];
    hifi_browser_start();
    drive_until_ready();
    assert(hifi_browser_count()==4);
    hifi_browser_entry_t a={0},b={0};
    assert(hifi_browser_entry(0,&a) && a.folder);
    assert(hifi_browser_entry(1,&b) && b.folder);
    assert(!strcmp(a.name,"A") && a.songs==2);
    assert(!strcmp(b.name,"B") && b.songs==1);
    assert(hifi_browser_enter_folder(a.folder_id));
    assert(!strcmp(hifi_browser_folder(),"A"));
    assert(hifi_browser_count()==2);
    hifi_browser_entry_t nested={0};
    assert(hifi_browser_entry(0,&nested) && nested.folder);
    assert(!strcmp(nested.name,"Nested"));
    assert(hifi_browser_enter_folder(nested.folder_id));
    assert(hifi_browser_count()==1);
    hifi_browser_entry_t track={0};
    assert(hifi_browser_entry(0,&track) && !track.folder);
    assert(track.track_index==2);
    hifi_browser_up();hifi_browser_up();
    assert(!*hifi_browser_folder() && hifi_browser_count()==4);

    hifi_browser_close();
    hifi_browser_start();
    assert(hifi_browser_progress().stage==HIFI_BROWSER_LOADING);
    drive_until_ready(); /* persisted CRC snapshot, no global catalog walk */
    assert(hifi_browser_count()==4);

    generation=6;songs=6; /* refresh invalidates only the folder generation */
    drive_until_ready(); /* tick automatically sees new HIF2 generation */
    assert(hifi_browser_count()==4); /* was not ticked after change yet */
    hifi_browser_tick();
    drive_until_ready();
    assert(hifi_browser_count()==5);
    hifi_browser_close();
    for(int i=1;i<16;i++)assert(!opened[i]);
    return 0;
}
'''
        # Generation refresh test must run at least one tick before expecting
        # the new folder index, because ready() describes the previous gen.
        checks = checks.replace(
            '    drive_until_ready(); /* tick automatically sees new HIF2 generation */\n'
            '    assert(hifi_browser_count()==4); /* was not ticked after change yet */',
            '    assert(hifi_browser_count()==4); /* prior index still loaded */')
        with tempfile.TemporaryDirectory() as folder:
            temp = Path(folder)
            combined = temp / "test.c"
            combined.write_text(prelude + code + checks, encoding="utf-8")
            exe = temp / "folder_test"
            build = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-I" + str(SRC), str(combined), "-o", str(exe)]
            subprocess.run(build, check=True, capture_output=True, text=True)
            subprocess.run([str(exe),str(temp)], check=True,
                           capture_output=True, text=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
