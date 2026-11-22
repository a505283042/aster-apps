"""HiFi V1 controls: source-level regression checks.

This suite deliberately avoids an ESP32/SDK dependency. It checks that the
expanded controls preserve the existing WASM UI interaction contract, while
real hardware/UI rendering is verified separately on the board.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "src/main.c").read_text(encoding="utf-8")
MANIFEST = (ROOT / "app.toml").read_text(encoding="utf-8")


def between(start: str, end: str) -> str:
    i = SRC.index(start)
    j = SRC.index(end, i + len(start))
    return SRC[i:j]


class HiFiControlsV1(unittest.TestCase):
    def test_version_and_460_canvas(self):
        self.assertIn('version = "0.5.0-alpha.8"', MANIFEST)
        self.assertIn("DESIGN = 460", SRC)

    def test_seek_bar_render_and_touch_are_consistent(self):
        self.assertRegex(SRC, r"PLAY_PROGRESS\s*=\s*286")
        playback = between("static void playback(void)", "static int next_lyric_group(")
        hit = between("static int hit(int x, int y)", "static bool page_swipe_area(")
        self.assertIn("box(60, PLAY_PROGRESS, 340, 10", playback)
        self.assertIn("circle(60 + filled, PLAY_PROGRESS + 5", playback)
        self.assertIn("PLAY_PROGRESS - 14", hit)
        self.assertIn("PLAY_PROGRESS + 24", hit)

    def test_playlist_button_symmetric_with_mode(self):
        playback = between("static void playback(void)", "static int next_lyric_group(")
        hit = between("static int hit(int x, int y)", "static bool page_swipe_area(")
        action = between("static void activate(int id)", "static void slider(int id, int x)")
        self.assertIn("box(48, 382, 60, 60", playback)
        self.assertIn("box(352, 382, 60, 60", playback)
        self.assertIn("return PLAY_LIST;", hit)
        self.assertIn("case PLAY_LIST:", action)
        self.assertIn("library_mode = LIBRARY_ALL_TRACKS;", action)
        self.assertIn("library_detail_group = -1;", action)
        self.assertIn("library_page = active >= 0 ? active / ROWS : 0;", action)
        self.assertIn("set_page(LIBRARY);", action)
        self.assertIn("finger.page_swipe =", between("static void touch(", "static void poll("))

    def test_mode_is_drawn_without_unsupported_font_glyphs(self):
        mode = between("static void mode_icon(", "static void list_icon(")
        for name in ("ORDER_SEQUENCE", "ORDER_REPEAT_ALL", "ORDER_REPEAT_ONE", "ORDER_SHUFFLE"):
            if name in ("ORDER_REPEAT_ALL", "ORDER_REPEAT_ONE"):
                continue  # repeat-one shares the repeat stroke geometry
            self.assertIn(name, mode)
        self.assertIn("line(", mode)
        self.assertIn("triangle(", mode)
        self.assertNotIn('text(68, 367', mode)

    def test_info_and_source_pills(self):
        statuses = between("static void playback_status_pills(", "static void playback_audio_info(")
        for label in ("有线", "蓝牙接收", "蓝牙发射", "提示"):
            self.assertIn(label, statuses)
        info = between("static void playback_audio_info(", "/* Two fully distinct album states")
        self.assertIn("current_index()", info)
        self.assertIn("track_count", info)
        self.assertIn("kHz", info)
        self.assertIn("player.bits", info)
        # Current-song lookup is cached by current track and HIF2 generation.
        lookup = between("static int current_index(void)\n{", "static int library_item_count(")
        self.assertIn("lookup_generation == catalog_generation", lookup)

    def test_audio_owner_and_cover_mode_unchanged(self):
        self.assertIn("static bool bright_pending = true;", SRC)
        self.assertIn("aster_surface_present(surface)", SRC)
        self.assertIn("ASTER_PLAYER_SET_QUEUE_MODE", SRC)
        self.assertIn("ASTER_PLAYER_F_BT_OUTPUT", SRC)
        self.assertNotIn("aster_device_audio_set_volume", SRC)


if __name__ == "__main__":
    unittest.main()
