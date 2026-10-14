#include "screen_mirror.h"
#include "esphome/core/log.h"
#include <cstring>
#include <algorithm>

#include <esp_heap_caps.h>
#include <esp_jpeg_enc.h>

#if __has_include("esp_lcd_panel_rgb.h")
#include <esp_lcd_panel_rgb.h>
#endif

#if __has_include("esphome/components/mipi_rgb/mipi_rgb.h")
#include "esphome/components/mipi_rgb/mipi_rgb.h"
#endif

#if __has_include("esphome/components/display/display_buffer.h")
#include "esphome/components/display/display_buffer.h"
#endif

namespace esphome {
namespace screen_mirror {

static const char *const TAG = "screen_mirror";

#ifdef USE_TOUCHSCREEN
// Accessor to dispatch simulated touch events to protected touch_listeners_
class TouchscreenAccess : public touchscreen::Touchscreen {
 public:
  static const std::vector<touchscreen::TouchListener *> &get_listeners(touchscreen::Touchscreen *ts) {
    return static_cast<TouchscreenAccess *>(ts)->touch_listeners_;
  }
};
#endif

#if __has_include("remote_html.h")
#include "remote_html.h"
#endif

void ScreenMirrorComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Screen Mirror component...");

  int w = (this->display_ != nullptr) ? this->display_->get_width() : 800;
  int h = (this->display_ != nullptr) ? this->display_->get_height() : 480;

