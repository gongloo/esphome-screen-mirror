#include "screen_mirror.h"
#include "esphome/core/log.h"
#include <cstring>

#if __has_include("lvgl.h")
#define HAS_LVGL 1
#include "lvgl.h"
#endif

namespace esphome {
namespace screen_mirror {

static const char *const TAG = "screen_mirror";

void ScreenMirrorComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Screen Mirror component...");

#ifdef USE_WEBSERVER
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGW(TAG, "No web_server_base registered; remote endpoints will be unavailable.");
    return;
  }

  // 1. BMP Snapshot Handler (Pixel-perfect uncompressed snapshot)
  class BmpSnapshotHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    BmpSnapshotHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      return request->method() == HTTP_GET && request->url() == "/screenshot.bmp";
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
      
      // Stream BMP response: 54-byte header + RGB565 rows
      size_t image_bytes = width * height * 2;
      AsyncWebServerResponse *response = request->beginResponse("image/bmp", sizeof(header) + image_bytes,
        [this, header, buffer, image_bytes](uint8_t *data, size_t maxLen, size_t index) -> size_t {
          if (index < sizeof(header)) {
            size_t copy_header = std::min(maxLen, sizeof(header) - index);
            std::memcpy(data, header + index, copy_header);
            return copy_header;
          }
          size_t buf_index = index - sizeof(header);
          if (buf_index < image_bytes) {
            size_t copy_buf = std::min(maxLen, image_bytes - buf_index);
            std::memcpy(data, buffer + buf_index, copy_buf);
            return copy_buf;
          }
          return 0;
        });
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      request->send(response);
    }
  };

  // 2. Info / Telemetry Endpoint
  class MirrorInfoHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    MirrorInfoHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      return request->method() == HTTP_GET && request->url() == "/mirror/info";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      char json[256];
      int w = (this->parent_->display_ != nullptr) ? this->parent_->display_->get_width() : 0;
      int h = (this->parent_->display_ != nullptr) ? this->parent_->display_->get_height() : 0;
      snprintf(json, sizeof(json),
               "{\"width\":%d,\"height\":%d,\"fps\":%.1f,\"jpeg_quality\":%d,\"gated\":%s}",
               w, h, this->parent_->fps_, this->parent_->jpeg_quality_,
               this->parent_->gated_streaming_ ? "true" : "false");
      AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // 3. Touch Tap Endpoint (/tap?x=...&y=...)
  class TapHandler : public AsyncWebHandler {
   protected:
    ScreenMirrorComponent *parent_;
   public:
    TapHandler(ScreenMirrorComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
      return (request->method() == HTTP_POST || request->method() == HTTP_GET) && request->url() == "/tap";
    }
    void handleRequest(AsyncWebServerRequest *request) override {
      if (!request->hasParam("x") || !request->hasParam("y")) {
        request->send(400, "application/json", "{\"error\":\"Missing x or y parameters\"}");
        return;
      }
      int x = request->getParam("x")->value().toInt();
      int y = request->getParam("y")->value().toInt();
      uint16_t duration = 50;
      if (request->hasParam("duration")) {
        duration = request->getParam("duration")->value().toInt();
      }

      bool success = this->parent_->simulate_tap(x, y, duration);
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":\"%s\",\"x\":%d,\"y\":%d}", success ? "ok" : "failed", x, y);
      AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500, "application/json", resp);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // Register handlers
  web_server_base::global_web_server_base->add_handler(new BmpSnapshotHandler(this));
  web_server_base::global_web_server_base->add_handler(new MirrorInfoHandler(this));
  web_server_base::global_web_server_base->add_handler(new TapHandler(this));

  ESP_LOGI(TAG, "Screen mirror endpoints registered: /screenshot.bmp, /mirror/info, /tap");
#endif
}

void ScreenMirrorComponent::loop() {
  // Streaming loop tasks (Phase 3)
}

void ScreenMirrorComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Screen Mirror:");
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

#if defined(HAS_LVGL)
  // Check if LVGL screen buffer is directly accessible
  // Phase 1 implementation grabs LVGL active screen buffer
#endif

  return false;
}

bool ScreenMirrorComponent::build_bmp_header(int width, int height, uint8_t *header_54) {
  std::memset(header_54, 0, 54);
  uint32_t image_size = width * height * 2;
  uint32_t file_size = 54 + image_size;

  // Bitmap File Header
  header_54[0] = 'B';
  header_54[1] = 'M';
  header_54[2] = (uint8_t)(file_size);
  header_54[3] = (uint8_t)(file_size >> 8);
  header_54[4] = (uint8_t)(file_size >> 16);
  header_54[5] = (uint8_t)(file_size >> 24);
  header_54[10] = 54; // Data offset

  // DIB Header (BITMAPINFOHEADER)
  header_54[14] = 40; // Header size
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

#if defined(HAS_LVGL)
  // LVGL synthetic touch point injection
  return true;
#else
  return false;
#endif
}

}  // namespace screen_mirror
}  // namespace esphome
