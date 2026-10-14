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
    #screenCanvas { display: block; max-width: 90vw; max-height: 80vh; width: auto; height: auto; cursor: crosshair; }
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
    <div class="badge">Orientation: <span id="rot">0°</span></div>
    <div class="badge">Latency: <span id="lat">-</span> ms</div>
    <div class="badge">Status: <span id="stat" style="color: #10b981">Connected</span></div>
  </div>
  <div class="screen-container" id="container">
    <canvas id="screenCanvas"></canvas>
  </div>
  <div class="status">Click or tap anywhere on the screen to simulate touch</div>
  <div class="btn-group">
    <button onclick="refreshScreen(true)">Snapshot (Refresh)</button>
    <button onclick="window.open('/screenshot.jpg', '_blank')">Full JPEG</button>
    <button onclick="window.open('/screenshot.bmp', '_blank')">Raw BMP</button>
  </div>
  <script>
    const canvas = document.getElementById('screenCanvas');
    const ctx = canvas.getContext('2d');
    const container = document.getElementById('container');
    const latSpan = document.getElementById('lat');
    const resSpan = document.getElementById('res');
    const rotSpan = document.getElementById('rot');

    let isRefreshing = false;
    let rotation = 0;
    let hwWidth = 800;
    let hwHeight = 480;

    async function updateInfo() {
      try {
        const res = await fetch('/mirror/info');
        const data = await res.json();
        hwWidth = data.width || 800;
        hwHeight = data.height || 480;
        rotation = data.rotation || 0;
        rotSpan.textContent = `${rotation}°`;

        if (rotation === 90 || rotation === 270) {
          resSpan.textContent = `${hwHeight}×${hwWidth}`;
        } else {
          resSpan.textContent = `${hwWidth}×${hwHeight}`;
        }
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
        latSpan.textContent = Math.round(performance.now() - start);
        drawRotated(img);
        isRefreshing = false;
      };
      img.onerror = () => { isRefreshing = false; };
    }

    function drawRotated(img) {
      const w = img.naturalWidth || hwWidth;
      const h = img.naturalHeight || hwHeight;

      if (rotation === 90 || rotation === 270) {
        if (canvas.width !== h || canvas.height !== w) {
          canvas.width = h;
          canvas.height = w;
        }
      } else {
        if (canvas.width !== w || canvas.height !== h) {
          canvas.width = w;
          canvas.height = h;
        }
      }

      ctx.save();
      if (rotation === 90) {
        // 90 deg CCW (270 deg) brings the buffer upright
        ctx.translate(0, canvas.height);
        ctx.rotate(270 * Math.PI / 180);
      } else if (rotation === 180) {
        ctx.translate(canvas.width, canvas.height);
        ctx.rotate(180 * Math.PI / 180);
      } else if (rotation === 270) {
        ctx.translate(canvas.width, 0);
        ctx.rotate(90 * Math.PI / 180);
      }
      ctx.drawImage(img, 0, 0);
      ctx.restore();
    }

    canvas.addEventListener('click', async (e) => {
      const rect = canvas.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      const clickY = e.clientY - rect.top;

      // Coordinate in the visible canvas space
      const dispX = Math.round(clickX * (canvas.width / rect.width));
      const dispY = Math.round(clickY * (canvas.height / rect.height));

      // Map from rotated canvas coordinates back to raw unrotated hardware framebuffer coordinates
      let rawX, rawY;
      if (rotation === 90) {
        rawX = dispY;
        rawY = hwHeight - dispX - 1;
      } else if (rotation === 180) {
        rawX = hwWidth - dispX - 1;
        rawY = hwHeight - dispY - 1;
      } else if (rotation === 270) {
        rawX = hwWidth - dispY - 1;
        rawY = dispX;
      } else {
        rawX = dispX;
        rawY = dispY;
      }

      // Constrain within bounds
      rawX = Math.max(0, Math.min(hwWidth - 1, Math.round(rawX)));
      rawY = Math.max(0, Math.min(hwHeight - 1, Math.round(rawY)));

      // Visual feedback ripple
      const ripple = document.createElement('div');
      ripple.className = 'ripple';
      ripple.style.left = clickX + 'px';
      ripple.style.top = clickY + 'px';
      container.appendChild(ripple);
      setTimeout(() => ripple.remove(), 400);

      try {
        await fetch(`/tap?x=${rawX}&y=${rawY}`, { method: 'POST', body: '' });
        setTimeout(() => refreshScreen(true), 120);
      } catch (err) {
        console.error(err);
      }
    });

    refreshScreen(true);
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
  ESP_LOGCONFIG(TAG, "  Rotation: %u°", this->rotation_);
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

#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ != nullptr) {
    touchscreen::TouchPoint tp{};
    tp.id = 0;
    tp.state = touchscreen::STATE_PRESSED;
    tp.x = x;
    tp.y = y;
    tp.x_raw = x;
    tp.y_raw = y;

    // 1. Fire on_touch trigger (wakes display / cancels sleep in YAML)
    this->touchscreen_->get_touch_trigger()->trigger(tp, {tp});

    // 2. Dispatch touch down to all registered listeners (e.g. LVTouchListener)
    for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
      listener->touch(tp);
      listener->update({tp});
    }

    // 3. Schedule release after duration_ms
    this->set_timeout("release_tap", duration_ms, [this]() {
      for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
        listener->release();
      }
      this->touchscreen_->get_release_trigger()->trigger();
    });
    return true;
  }
#endif

  ESP_LOGW(TAG, "Cannot simulate tap: no touchscreen component configured under screen_mirror");
  return false;
}

}  // namespace screen_mirror
}  // namespace esphome
