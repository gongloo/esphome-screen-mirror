import gzip
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "components"))

from esphome.const import KEY_CORE, KEY_TARGET_PLATFORM, KEY_TARGET_FRAMEWORK
from esphome.core import CORE
import esphome.final_validate as fv
import screen_mirror


class TestScreenMirrorCodegen(unittest.TestCase):
    def setUp(self):
        CORE.data[KEY_CORE] = {
            KEY_TARGET_PLATFORM: "esp32",
            KEY_TARGET_FRAMEWORK: "esp-idf",
        }
    def test_detect_rotation_explicit(self):
        config = {"rotation": 180}
        self.assertEqual(screen_mirror._detect_rotation(config), 180)

    def test_detect_rotation_from_lvgl(self):
        config = {}
        fv.full_config.set({"lvgl": [{"rotation": 270}]})
        self.assertEqual(screen_mirror._detect_rotation(config), 270)

        # Single dict notation
        fv.full_config.set({"lvgl": {"rotation": 90}})
        self.assertEqual(screen_mirror._detect_rotation(config), 90)

    def test_detect_rotation_from_display(self):
        config = {}
        fv.full_config.set({"display": [{"rotation": 90}]})
        self.assertEqual(screen_mirror._detect_rotation(config), 90)

    def test_detect_rotation_fallback(self):
        config = {}
        fv.full_config.set({})
        self.assertEqual(screen_mirror._detect_rotation(config), 0)

    def test_detect_byte_order_explicit(self):
        cfg_be = screen_mirror.CONFIG_SCHEMA({"display": "d", "byte_order": "big_endian"})
        self.assertEqual(screen_mirror._detect_byte_order(cfg_be), True)

        cfg_le = screen_mirror.CONFIG_SCHEMA({"display": "d", "byte_order": "little_endian"})
        self.assertEqual(screen_mirror._detect_byte_order(cfg_le), False)

    def test_detect_byte_order_from_lvgl(self):
        config = {}
        fv.full_config.set({"lvgl": [{"byte_order": "big_endian"}]})
        self.assertEqual(screen_mirror._detect_byte_order(config), True)

        fv.full_config.set({"lvgl": [{"byte_order": "little_endian"}]})
        self.assertEqual(screen_mirror._detect_byte_order(config), False)

    def test_detect_byte_order_from_display(self):
        config = {}
        fv.full_config.set({"display": [{"byte_order": "big_endian"}]})
        self.assertEqual(screen_mirror._detect_byte_order(config), True)

    def test_detect_byte_order_from_display_metadata(self):
        from esphome.components import display
        from esphome.core import ID
        disp_id = ID("disp_be", is_declaration=True, type=display.Display)
        display.add_metadata(disp_id, 800, 480, byte_order="big_endian")
        config = {"display": disp_id}
        self.assertEqual(screen_mirror._detect_byte_order(config), True)

    def test_detect_byte_order_fallback(self):
        config = {}
        fv.full_config.set({})
        self.assertEqual(screen_mirror._detect_byte_order(config), False)

    def test_remote_html_compression(self):
        html_path = REPO_ROOT / "components" / "screen_mirror" / "remote.html"
        self.assertTrue(html_path.is_file(), "remote.html must exist")

        html_bytes = html_path.read_bytes()
        self.assertGreater(len(html_bytes), 1000, "remote.html should have substantial content")

        gz_bytes = gzip.compress(html_bytes, 9)
        self.assertLess(len(gz_bytes), len(html_bytes), "Gzip compression must reduce size")
        # Ensure it decompresses losslessly
        self.assertEqual(gzip.decompress(gz_bytes), html_bytes)

    def test_final_validate_auto_detect_rotation(self):
        config = {"display": "d"}
        fv.full_config.set({"display": [{"rotation": 180}]})
        validated = screen_mirror._final_validate(config)
        self.assertEqual(validated.get("rotation"), 180)

    def test_final_validate_preserves_explicit(self):
        config = {"display": "d", "rotation": 270}
        fv.full_config.set({"display": [{"rotation": 90}]})
        validated = screen_mirror._final_validate(config)
        self.assertEqual(validated.get("rotation"), 270)

    def test_detect_rotation_lvgl_precedence_over_display(self):
        config = {}
        fv.full_config.set({
            "lvgl": [{"rotation": 90}],
            "display": [{"rotation": 180}],
        })
        self.assertEqual(screen_mirror._detect_rotation(config), 90)

    def test_detect_byte_order_lvgl_precedence_over_display(self):
        config = {}
        fv.full_config.set({
            "lvgl": [{"byte_order": "big_endian"}],
            "display": [{"byte_order": "little_endian"}],
        })
        self.assertEqual(screen_mirror._detect_byte_order(config), True)

    def test_remote_html_rotation_injection(self):
        html_path = REPO_ROOT / "components" / "screen_mirror" / "remote.html"
        html_text = html_path.read_text(encoding="utf-8")
        self.assertIn("/*%ROTATION%*/ 0", html_text, "Placeholder must exist in remote.html")

        # Test injecting 90, 180, 270
        for rot in [90, 180, 270]:
            injected = html_text.replace("/*%ROTATION%*/ 0", str(rot))
            self.assertIn(f"let rotation = {rot};", injected)


if __name__ == "__main__":
    unittest.main()