  this->jpeg_buffer_size_ = 256 * 1024; // 256 KB buffer for compressed JPEG
  this->jpeg_buffer_ = (uint8_t *) heap_caps_malloc(this->jpeg_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->jpeg_buffer_ == nullptr) {
    this->jpeg_buffer_ = (uint8_t *) malloc(this->jpeg_buffer_size_);
  }

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG working buffer!");
  } else {
    ESP_LOGI(TAG, "Allocated %u KB JPEG working buffer in PSRAM", (unsigned)(this->jpeg_buffer_size_ / 1024));
  }

  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGW(TAG, "No web_server_base registered; remote endpoints will be unavailable.");
    return;
  }

  // 1. JPEG Snapshot Handler (/screenshot.jpg)
  class JpegSnapshotHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    JpegSnapshotHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      auto url = request->url_to(url_buf);
      return (url == "/screenshot.jpg" || url == "/screenshot.jpeg");
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      const uint8_t *raw_buf = nullptr;
      int width = 0, height = 0;
      if (!this->parent_->capture_frame_rgb565(raw_buf, width, height) || raw_buf == nullptr) {
        request->send(503, "text/plain", "Display buffer not ready");
        return;
      }

      uint8_t *out_jpeg = this->parent_->get_jpeg_buffer();
      size_t out_size = 0;
      if (!this->parent_->encode_jpeg(raw_buf, width, height, out_jpeg, out_size) || out_size == 0) {
        request->send(500, "text/plain", "JPEG encoding failed");
        return;
      }

      AsyncWebServerResponse *response = request->beginResponse(200, "image/jpeg", out_jpeg, out_size);
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // 2. BMP Snapshot Handler (/screenshot.bmp)
  class BmpSnapshotHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    BmpSnapshotHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      return request->url_to(url_buf) == "/screenshot.bmp";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      const uint8_t *buffer = nullptr;
      int width = 0, height = 0;
      if (!this->parent_->capture_frame_rgb565(buffer, width, height) || buffer == nullptr) {
        request->send(503, "text/plain", "Display buffer not ready");
        return;
      }

      uint8_t header[54];
      this->parent_->build_bmp_header(width, height, header);
      size_t image_bytes = width * height * 2;

      httpd_resp_set_type(*request, "image/bmp");
      httpd_resp_set_hdr(*request, "Cache-Control", "no-cache, no-store, must-revalidate");
      httpd_resp_set_hdr(*request, "Access-Control-Allow-Origin", "*");
      httpd_resp_send_chunk(*request, reinterpret_cast<const char *>(header), sizeof(header));
      httpd_resp_send_chunk(*request, reinterpret_cast<const char *>(buffer), image_bytes);
      httpd_resp_send_chunk(*request, nullptr, 0);
    }
  };

  // 3. Info / Metadata Handler (/mirror/info)
  class MirrorInfoHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    MirrorInfoHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      return request->url_to(url_buf) == "/mirror/info";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      char json[256];
      int w = (this->parent_->display_ != nullptr) ? this->parent_->display_->get_width() : 0;
      int h = (this->parent_->display_ != nullptr) ? this->parent_->display_->get_height() : 0;
      snprintf(json, sizeof(json),
               "{\"width\":%d,\"height\":%d,\"rotation\":%u,\"fps\":%.1f,\"jpeg_quality\":%d,\"gated\":%s}",
               w, h, this->parent_->rotation_, this->parent_->fps_, this->parent_->jpeg_quality_,
               this->parent_->gated_streaming_ ? "true" : "false");
      AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // 4. Touch Tap Handler (/tap?x=...&y=...)
  class TapHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    TapHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_POST && request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      return request->url_to(url_buf) == "/tap";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      if (!request->hasParam("x") || !request->hasParam("y")) {
        request->send(400, "application/json", "{\"error\":\"Missing x or y parameters\"}");
        return;
      }
      int x = std::atoi(request->getParam("x")->value().c_str());
      int y = std::atoi(request->getParam("y")->value().c_str());
      uint16_t duration = 50;
      if (request->hasParam("duration")) {
        duration = static_cast<uint16_t>(std::atoi(request->getParam("duration")->value().c_str()));
      }

      bool success = this->parent_->simulate_tap(x, y, duration);
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":\"%s\",\"x\":%d,\"y\":%d}", success ? "ok" : "failed", x, y);
      AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500, "application/json", resp);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // 5. Continuous Touch Handler (/touch?x=...&y=...&action=down|move|up)
  class TouchHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    TouchHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_POST && request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      return request->url_to(url_buf) == "/touch";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      if (!request->hasParam("action")) {
        request->send(400, "application/json", "{\"error\":\"Missing action parameter (down, move, up)\"}");
        return;
      }
      std::string action = request->getParam("action")->value();
      int x = 0, y = 0;
      if (action != "up") {
        if (!request->hasParam("x") || !request->hasParam("y")) {
          request->send(400, "application/json", "{\"error\":\"Missing x or y parameters\"}");
          return;
        }
        x = std::atoi(request->getParam("x")->value().c_str());
        y = std::atoi(request->getParam("y")->value().c_str());
      }

      bool success = this->parent_->simulate_touch(x, y, action);
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":\"%s\",\"action\":\"%s\",\"x\":%d,\"y\":%d}",
               success ? "ok" : "failed", action.c_str(), x, y);
      AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500, "application/json", resp);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // 6. Swipe Gesture Handler (/swipe?start_x=...&start_y=...&end_x=...&end_y=...&duration=...)
  class SwipeHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    SwipeHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_POST && request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      return request->url_to(url_buf) == "/swipe";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      if (!request->hasParam("start_x") || !request->hasParam("start_y") ||
          !request->hasParam("end_x") || !request->hasParam("end_y")) {
        request->send(400, "application/json",
                      "{\"error\":\"Missing start_x, start_y, end_x, or end_y parameters\"}");
        return;
      }
      int start_x = std::atoi(request->getParam("start_x")->value().c_str());
      int start_y = std::atoi(request->getParam("start_y")->value().c_str());
      int end_x = std::atoi(request->getParam("end_x")->value().c_str());
      int end_y = std::atoi(request->getParam("end_y")->value().c_str());
      uint16_t duration_ms = 300;
      if (request->hasParam("duration")) {
        duration_ms = static_cast<uint16_t>(std::atoi(request->getParam("duration")->value().c_str()));
      }

      bool success = this->parent_->simulate_swipe(start_x, start_y, end_x, end_y, duration_ms);
      char resp[128];
      snprintf(resp, sizeof(resp),
               "{\"status\":\"%s\",\"start_x\":%d,\"start_y\":%d,\"end_x\":%d,\"end_y\":%d,\"duration\":%u}",
               success ? "ok" : "failed", start_x, start_y, end_x, end_y, duration_ms);
      AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500, "application/json", resp);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // 7. Embedded Web Mirror UI (/remote)
  class RemoteUiHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    RemoteUiHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      if (request->method() != HTTP_GET)
        return false;
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      return request->url_to(url_buf) == this->parent_->web_path_;
    }
    void handleRequest(AsyncWebServerRequest *request) override {
#if defined(REMOTE_HTML_GZ) || __has_include("remote_html.h")
      AsyncWebServerResponse *response = request->beginResponse(
          200, "text/html", REMOTE_HTML_GZ, REMOTE_HTML_GZ_SIZE);
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
#else
      request->send(404, "text/plain", "Remote UI asset not found");
#endif
    }
  };

  // Register all endpoints
  web_server_base::global_web_server_base->add_handler(new JpegSnapshotHandler(this));
  web_server_base::global_web_server_base->add_handler(new BmpSnapshotHandler(this));
  web_server_base::global_web_server_base->add_handler(new MirrorInfoHandler(this));
  web_server_base::global_web_server_base->add_handler(new TapHandler(this));
  web_server_base::global_web_server_base->add_handler(new TouchHandler(this));
  web_server_base::global_web_server_base->add_handler(new SwipeHandler(this));
  web_server_base::global_web_server_base->add_handler(new RemoteUiHandler(this));

  // Initialize web_server_base to ensure the HTTP server is listening on port 80
  web_server_base::global_web_server_base->init();

  ESP_LOGI(TAG, "Screen mirror endpoints active at: %s, /screenshot.jpg, /screenshot.bmp, /tap, /touch, /swipe",
           this->web_path_.c_str());
}

