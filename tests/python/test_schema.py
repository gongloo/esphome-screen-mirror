import sys
import unittest
from pathlib import Path

# Add components directory to module search path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "components"))

from esphome.const import KEY_CORE, KEY_TARGET_PLATFORM, KEY_TARGET_FRAMEWORK
from esphome.core import CORE
import esphome.config_validation as cv
import screen_mirror


class TestScreenMirrorSchema(unittest.TestCase):
    def setUp(self):
        # Default mock environment: ESP32 + esp-idf
        CORE.data[KEY_CORE] = {
            KEY_TARGET_PLATFORM: "esp32",
            KEY_TARGET_FRAMEWORK: "esp-idf",
        }

    def test_valid_minimal_config(self):
        config = screen_mirror.CONFIG_SCHEMA({"display": "main_display"})
        self.assertEqual(str(config["display"]), "main_display")
        self.assertEqual(config["fps"], 1.0)
        self.assertEqual(config["jpeg_quality"], 75)
        self.assertEqual(config["gated_streaming"], True)
        self.assertEqual(config["web_path"], "/remote")
        self.assertEqual(config["show_telemetry"], True)

    def test_valid_full_config(self):
        raw = {
            "display": "main_display",
            "touchscreen": "main_touch",
            "rotation": 90,
            "byte_order": "big_endian",
            "fps": 15.0,
            "jpeg_quality": 85,
            "gated_streaming": False,
            "web_path": "/dashboard",
            "show_telemetry": False,
        }
        config = screen_mirror.CONFIG_SCHEMA(raw)
        self.assertEqual(str(config["display"]), "main_display")
        self.assertEqual(str(config["touchscreen"]), "main_touch")
        self.assertEqual(config["rotation"], 90)
        self.assertEqual(config["byte_order"], "big_endian")
        self.assertEqual(config["byte_order"].enum_value, True)
        self.assertEqual(config["fps"], 15.0)
        self.assertEqual(config["jpeg_quality"], 85)
        self.assertEqual(config["gated_streaming"], False)
        self.assertEqual(config["web_path"], "/dashboard")
        self.assertEqual(config["show_telemetry"], False)

    def test_platform_rejection(self):
        # Reject non-ESP32 platforms (e.g. ESP8266, RP2040)
        CORE.data[KEY_CORE][KEY_TARGET_PLATFORM] = "esp8266"
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "main_display"})

    def test_framework_rejection(self):
        # Reject non-esp-idf frameworks (e.g. Arduino)
        CORE.data[KEY_CORE][KEY_TARGET_FRAMEWORK] = "arduino"
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "main_display"})

    def test_fps_validation(self):
        # Valid bounds
        config_min = screen_mirror.CONFIG_SCHEMA({"display": "disp", "fps": 0.1})
        self.assertAlmostEqual(config_min["fps"], 0.1)
        config_max = screen_mirror.CONFIG_SCHEMA({"display": "disp", "fps": 30.0})
        self.assertAlmostEqual(config_max["fps"], 30.0)

        # Invalid bounds
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "fps": 0.0})
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "fps": -5.0})
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "fps": 31.0})

    def test_jpeg_quality_validation(self):
        # Valid bounds (10 to 100)
        config_min = screen_mirror.CONFIG_SCHEMA({"display": "disp", "jpeg_quality": 10})
        self.assertEqual(config_min["jpeg_quality"], 10)
        config_max = screen_mirror.CONFIG_SCHEMA({"display": "disp", "jpeg_quality": 100})
        self.assertEqual(config_max["jpeg_quality"], 100)

        # Invalid bounds
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "jpeg_quality": 9})
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "jpeg_quality": 101})

    def test_rotation_validation(self):
        for rot in [0, 90, 180, 270]:
            cfg = screen_mirror.CONFIG_SCHEMA({"display": "disp", "rotation": rot})
            self.assertEqual(cfg["rotation"], rot)

        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "rotation": 45})
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "rotation": 120})

    def test_byte_order_validation(self):
        cfg_be = screen_mirror.CONFIG_SCHEMA({"display": "disp", "byte_order": "big_endian"})
        self.assertEqual(cfg_be["byte_order"], "big_endian")
        self.assertEqual(cfg_be["byte_order"].enum_value, True)

        cfg_le = screen_mirror.CONFIG_SCHEMA({"display": "disp", "byte_order": "little_endian"})
        self.assertEqual(cfg_le["byte_order"], "little_endian")
        self.assertEqual(cfg_le["byte_order"].enum_value, False)

        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({"display": "disp", "byte_order": "middle_endian"})

    def test_missing_required_display(self):
        with self.assertRaises(cv.Invalid):
            screen_mirror.CONFIG_SCHEMA({})

    def test_web_path_and_options(self):
        cfg = screen_mirror.CONFIG_SCHEMA({
            "display": "disp",
            "web_path": "/screen_stream",
            "gated_streaming": False,
            "show_telemetry": False,
        })
        self.assertEqual(cfg["web_path"], "/screen_stream")
        self.assertEqual(cfg["gated_streaming"], False)
        self.assertEqual(cfg["show_telemetry"], False)


if __name__ == "__main__":
    unittest.main()
