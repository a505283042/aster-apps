"""Regression guards for optional S31 JPEG + PPA cover acceleration.

This is an APP source-compatibility test, not an ESP-IDF hardware benchmark.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
COVER = (ROOT / "src/cover.c").read_text(encoding="utf-8")
PLAYER = (ROOT / "src/main.c").read_text(encoding="utf-8")
MANIFEST = (ROOT / "app.toml").read_text(encoding="utf-8")


def section(start, end):
    a = COVER.index(start)
    b = COVER.index(end, a + len(start))
    return COVER[a:b]


class StageBCover(unittest.TestCase):
    def test_optional_v2_no_new_required_import(self):
        self.assertIn('version = "0.5.0-alpha.10"', MANIFEST)
        self.assertIn("sizeof(hifi_cover_hw_args_t) == 64", COVER)
        self.assertIn("sizeof(aster_img_render_args_t) == 40", COVER)
        hw = section("static bool hardware_jpeg(", "static bool begin_jpeg(")
        self.assertIn(".version = 2", hw)
        self.assertIn("aster_img_render(path,", hw)
        self.assertIn(".dst_off = (uint32_t)(uintptr_t)pixels", hw)
        self.assertIn(".dim_off = (uint32_t)(uintptr_t)dim_pixels", hw)
        self.assertNotIn("aster_img_cover_render(", COVER)

    def test_standalone_jpeg_preferred_and_legacy_fallback(self):
        native = section("static bool decode_native(", "static bool open_image(")
        self.assertIn("hardware_jpeg(path, 0, 0, cap)", native)
        self.assertIn("aster_img_render(path, strlen(path), &args, sizeof(args))", native)
        opened = section("static bool open_image(", "static bool sidecar(")
        self.assertIn("return begin_jpeg(path, 0, (uint32_t)st.filesize, cap, fallback)", opened)
        begin = section("static bool begin_jpeg(", "static void begin_embedded(")
        self.assertIn("hardware_jpeg(source, offset, length, cap)", begin)
        self.assertIn("cover_jpeg_begin(input, offset, length, cap, staging_pixels, chunk)", begin)

    def test_embedded_flac_id3_exact_offset_and_fallback(self):
        embedded = section("static void begin_embedded(", "static bool finish_image(")
        self.assertIn("flac(size, &picture)", embedded)
        self.assertIn("id3(0, size, &picture)", embedded)
        self.assertIn("begin_jpeg(track_path, picture.offset, picture.length", embedded)
        tick = section("bool cover_tick(unsigned cap)", "void cover_close(void)")
        self.assertIn("return phase == IDLE;", tick)
        self.assertIn("cover_jpeg_step(&w, &h)", tick)

    def test_dim_only_on_image_swap(self):
        hw = section("static bool hardware_jpeg(", "static bool begin_jpeg(")
        self.assertIn("image_w = args.out_w;", hw)
        self.assertIn("image_h = args.out_h;", hw)
        self.assertIn("phase = IDLE;", hw)
        cpu = section("static bool finish_image(", "static bool decode_native(")
        self.assertIn("dim_pixels[i] =", cpu)
        # Drawing progresses without launching another blend.
        paint = COVER[COVER.index("bool cover_paint("):]
        self.assertNotIn("hardware_jpeg(", paint)
        self.assertNotIn("aster_img_render(", paint)
        self.assertNotIn("dim_pixels[i] =", paint)
        self.assertIn("dimmed ? dim_pixels : pixels", paint)
        self.assertIn("cover_tick(DESIGN)", PLAYER)


if __name__ == "__main__":
    unittest.main()