void ScreenMirrorComponent::loop() {
#ifdef USE_TOUCHSCREEN
  if (this->active_swipe_.active && this->touchscreen_ != nullptr) {
    uint32_t now = millis();
    // Step swipe interpolation every 15-20 ms
    if (now - this->active_swipe_.last_step_time >= 16) {
      this->active_swipe_.last_step_time = now;
      uint32_t elapsed = now - this->active_swipe_.start_time;

      if (elapsed >= this->active_swipe_.duration_ms) {
        // Final coordinate
        this->simulate_touch(this->active_swipe_.end_x, this->active_swipe_.end_y, "move");
        // Release touch
        this->simulate_touch(this->active_swipe_.end_x, this->active_swipe_.end_y, "up");
        this->active_swipe_.active = false;
        ESP_LOGD(TAG, "Swipe completed at (%d, %d)", this->active_swipe_.end_x, this->active_swipe_.end_y);
      } else {
        float progress = (float) elapsed / (float) this->active_swipe_.duration_ms;
        int current_x = this->active_swipe_.start_x + (int) ((this->active_swipe_.end_x - this->active_swipe_.start_x) * progress);
        int current_y = this->active_swipe_.start_y + (int) ((this->active_swipe_.end_y - this->active_swipe_.start_y) * progress);
        this->simulate_touch(current_x, current_y, "move");
      }
    }
  }
#endif
}

void ScreenMirrorComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Screen Mirror:");
  ESP_LOGCONFIG(TAG, "  Rotation: %u°", this->rotation_);
  ESP_LOGCONFIG(TAG, "  Byte Order: %s", this->big_endian_ ? "Big-Endian" : "Little-Endian");
  ESP_LOGCONFIG(TAG, "  FPS Target: %.1f", this->fps_);
  ESP_LOGCONFIG(TAG, "  JPEG Quality: %u", this->jpeg_quality_);
  ESP_LOGCONFIG(TAG, "  Gated Streaming: %s", YESNO(this->gated_streaming_));
  ESP_LOGCONFIG(TAG, "  Web Path: %s", this->web_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Core: %u", this->core_);
}

