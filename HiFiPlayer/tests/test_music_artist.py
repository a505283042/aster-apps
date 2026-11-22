"""Artist metadata regression guards (SDK/ESP-IDF independent).

This verifies the display data path is backed by ID3/Vorbis tags, with
per-track HIF2 caching and no filesystem work during playback redraws.
A hardware check is still required for real TF and UI timing.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
TAGS = (ROOT / "src/music_tags.c").read_text(encoding="utf-8")
TAG_API = (ROOT / "src/music_tags.h").read_text(encoding="utf-8")
GROUPS = (ROOT / "src/music_groups.c").read_text(encoding="utf-8")
GROUP_API = (ROOT / "src/music_groups.h").read_text(encoding="utf-8")
PLAYER = (ROOT / "src/main.c").read_text(encoding="utf-8")


class ArtistMetadata(unittest.TestCase):
    def test_id3_and_vorbis_artist_sources(self):
        for frame in ('"TPE1"', '"TP1"'):
            self.assertIn(frame, TAGS)
        self.assertIn('key_equal(key,"ARTIST")', TAGS)
        self.assertIn('key_equal(key,"ALBUMARTIST")', TAGS)
        self.assertIn("parse_id3v1(", TAGS)
        self.assertIn("append_artist_segments(m,value,bytes,encoding)", TAGS)
        self.assertIn("append_artist(m,val)", TAGS)
        self.assertIn('memcpy(m->artist+used," / ",3)', TAGS)
        self.assertIn("HIFI_TAG_TEXT_MAX = 192", TAG_API)
        self.assertIn("TAG_READ_CAP = 16 * 1024", TAGS)
        # Aster's WASM SDK intentionally omits strstr from its libc.
        self.assertIn("const char *end=cursor;", TAGS)
        self.assertNotIn('const char *next=strstr(', TAGS)

    def test_cache_version_and_local_artist_lookup(self):
        self.assertIn("GC_VERSION = 2", GROUPS)
        self.assertIn("hifi_groups_track_artist(", GROUP_API)
        track = GROUPS[GROUPS.index("bool hifi_groups_track_artist("):
                       GROUPS.index("bool hifi_groups_busy(")]
        self.assertIn("gc_mode==GC_PROBE || gc_mode==GC_READ", track)
        self.assertIn("expected_generation!=hifi_catalog_status().generation", track)
        self.assertIn("track_artists[track_id]", track)
        self.assertIn("track_album_artists[track_id]", track)
        self.assertNotIn("hifi_tags_read(", track)  # O(1) lookup, no TF

    def test_artist_priority_and_no_hot_frame_tag_reads(self):
        artist = PLAYER[PLAYER.index("static const char *artist(void)"):
                        PLAYER.index("static void cache_artist_when_idle(")]
        self.assertIn("if (bluetooth_input())", artist)
        self.assertIn("hifi_groups_track_artist", artist)
        self.assertIn("selected_artist_path", artist)
        self.assertIn("lyrics.artist", artist)
        self.assertNotIn("source_label()", artist)
        probe = PLAYER[PLAYER.index("static void cache_artist_when_idle("):
                       PLAYER.index("static void notice(")]
        self.assertIn("media_in_use()", probe)
        self.assertIn("hifi_tags_read(path+7,&tags)", probe)
        cover = PLAYER[PLAYER.index("static void playback(void)"):
                       PLAYER.index("static int next_lyric_group(")]
        self.assertNotIn("hifi_tags_read(", cover)


if __name__ == "__main__":
    unittest.main()
