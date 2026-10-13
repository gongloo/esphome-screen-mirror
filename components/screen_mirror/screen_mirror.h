#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/display/display.h"

#ifdef USE_TOUCHSCREEN
#include "esphome/components/touchscreen/touchscreen.h"
#endif

#ifdef USE_WEBSERVER
#include "esphome/components/web_server_base/web_server_base.h"
#if !USE_ESP32
#include <ESPAsyncWebServer.h>
#endif
#endif

namespace esphome {
namespace screen_mirror {

class ScreenMirrorComponent : public Component {
 public:
  ScreenMirrorComponent() = default;

  void set_display(display::Display *disp) { this->display_ = disp; }
#ifdef USE_TOUCHSCREEN
  void set_touchscreen(touchscreen::Touchscreen *touch) { this->touchscreen_ = touch; }
#endif
  void set_fps(float fps) { this->fps_ = fps; }
  void set_jpeg_quality(uint8_t quality) { this->jpeg_quality_ = quality; }
  void set_gated_streaming(bool gated) { this->gated_streaming_ = gated; }
  void set_web_path(const std::string &path) { this->web_path_ = path; }
  void set_show_telemetry(bool show) { this->show_telemetry_ = show; }
  void set_core(uint8_t core) { this->core_ = core; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Snapshot generation helpers
  bool capture_frame_rgb565(const uint8_t *&out_buffer, int &width, int &height);
  bool encode_jpeg(const uint8_t *rgb565_buffer, int width, int height, uint8_t **out_jpeg, size_t *out_size);
  bool build_bmp_header(int width, int height, uint8_t *header_54);

  // Simulated touch helper
  bool simulate_tap(int x, int y, uint16_t duration_ms = 50);

 protected:
  display::Display *display_{nullptr};
#ifdef USE_TOUCHSCREEN
  touchscreen::Touchscreen *touchscreen_{nullptr};
#endif
  float fps_{1.0f};
  uint8_t jpeg_quality_{75};
  bool gated_streaming_{true};
  std::string web_path_{"/remote"};
  bool show_telemetry_{true};
  uint8_t core_{0};

  // State tracking
  bool dirty_{true};
  uint32_t last_frame_time_{0};
  uint32_t frame_count_{0};
};

}  // namespace screen_mirror
}  // namespace esphome
