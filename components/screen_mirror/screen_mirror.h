#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/display/display.h"

#ifdef USE_TOUCHSCREEN
#include "esphome/components/touchscreen/touchscreen.h"
#endif

#include "esphome/components/web_server_base/web_server_base.h"
#include <vector>
#include <mutex>

namespace esphome {
namespace screen_mirror {

class ScreenMirrorComponent : public Component {
 public:
  ScreenMirrorComponent() = default;
  ~ScreenMirrorComponent() override;

  void set_display(display::Display *disp) { this->display_ = disp; }
#ifdef USE_TOUCHSCREEN
  void set_touchscreen(touchscreen::Touchscreen *touch) { this->touchscreen_ = touch; }
#endif
  void set_rotation(uint16_t rotation) { this->rotation_ = rotation; }
  uint16_t get_rotation() const { return this->rotation_; }
  void set_big_endian(bool big_endian) { this->big_endian_ = big_endian; }
  bool get_big_endian() const { return this->big_endian_; }
  void set_fps(float fps) { this->fps_ = fps; }
  void set_jpeg_quality(uint8_t quality) { this->jpeg_quality_ = quality; }
  void set_gated_streaming(bool gated) { this->gated_streaming_ = gated; }
  void set_web_path(const std::string &path) { this->web_path_ = path; }
  void set_show_telemetry(bool show) { this->show_telemetry_ = show; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Frame capture and encoding APIs
  bool capture_frame_rgb565(const uint8_t *&out_buffer, int &width, int &height);
  bool encode_jpeg(const uint8_t *rgb565_buffer, int width, int height, uint8_t *out_jpeg, size_t &out_size, size_t max_out_capacity = 0);
  bool build_bmp_header(int width, int height, uint8_t *header_66);

  // Input simulation APIs
  bool simulate_tap(int x, int y, uint16_t duration_ms = 180);
  bool simulate_touch(int x, int y, const std::string &action); // "down", "move", "up"
  bool simulate_swipe(int start_x, int start_y, int end_x, int end_y, uint16_t duration_ms = 300);
  void mark_dirty() { this->dirty_ = true; }

  // LVGL dirty tracking & WebSocket tile streaming
  void setup_lvgl_hooks();
  void add_dirty_area(int x1, int y1, int x2, int y2);
  void on_lvgl_render_ready();
  bool send_dirty_update();
  bool send_full_keyframe(int target_fd = -1);
  void add_ws_client(int fd);
  void remove_ws_client(int fd);
  bool send_ws_binary_packet(int fd, const uint8_t *payload, size_t len);

#if defined(USE_ESP32) && defined(CONFIG_HTTPD_WS_SUPPORT)
  static esp_err_t ws_handler(httpd_req_t *req);
#endif

  // Buffer accessors
  uint8_t *get_jpeg_buffer() { return this->jpeg_buffer_; }
  size_t get_jpeg_buffer_size() const { return this->jpeg_buffer_size_; }
  uint8_t *get_http_jpeg_buffer() { return this->http_jpeg_buffer_; }
  size_t get_http_jpeg_buffer_size() const { return this->http_jpeg_buffer_size_; }

 protected:
  void release_touch_();
  void simulate_touch_internal_(int x, int y, const std::string &action);

  display::Display *display_{nullptr};
#ifdef USE_TOUCHSCREEN
  touchscreen::Touchscreen *touchscreen_{nullptr};
#endif
  uint16_t rotation_{0};
  bool big_endian_{false};
  float fps_{1.0f};
  uint8_t jpeg_quality_{75};
  bool gated_streaming_{true};
  std::string web_path_{"/remote"};
  bool show_telemetry_{true};

  // Dedicated PSRAM buffer for WebSocket streaming encoding
  uint8_t *jpeg_buffer_{nullptr};
  size_t jpeg_buffer_size_{0};

  // Dedicated PSRAM buffer for HTTP snapshots to eliminate buffer contention with WS streaming
  uint8_t *http_jpeg_buffer_{nullptr};
  size_t http_jpeg_buffer_size_{0};

  // Dedicated tile extraction scratch buffer in PSRAM
  uint8_t *tile_scratch_{nullptr};
  size_t tile_scratch_size_{0};

  // Persistent JPEG encoder handle for full keyframes (fixed native width/height)
  void *keyframe_enc_handle_{nullptr};

  // Swipe gesture state
  struct ActiveSwipe {
    bool active{false};
    int start_x{0};
    int start_y{0};
    int end_x{0};
    int end_y{0};
    uint32_t start_time{0};
    uint32_t duration_ms{0};
    uint32_t last_step_time{0};
  } active_swipe_;

  // State tracking
  std::atomic<bool> dirty_{true};
  bool is_touch_down_{false};
  uint32_t last_touch_down_time_{0};
  int last_touch_x_{0};
  int last_touch_y_{0};
  uint32_t last_frame_time_{0};
  uint32_t frame_count_{0};

  // LVGL dirty tracking state
  bool lvgl_hooked_{false};
  std::mutex dirty_mutex_{};
  std::atomic<bool> has_dirty_rect_{false};
  int dirty_x1_{0};
  int dirty_y1_{0};
  int dirty_x2_{0};
  int dirty_y2_{0};

  // Connected WebSocket client tracking
  std::vector<int> ws_client_fds_{};
  std::mutex ws_mutex_{};
  std::mutex jpeg_mutex_{};
};

}  // namespace screen_mirror
}  // namespace esphome