bool ScreenMirrorComponent::capture_frame_rgb565(const uint8_t *&out_buffer, int &width, int &height) {
  if (this->display_ == nullptr)
    return false;

  width = this->display_->get_width();
  height = this->display_->get_height();

#if __has_include("esphome/components/mipi_rgb/mipi_rgb.h") && __has_include("esp_lcd_panel_rgb.h")
  // For MIPI RGB panels, read the active hardware PSRAM scanout framebuffer
  class MipiRgbAccessor : public esphome::mipi_rgb::MipiRgb {
   public:
    esp_lcd_panel_handle_t get_panel_handle() { return this->handle_; }
    uint16_t *get_buffer() { return this->buffer_; }
  };
  auto *mipi = reinterpret_cast<MipiRgbAccessor *>(this->display_);
  if (mipi != nullptr && mipi->get_panel_handle() != nullptr) {
    void *fb = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(mipi->get_panel_handle(), 1, &fb) == ESP_OK && fb != nullptr) {
      out_buffer = reinterpret_cast<const uint8_t *>(fb);
      return true;
    }
  }
  if (mipi != nullptr && mipi->get_buffer() != nullptr) {
    out_buffer = reinterpret_cast<const uint8_t *>(mipi->get_buffer());
    return true;
  }
#endif

  return false;
}

bool ScreenMirrorComponent::encode_jpeg(const uint8_t *rgb565_buffer, int width, int height,
                                        uint8_t *out_jpeg, size_t &out_size) {
  if (rgb565_buffer == nullptr || out_jpeg == nullptr || width <= 0 || height <= 0)
    return false;

  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = width;
  cfg.height = height;
  cfg.src_type = this->big_endian_ ? JPEG_PIXEL_FORMAT_RGB565_BE : JPEG_PIXEL_FORMAT_RGB565_LE;
  cfg.subsampling = JPEG_SUBSAMPLE_420;
  cfg.quality = this->jpeg_quality_;
  cfg.task_enable = false; // Mono-task mode: runs synchronously on current thread, ~10KB DRAM

  jpeg_enc_handle_t enc_handle = nullptr;
  jpeg_error_t open_err = jpeg_enc_open(&cfg, &enc_handle);
  if (open_err != JPEG_ERR_OK || enc_handle == nullptr) {
    ESP_LOGE(TAG, "jpeg_enc_open failed: %d", open_err);
    return false;
  }

  int in_size = width * height * 2;
  int out_len = 0;
  jpeg_error_t err = jpeg_enc_process(enc_handle, (uint8_t *) rgb565_buffer, in_size,
                                      out_jpeg, (int) this->jpeg_buffer_size_, &out_len);
  jpeg_enc_close(enc_handle);

  if (err != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_enc_process failed: %d", err);
    return false;
  }

  out_size = (size_t) out_len;
  return (out_size > 0);
}

bool ScreenMirrorComponent::build_bmp_header(int width, int height, uint8_t *header_54) {
  std::memset(header_54, 0, 54);
  uint32_t image_size = width * height * 2;
  uint32_t file_size = 54 + image_size;

  // Bitmap File Header (14 bytes)
  header_54[0] = 'B';
  header_54[1] = 'M';
  header_54[2] = (uint8_t)(file_size);
  header_54[3] = (uint8_t)(file_size >> 8);
  header_54[4] = (uint8_t)(file_size >> 16);
  header_54[5] = (uint8_t)(file_size >> 24);
  header_54[10] = 54; // Data offset

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  header_54[14] = 40;
  header_54[18] = (uint8_t)(width);
  header_54[19] = (uint8_t)(width >> 8);
  header_54[20] = (uint8_t)(width >> 16);
  header_54[21] = (uint8_t)(width >> 24);
  // Negative height specifies top-down bitmap (origin upper-left)
  int32_t neg_height = -height;
  header_54[22] = (uint8_t)(neg_height);
  header_54[23] = (uint8_t)(neg_height >> 8);
  header_54[24] = (uint8_t)(neg_height >> 16);
  header_54[25] = (uint8_t)(neg_height >> 24);
  header_54[26] = 1;  // Planes
  header_54[28] = 16; // Bits per pixel (RGB565)
  header_54[30] = 0;  // BI_RGB compression
  header_54[34] = (uint8_t)(image_size);
  header_54[35] = (uint8_t)(image_size >> 8);
  header_54[36] = (uint8_t)(image_size >> 16);
  header_54[37] = (uint8_t)(image_size >> 24);

  return true;
}

