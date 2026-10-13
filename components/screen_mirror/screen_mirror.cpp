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

#if __has_include("lvgl.h")
#define HAS_LVGL 1
#include "lvgl.h"
#endif

#if __has_include("esphome/components/display/display_buffer.h")
#include "esphome/components/display/display_buffer.h"
#endif

namespace esphome {
namespace screen_mirror {

static const char *const TAG = "screen_mirror";

#if defined(HAS_LVGL)
// Static state for virtual touch indev driver
static lv_indev_t *s_virtual_indev = nullptr;
static int s_tap_x = 0;
static int s_tap_y = 0;
static bool s_is_pressed = false;

static void virtual_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  data->point.x = s_tap_x;
  data->point.y = s_tap_y;
  data->state = s_is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
#endif

// HTML5 Interactive Web Mirror
static const char REMOTE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESPHome Screen Mirror</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0; background: #070e1b; color: #f1f5f9;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      display: flex; flex-direction: column; align-items: center; justify-content: center;
      min-height: 100vh; padding: 16px;
    }
    .header { margin-bottom: 14px; display: flex; gap: 12px; align-items: center; flex-wrap: wrap; justify-content: center; }
    .badge {
      background: #0f1929; border: 1px solid #1e293b; padding: 6px 12px; border-radius: 8px;
      font-size: 13px; font-weight: 500; color: #94a3b8;
    }
    .badge span { color: #38bdf8; font-weight: 600; }
    .screen-container {
      position: relative; display: inline-block;
      box-shadow: 0 20px 30px -10px rgba(0,0,0,0.6), 0 10px 15px -5px rgba(0,0,0,0.4);
      border-radius: 12px; overflow: hidden; border: 2px solid #1e293b; background: #000;
    }
    #screen { display: block; max-width: 90vw; max-height: 80vh; width: auto; height: auto; cursor: crosshair; }
    .ripple {
      position: absolute; width: 28px; height: 28px; background: rgba(56, 189, 248, 0.5);
      border: 2px solid #fff; border-radius: 50%; transform: translate(-50%, -50%) scale(0);
      animation: ripple-anim 0.35s ease-out forwards; pointer-events: none;
    }
    @keyframes ripple-anim { to { transform: translate(-50%, -50%) scale(2.0); opacity: 0; } }
    .status { margin-top: 14px; font-size: 13px; color: #64748b; }
    .btn-group { margin-top: 10px; display: flex; gap: 8px; }
    button {
      background: #1e293b; border: 1px solid #334155; color: #cbd5e1;
      padding: 6px 12px; border-radius: 6px; font-size: 12px; cursor: pointer;
      transition: background 0.15s;
    }
    button:hover { background: #334155; color: #fff; }
  </style>
</head>
<body>
  <div class="header">
    <div class="badge">Device: <span id="res">-</span></div>
    <div class="badge">Latency: <span id="lat">-</span> ms</div>
    <div class="badge">Status: <span id="stat" style="color: #10b981">Connected</span></div>
  </div>
  <div class="screen-container" id="container">
    <img id="screen" src="/screenshot.jpg" alt="Screen Mirror">
  </div>
  <div class="status">Click or tap anywhere on the screen to simulate touch</div>
  <div class="btn-group">
    <button onclick="refreshScreen(true)">Snapshot (Refresh)</button>
    <button onclick="window.open('/screenshot.jpg', '_blank')">Full JPEG</button>
    <button onclick="window.open('/screenshot.bmp', '_blank')">Raw BMP</button>
  </div>
  <script>
    const screen = document.getElementById('screen');
    const container = document.getElementById('container');
    const latSpan = document.getElementById('lat');
    const resSpan = document.getElementById('res');
    let isRefreshing = false;

    async function updateInfo() {
      try {
        const res = await fetch('/mirror/info');
        const data = await res.json();
        resSpan.textContent = `${data.width}×${data.height}`;
      } catch (e) {}
    }
    updateInfo();

    async function refreshScreen(manual = false) {
      if (isRefreshing && !manual) return;
      isRefreshing = true;
      const start = performance.now();
      const img = new Image();
      img.src = '/screenshot.jpg?t=' + Date.now();
      img.onload = () => {
        screen.src = img.src;
        latSpan.textContent = Math.round(performance.now() - start);
        isRefreshing = false;
      };
      img.onerror = () => { isRefreshing = false; };
    }

    screen.addEventListener('click', async (e) => {
      const rect = screen.getBoundingClientRect();
      const nativeW = screen.naturalWidth || 800;
      const nativeH = screen.naturalHeight || 480;
      const clickX = e.clientX - rect.left;
      const clickY = e.clientY - rect.top;
      const x = Math.round(clickX * (nativeW / rect.width));
      const y = Math.round(clickY * (nativeH / rect.height));

      // Visual feedback ripple
      const ripple = document.createElement('div');
      ripple.className = 'ripple';
      ripple.style.left = clickX + 'px';
      ripple.style.top = clickY + 'px';
      container.appendChild(ripple);
      setTimeout(() => ripple.remove(), 400);

      try {
        await fetch(`/tap?x=${x}&y=${y}`, { method: 'POST', body: '' });
        setTimeout(() => refreshScreen(true), 120);
      } catch (err) {
        console.error(err);
      }
    });

    setInterval(() => refreshScreen(false), 1000);
  </script>
</body>
</html>)rawliteral";

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

#if defined(HAS_LVGL)
  // Register virtual touch indev in LVGL 9
  s_virtual_indev = lv_indev_create();
  if (s_virtual_indev != nullptr) {
    lv_indev_set_type(s_virtual_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_virtual_indev, virtual_touch_read_cb);
    ESP_LOGI(TAG, "LVGL 9 virtual touch input device registered successfully");
  } else {
    ESP_LOGW(TAG, "Failed to create LVGL virtual input device");
  }
#endif

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
               "{\"width\":%d,\"height\":%d,\"fps\":%.1f,\"jpeg_quality\":%d,\"gated\":%s}",
               w, h, this->parent_->fps_, this->parent_->jpeg_quality_,
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

  // 5. Embedded Web Mirror UI (/remote)
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
      AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const uint8_t *)REMOTE_HTML, sizeof(REMOTE_HTML) - 1);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  };

  // Register all endpoints
  web_server_base::global_web_server_base->add_handler(new JpegSnapshotHandler(this));
  web_server_base::global_web_server_base->add_handler(new BmpSnapshotHandler(this));
  web_server_base::global_web_server_base->add_handler(new MirrorInfoHandler(this));
  web_server_base::global_web_server_base->add_handler(new TapHandler(this));
  web_server_base::global_web_server_base->add_handler(new RemoteUiHandler(this));

  // Initialize web_server_base to ensure the HTTP server is listening on port 80
  web_server_base::global_web_server_base->init();

  ESP_LOGI(TAG, "Screen mirror endpoints active at: %s, /screenshot.jpg, /screenshot.bmp, /tap",
           this->web_path_.c_str());
}

void ScreenMirrorComponent::loop() {
  // Reserved for Phase 3 streaming event loop
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
  cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
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

#if defined(HAS_LVGL)
  s_tap_x = x;
  s_tap_y = y;
  s_is_pressed = true;

  this->set_timeout("release_tap", duration_ms, []() {
    s_is_pressed = false;
  });
  return true;
#else
  return false;
#endif
}

}  // namespace screen_mirror
}  // namespace esphome
