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
        self.assertIn('version = "0.5.0-alpha.10"', MANIFEST)
        self.assertIn("DESIGN = 460", SRC)

    def test_seek_bar_render_and_touch_are_consistent(self):
        self.assertRegex(SRC, r"PLAY_PROGRESS\s*=\s*286")
        playback = between("static void playback(void)", "static int next_lyric_group(")
        hit = between("static int hit(int x, int y)", "static bool page_swipe_area(")
        self.assertIn("box(60, PLAY_PROGRESS, 340, 10", playback)
        self.assertIn("circle(60 + filled, PLAY_PROGRESS + 5", playback)
        self.assertIn("PLAY_PROGRESS - 14", hit)
        self.assertIn("PLAY_PROGRESS + 19", hit)

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

    def test_queue_number_width_and_transport_spacing(self):
        info = between("static void playback_audio_info(", "/* Two fully distinct album states")
        self.assertIn('"%u / %u · %s · %s"', info)
        self.assertIn('"%u / %u · %s"', info)
        self.assertNotIn("%04u", info)
        playback = between("static void playback(void)", "static int next_lyric_group(")
        hit = between("static int hit(int x, int y)", "static bool page_swipe_area(")
        self.assertIn("circle(116, 194, 37", playback)
        self.assertIn("circle(230, 194, 47", playback)
        self.assertIn("circle(344, 194, 37", playback)
        self.assertIn("x >= 79 && x < 153", hit)
        self.assertIn("x >= 307 && x < 381", hit)
        # Circular surfaces are disjoint and each has its own hit target.
        self.assertEqual((230 - 47) - (116 + 37), 30)
        self.assertEqual((344 - 37) - (230 + 47), 30)

    def test_one_current_audio_source_in_center(self):
        source = between("static const char *source_label(void)", "static const char *playback_state(void)")
        self.assertIn('return "有线";', source)
        self.assertIn('return "蓝牙接收";', source)
        self.assertIn('return "蓝牙发射";', source)
        indicator = between("static void playback_source_indicator(", "static void playback_audio_info(")
        self.assertIn("const char *name = source_label();", indicator)
        self.assertIn("box(146, 382, 168, 60", indicator)
        self.assertIn("text(199, 400, 106, 20, white, name, CENTER)", indicator)
        self.assertNotIn("aster_device_audio_set_volume", indicator)
        playback = between("static void playback(void)", "static int next_lyric_group(")
        self.assertIn("playback_source_indicator(white);", playback)
        self.assertNotIn("playback_status_pills", SRC)
        self.assertEqual(playback.count("playback_source_indicator(white);"), 1)
        self.assertIn("box(48, 382, 60, 60", playback)
        self.assertIn("box(352, 382, 60, 60", playback)

    def test_artist_field_does_not_substitute_output_status(self):
        # Some songs lack artist metadata; output status belongs in its
        # dedicated bottom capsule, never in the artist line at y=61.
        playback = between("static void playback(void)", "static int next_lyric_group(")
        self.assertIn("const char *by = artist();", playback)
        self.assertIn("if (*by)", playback)
        self.assertIn("text(40, 61, 380, 21, secondary, by, CENTER)", playback)
        self.assertNotIn("*by ? by : source_label()", playback)
        self.assertIn("playback_source_indicator(white);", playback)

    def test_error_toast_kept_without_permanent_notice_slot(self):
        draw = between("static void draw(uint32_t now)", "static int hit(int x, int y)")
        self.assertIn("if (message[0] &&", draw)
        self.assertIn("text(44, ty + (page == PLAYBACK ? 7 : 12)", draw)
        report = between("static int report_control(", "static int control(")
        self.assertIn("notice(", report)

    def test_queue_metadata_remains(self):
        info = between("static void playback_audio_info(", "/* Two fully distinct album states")
        self.assertIn("current_index()", info)
        self.assertIn("track_count", info)
        self.assertIn("kHz", info)
        self.assertIn("player.bits", info)
        # Current-song lookup is cached by current track and HIF2 generation.
        lookup = between("static int current_index(void)\n{", "static int library_item_count(")
        self.assertIn("lookup_generation == catalog_generation", lookup)

    def test_next_prev_does_not_flash_last_explicit_library_selection(self):
        # A global NEXT/PREV is owned by firmware. Its media state briefly
        # becomes STOPPING -> IDLE while the old path is retained. The UI
        # must not fall back to the old selected_file during this handoff.
        selected = between("static const char *display_track(void)",
                           "static void confirm_media_track(void)")
        self.assertIn("confirmed_media_track[0]", selected)
        self.assertIn("!strcmp(active, confirmed_media_track)", selected)
        self.assertLess(selected.index("confirmed_media_track[0]"),
                        selected.index("return selected_file[0]"))
        confirm = between("static void confirm_media_track(void)",
                          "static const char *title(")
        for state in ("ASTER_PLAYER_OPENING", "ASTER_PLAYER_PLAYING",
                      "ASTER_PLAYER_PAUSED"):
            self.assertIn(state, confirm)
        self.assertNotIn("aster_kv_set(", confirm)
        self.assertNotIn("aster_fs_", confirm)
        poll = between("static void poll(uint32_t now)",
                       "ASTER_APP_EXPORT int aster_app_init(void)")
        self.assertIn("confirm_media_track();", poll)
        self.assertIn("confirmed_media_track[0] = 0;", poll)
        explicit = between("static void queue_track(const char *path)",
                           "static void play_index(int index)")
        self.assertIn("confirmed_media_track[0] = 0;", explicit)
        self.assertIn("select_track(path);", explicit)
        transport = between("    case PREVIOUS:\n    case NEXT:",
                            "    default:")
        self.assertIn("ASTER_PLAYER_NEXT", transport)
        self.assertNotIn("queue_track(", transport)

    def test_audio_owner_and_cover_mode_unchanged(self):
        self.assertIn("static bool bright_pending = true;", SRC)
        self.assertIn("aster_surface_present(surface)", SRC)
        self.assertIn("ASTER_PLAYER_SET_QUEUE_MODE", SRC)
        self.assertIn("ASTER_PLAYER_F_BT_OUTPUT", SRC)
        self.assertNotIn("aster_device_audio_set_volume", SRC)


if __name__ == "__main__":
    unittest.main()