bool ScreenMirrorComponent::simulate_tap(int x, int y, uint16_t duration_ms) {
  ESP_LOGD(TAG, "Simulating tap at (%d, %d) for %u ms", x, y, duration_ms);

#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ != nullptr) {
    // 1. Send touch down
    this->simulate_touch(x, y, "down");

    // 2. Schedule release after duration_ms
    this->set_timeout("release_tap", duration_ms, [this, x, y]() {
      this->simulate_touch(x, y, "up");
    });
    return true;
  }
#endif

  ESP_LOGW(TAG, "Cannot simulate tap: no touchscreen component configured under screen_mirror");
  return false;
}

bool ScreenMirrorComponent::simulate_touch(int x, int y, const std::string &action) {
#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ == nullptr) {
    ESP_LOGW(TAG, "Cannot simulate touch: no touchscreen configured");
    return false;
  }

  if (action == "down") {
    touchscreen::TouchPoint tp{};
    tp.id = 0;
    tp.state = touchscreen::STATE_PRESSED;
    tp.x = x;
    tp.y = y;
    tp.x_raw = x;
    tp.y_raw = y;

    // Fire on_touch trigger (e.g. wakes screen from sleep)
    this->touchscreen_->get_touch_trigger()->trigger(tp, {tp});

    // Notify all registered listeners (LVGL)
    for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
      listener->touch(tp);
      listener->update({tp});
    }
    this->is_touch_down_ = true;
    this->mark_dirty();
    return true;

  } else if (action == "move") {
    touchscreen::TouchPoint tp{};
    tp.id = 0;
    tp.state = touchscreen::STATE_UPDATED;
    tp.x = x;
    tp.y = y;
    tp.x_raw = x;
    tp.y_raw = y;

    // Fire update trigger
    this->touchscreen_->get_update_trigger()->trigger({tp});

    // Notify listeners of coordinate movement
    for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
      listener->update({tp});
    }
    this->is_touch_down_ = true;
    this->mark_dirty();
    return true;

  } else if (action == "up") {
    // Release touch
    for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
      listener->release();
    }
    this->touchscreen_->get_release_trigger()->trigger();
    this->is_touch_down_ = false;
    this->mark_dirty();
    return true;
  }

  ESP_LOGW(TAG, "Invalid touch action: %s (expected 'down', 'move', or 'up')", action.c_str());
  return false;
#else
  return false;
#endif
}

bool ScreenMirrorComponent::simulate_swipe(int start_x, int start_y, int end_x, int end_y, uint16_t duration_ms) {
#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ == nullptr) {
    ESP_LOGW(TAG, "Cannot simulate swipe: no touchscreen configured");
    return false;
  }

  ESP_LOGD(TAG, "Starting swipe from (%d, %d) to (%d, %d) over %u ms",
           start_x, start_y, end_x, end_y, duration_ms);

  // 1. Initial touch down at start coordinates
  this->simulate_touch(start_x, start_y, "down");

  // 2. Initialize active swipe state for loop() interpolation
  this->active_swipe_.active = true;
  this->active_swipe_.start_x = start_x;
  this->active_swipe_.start_y = start_y;
  this->active_swipe_.end_x = end_x;
  this->active_swipe_.end_y = end_y;
  this->active_swipe_.start_time = millis();
  this->active_swipe_.duration_ms = duration_ms;
  this->active_swipe_.last_step_time = millis();

  return true;
#else
  return false;
#endif
}

}  // namespace screen_mirror
}  // namespace esphome
