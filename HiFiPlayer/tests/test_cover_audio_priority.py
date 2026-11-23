"""HiFiPlayer: cover JPEG/SD work must not interfere with local audio startup."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/main.c").read_text(encoding="utf-8")
TOML = (ROOT / "app.toml").read_text(encoding="utf-8")
COVER = (ROOT / "src/cover.c").read_text(encoding="utf-8")


class CoverAudioPriority(unittest.TestCase):
    def test_local_only_playback_guard(self):
        gate = MAIN[MAIN.index("static bool cover_io_allowed("):
                    MAIN.index("static uint32_t position(")]
        self.assertIn("bluetooth_input() || player.mode != ASTER_PLAYER_LOCAL", gate)
        self.assertIn("strcmp(cover_io_track, track)", gate)
        self.assertIn("COVER_AUDIO_CHANGE_GUARD_MS", gate)
        self.assertIn("(uint32_t)(now - cover_changed_since)", gate)
        self.assertIn("cover_playing_seen = false;", gate)
        self.assertIn("player.state == ASTER_PLAYER_OPENING", gate)
        self.assertIn("player.state == ASTER_PLAYER_STOPPING", gate)
        self.assertIn("player.state == ASTER_PLAYER_PLAYING", gate)
        self.assertIn("player.position_ms >= COVER_AUDIO_POSITION_MS", gate)
        self.assertIn("(uint32_t)(now - cover_playing_since)", gate)
        self.assertIn("return true;", gate)

    def test_cover_step_is_suppressed_before_startup(self):
        event = MAIN[MAIN.index("ASTER_APP_EXPORT void aster_app_event("):
                     MAIN.index("ASTER_APP_EXPORT void aster_app_pause(")]
        self.assertIn("cover_io_allowed(now) && cover_tick(DESIGN)", event)
        self.assertEqual(event.count("cover_tick(DESIGN)"), 1)
        self.assertIn("cover_select(path);", MAIN)
        self.assertIn("Keep the previous art until the new decode completes", MAIN)
        self.assertIn("static bool cover_playing_seen;", MAIN)

    def test_unchanged_cover_surface_contract(self):
        self.assertIn("static uint16_t pixels[COVER_EDGE * COVER_EDGE]", COVER)
        self.assertIn("static uint16_t dim_pixels[COVER_EDGE * COVER_EDGE]", COVER)
        self.assertIn("bool cover_tick(unsigned cap)", COVER)
        self.assertIn('version = "0.5.0-alpha.11"', TOML)


if __name__ == "__main__":
    unittest.main()
