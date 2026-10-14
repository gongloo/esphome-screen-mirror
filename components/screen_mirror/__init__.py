import logging
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE
from esphome.components import display, touchscreen
from esphome.components import web_server_base

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@gongloo"]
AUTO_LOAD = ["web_server_base"]

screen_mirror_ns = cg.esphome_ns.namespace("screen_mirror")
ScreenMirrorComponent = screen_mirror_ns.class_("ScreenMirrorComponent", cg.Component)

CONF_DISPLAY = "display"
CONF_TOUCHSCREEN = "touchscreen"
CONF_ROTATION = "rotation"
CONF_BYTE_ORDER = "byte_order"
CONF_FPS = "fps"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_GATED_STREAMING = "gated_streaming"
CONF_WEB_PATH = "web_path"
CONF_SHOW_TELEMETRY = "show_telemetry"
CONF_CORE = "core"

BYTE_ORDER_OPTIONS = {
    "little_endian": False,
    "big_endian": True,
}

CONFIG_SCHEMA = cv.All(
    cv.only_on_esp32,
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ScreenMirrorComponent),
            cv.Required(CONF_DISPLAY): cv.use_id(display.Display),
            cv.Optional(CONF_TOUCHSCREEN): cv.use_id(touchscreen.Touchscreen),
            cv.Optional(CONF_ROTATION): display.validate_rotation,
            cv.Optional(CONF_BYTE_ORDER): cv.enum(BYTE_ORDER_OPTIONS, lower=True),
            cv.Optional(CONF_FPS, default=1.0): cv.float_range(min=0.1, max=10.0),
            cv.Optional(CONF_JPEG_QUALITY, default=75): cv.int_range(min=10, max=100),
            cv.Optional(CONF_GATED_STREAMING, default=True): cv.boolean,
            cv.Optional(CONF_WEB_PATH, default="/remote"): cv.string,
            cv.Optional(CONF_SHOW_TELEMETRY, default=True): cv.boolean,
            cv.Optional(CONF_CORE, default=0): cv.int_range(min=0, max=1),
        }
    ).extend(cv.COMPONENT_SCHEMA),
)


def _final_validate(config):
    import esphome.final_validate as fv

    full = fv.full_config.get()

    # Auto-detect rotation if not explicitly specified
    if CONF_ROTATION not in config:
        detected_rot = 0
        if "lvgl" in full:
            lv = full["lvgl"]
            if isinstance(lv, list) and len(lv) > 0:
                detected_rot = lv[0].get("rotation", 0)
            elif isinstance(lv, dict):
                detected_rot = lv.get("rotation", 0)
        elif "display" in full:
            disp = full["display"]
            if isinstance(disp, list) and len(disp) > 0:
                detected_rot = disp[0].get("rotation", 0)
            elif isinstance(disp, dict):
                detected_rot = disp.get("rotation", 0)
        config[CONF_ROTATION] = display.validate_rotation(detected_rot)

    # Auto-detect byte_order if not explicitly specified
    if CONF_BYTE_ORDER not in config:
        big_endian = False
        if "lvgl" in full:
            lv = full["lvgl"]
            if isinstance(lv, list) and len(lv) > 0:
                bo = lv[0].get("byte_order")
                if bo is not None:
                    big_endian = (bo == "big_endian")
            elif isinstance(lv, dict):
                bo = lv.get("byte_order")
                if bo is not None:
                    big_endian = (bo == "big_endian")
        elif "display" in full:
            disp = full["display"]
            if isinstance(disp, list) and len(disp) > 0:
                bo = disp[0].get("byte_order")
                if bo is not None:
                    big_endian = (bo == "big_endian")
            elif isinstance(disp, dict):
                bo = disp.get("byte_order")
                if bo is not None:
                    big_endian = (bo == "big_endian")
        config[CONF_BYTE_ORDER] = big_endian

    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    disp = await cg.get_variable(config[CONF_DISPLAY])
    cg.add(var.set_display(disp))

    if CONF_TOUCHSCREEN in config:
        touch = await cg.get_variable(config[CONF_TOUCHSCREEN])
        cg.add(var.set_touchscreen(touch))

    cg.add(var.set_rotation(config[CONF_ROTATION]))
    cg.add(var.set_big_endian(config[CONF_BYTE_ORDER]))
    cg.add(var.set_fps(config[CONF_FPS]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))
    cg.add(var.set_gated_streaming(config[CONF_GATED_STREAMING]))
    cg.add(var.set_web_path(config[CONF_WEB_PATH]))
    cg.add(var.set_show_telemetry(config[CONF_SHOW_TELEMETRY]))
    cg.add(var.set_core(config[CONF_CORE]))
    cg.add_define("USE_SCREEN_MIRROR")

    # Preprocess remote.html into PROGMEM byte array header (gzipped)
    import gzip
    import os

    html_file = os.path.join(os.path.dirname(__file__), "remote.html")
    with open(html_file, "rb") as f:
        html_bytes = f.read()

    gzipped = gzip.compress(html_bytes)
    hex_bytes = ", ".join(f"0x{b:02x}" for b in gzipped)

    header_content = f"""#pragma once
#include "esphome/core/progmem.h"
#include <stddef.h>

// Automatically generated from remote.html during compile phase. Do not edit.
constexpr uint8_t REMOTE_HTML_GZ[] PROGMEM = {{{hex_bytes}}};
constexpr size_t REMOTE_HTML_GZ_SIZE = {len(gzipped)};
"""
    build_src_dir = os.path.join(CORE.build_path, "src")
    os.makedirs(build_src_dir, exist_ok=True)
    with open(os.path.join(build_src_dir, "remote_html.h"), "w", encoding="utf-8") as f:
        f.write(header_content)

    if CORE.is_esp32:
        from esphome.components.esp32 import add_idf_component
        add_idf_component(name="espressif/esp_new_jpeg", ref="^1.0.2")



