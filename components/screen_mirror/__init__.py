import logging
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import display, touchscreen
from esphome.components import web_server_base

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@gongloo"]
DEPENDENCIES = ["web_server_base"]

screen_mirror_ns = cg.esphome_ns.namespace("screen_mirror")
ScreenMirrorComponent = screen_mirror_ns.class_("ScreenMirrorComponent", cg.Component)

CONF_DISPLAY = "display"
CONF_TOUCHSCREEN = "touchscreen"
CONF_FPS = "fps"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_GATED_STREAMING = "gated_streaming"
CONF_WEB_PATH = "web_path"
CONF_SHOW_TELEMETRY = "show_telemetry"
CONF_CORE = "core"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ScreenMirrorComponent),
            cv.Required(CONF_DISPLAY): cv.use_id(display.Display),
            cv.Optional(CONF_TOUCHSCREEN): cv.use_id(touchscreen.Touchscreen),
            cv.Optional(CONF_FPS, default=1.0): cv.float_range(min=0.1, max=10.0),
            cv.Optional(CONF_JPEG_QUALITY, default=75): cv.int_range(min=10, max=100),
            cv.Optional(CONF_GATED_STREAMING, default=True): cv.boolean,
            cv.Optional(CONF_WEB_PATH, default="/remote"): cv.string,
            cv.Optional(CONF_SHOW_TELEMETRY, default=True): cv.boolean,
            cv.Optional(CONF_CORE, default=0): cv.int_range(min=0, max=1),
        }
    ).extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    disp = await cg.get_variable(config[CONF_DISPLAY])
    cg.add(var.set_display(disp))

    if CONF_TOUCHSCREEN in config:
        touch = await cg.get_variable(config[CONF_TOUCHSCREEN])
        cg.add(var.set_touchscreen(touch))

    cg.add(var.set_fps(config[CONF_FPS]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))
    cg.add(var.set_gated_streaming(config[CONF_GATED_STREAMING]))
    cg.add(var.set_web_path(config[CONF_WEB_PATH]))
    cg.add(var.set_show_telemetry(config[CONF_SHOW_TELEMETRY]))
    cg.add(var.set_core(config[CONF_CORE]))
    
    # Mark component as active in C++ preprocessor
    cg.add_define("USE_SCREEN_MIRROR")
