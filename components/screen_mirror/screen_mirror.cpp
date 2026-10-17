#include "screen_mirror.h"
#include "esphome/core/log.h"
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <unistd.h>

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

#if __has_include(<lvgl.h>)
#include <lvgl.h>
#define HAS_LVGL 1
#elif defined(USE_LVGL)
#include <lvgl.h>
#define HAS_LVGL 1
#endif

namespace esphome {
namespace screen_mirror {

static const char *const TAG = "screen_mirror";
static ScreenMirrorComponent *s_mirror_instance = nullptr;

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

ScreenMirrorComponent::~ScreenMirrorComponent() {
  this->release_keyframe_encoder_();
  if (s_mirror_instance == this) {
    s_mirror_instance = nullptr;
  }
  if (this->jpeg_buffer_ != nullptr) {
    free(this->jpeg_buffer_);
    this->jpeg_buffer_ = nullptr;
  }
  if (this->http_jpeg_buffer_ != nullptr) {
    free(this->http_jpeg_buffer_);
    this->http_jpeg_buffer_ = nullptr;
  }
  if (this->tile_scratch_ != nullptr) {
    free(this->tile_scratch_);
    this->tile_scratch_ = nullptr;
  }
}

void ScreenMirrorComponent::init_keyframe_encoder_() {
  std::lock_guard<std::mutex> jpeg_lock(this->jpeg_mutex_);
  if (this->keyframe_enc_handle_ != nullptr)
    return;

  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (is_dram_low_for_delta(free_internal, 35 * 1024)) {
    ESP_LOGW(TAG, "Low internal DRAM (%zu bytes free); skipping session keyframe encoder init", free_internal);
    return;
  }

  int w = (this->display_ != nullptr) ? this->display_->get_native_width() : 800;
  int h = (this->display_ != nullptr) ? this->display_->get_native_height() : 480;

  jpeg_enc_config_t keyframe_cfg = DEFAULT_JPEG_ENC_CONFIG();
  keyframe_cfg.width = w;
  keyframe_cfg.height = h;
  keyframe_cfg.src_type = this->big_endian_ ? JPEG_PIXEL_FORMAT_RGB565_BE : JPEG_PIXEL_FORMAT_RGB565_LE;
  keyframe_cfg.subsampling = JPEG_SUBSAMPLE_420;
  keyframe_cfg.quality = this->jpeg_quality_;
  keyframe_cfg.task_enable = false;

  jpeg_enc_handle_t kf_handle = nullptr;
  if (jpeg_enc_open(&keyframe_cfg, &kf_handle) == JPEG_ERR_OK && kf_handle != nullptr) {
    this->keyframe_enc_handle_ = kf_handle;
    ESP_LOGI(TAG, "Session keyframe JPEG encoder initialized (%dx%d, Q%u)", w, h, this->jpeg_quality_);
  } else {
    ESP_LOGW(TAG, "Failed to initialize session keyframe JPEG encoder; will fallback to per-frame open");
  }
}

void ScreenMirrorComponent::release_keyframe_encoder_() {
  std::lock_guard<std::mutex> jpeg_lock(this->jpeg_mutex_);
  if (this->keyframe_enc_handle_ != nullptr) {
    jpeg_enc_close(static_cast<jpeg_enc_handle_t>(this->keyframe_enc_handle_));
    this->keyframe_enc_handle_ = nullptr;
    ESP_LOGI(TAG, "Session keyframe JPEG encoder closed; internal DRAM released");
  }
}

void ScreenMirrorComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Screen Mirror component...");

  if (this->rotation_ == 0 && this->display_ != nullptr) {
    this->rotation_ = static_cast<uint16_t>(this->display_->get_rotation());
  }

  // Design Decision: Static Buffer Pre-Allocation in PSRAM.
  // Working buffers for JPEG encoding and tile extraction are pre-allocated statically in PSRAM.
  // This guarantees zero dynamic heap allocations per frame during streaming, eliminating heap
  // fragmentation and OOM failures over weeks/months of continuous operation.
  this->jpeg_buffer_size_ = 256 * 1024; // 256 KB buffer for compressed WebSocket JPEG frames
  this->jpeg_buffer_ = (uint8_t *) heap_caps_malloc(this->jpeg_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate WebSocket JPEG buffer in PSRAM!");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Allocated %u KB WebSocket JPEG buffer in PSRAM", (unsigned)(this->jpeg_buffer_size_ / 1024));

  // Allocate dedicated HTTP snapshot buffer in PSRAM to avoid clobbering live WS stream
  this->http_jpeg_buffer_size_ = 256 * 1024;
  this->http_jpeg_buffer_ = (uint8_t *) heap_caps_malloc(this->http_jpeg_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->http_jpeg_buffer_ != nullptr) {
    ESP_LOGI(TAG, "Allocated %u KB HTTP snapshot JPEG buffer in PSRAM", (unsigned)(this->http_jpeg_buffer_size_ / 1024));
  } else {
    ESP_LOGW(TAG, "Failed to allocate HTTP snapshot JPEG buffer in PSRAM");
  }

  // Allocate tile scratch buffer in PSRAM for extracting dirty sub-rectangles (512 KB in PSRAM, fallback to 128 KB)
  this->tile_scratch_size_ = 512 * 1024;
  this->tile_scratch_ = (uint8_t *) heap_caps_malloc(this->tile_scratch_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->tile_scratch_ == nullptr) {
    this->tile_scratch_size_ = 128 * 1024;
    this->tile_scratch_ = (uint8_t *) heap_caps_malloc(this->tile_scratch_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (this->tile_scratch_ != nullptr) {
    ESP_LOGI(TAG, "Allocated %u KB tile extraction scratch buffer in PSRAM", (unsigned)(this->tile_scratch_size_ / 1024));
  } else {
    ESP_LOGW(TAG, "Failed to allocate tile scratch buffer in PSRAM; delta streaming will be disabled");
  }

  // Install LVGL dirty region invalidation and render-ready hooks
  this->setup_lvgl_hooks();

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

      std::lock_guard<std::mutex> jpeg_lock(this->parent_->jpeg_mutex_);
      uint8_t *out_jpeg = this->parent_->get_http_jpeg_buffer();
      size_t max_capacity = this->parent_->get_http_jpeg_buffer_size();
      if (out_jpeg == nullptr || max_capacity == 0) {
        request->send(503, "text/plain", "Snapshot buffer not allocated");
        return;
      }

      size_t out_size = 0;
      if (!this->parent_->encode_jpeg(raw_buf, width, height, out_jpeg, out_size, max_capacity) || out_size == 0) {
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
  // Design Decision: Zero-Copy Live DMA Scanout Streaming.
  // Streams 16-bit RGB565 BMP chunks directly from active scanout memory without allocating
  // an intermediate ~768 KB snapshot buffer in PSRAM. Active high-speed animations during
  // download may exhibit progressive scanline tearing, which is expected for direct live DMA inspection.
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

      uint8_t header[66];
      this->parent_->build_bmp_header(width, height, header);
      size_t image_bytes = width * height * 2;

      httpd_resp_set_type(*request, "image/bmp");
      httpd_resp_set_hdr(*request, "Cache-Control", "no-cache, no-store, must-revalidate");
      httpd_resp_set_hdr(*request, "Access-Control-Allow-Origin", "*");
      if (httpd_resp_send_chunk(*request, reinterpret_cast<const char *>(header), sizeof(header)) == ESP_OK) {
        const size_t chunk_size = 1024;
        uint8_t swap_chunk[1024];
        size_t offset = 0;
        esp_err_t err = ESP_OK;
        while (offset < image_bytes && err == ESP_OK) {
          size_t to_send = std::min(chunk_size, image_bytes - offset);
          const char *send_ptr = nullptr;
          if (this->parent_->big_endian_) {
            swap_rgb565_buffer(buffer + offset, swap_chunk, to_send);
            send_ptr = reinterpret_cast<const char *>(swap_chunk);
          } else {
            send_ptr = reinterpret_cast<const char *>(buffer + offset);
          }
          err = httpd_resp_send_chunk(*request, send_ptr, to_send);
          offset += to_send;
        }
        if (err == ESP_OK) {
          httpd_resp_send_chunk(*request, nullptr, 0);
        }
      }
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
      int w = (this->parent_->display_ != nullptr) ? this->parent_->display_->get_native_width() : 0;
      int h = (this->parent_->display_ != nullptr) ? this->parent_->display_->get_native_height() : 0;
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

#if defined(USE_ESP32) && defined(CONFIG_HTTPD_WS_SUPPORT)
  auto *server = web_server_base::global_web_server_base->get_server();
  if (server != nullptr && server->get_server() != nullptr) {
    httpd_handle_t hd = server->get_server();

    // Struct matching internal httpd_data up to uri_match_fn to chain matching
    struct CustomHttpdData {
      httpd_config_t config;
    };
    auto *data = reinterpret_cast<CustomHttpdData *>(hd);
    data->config.uri_match_fn = [](const char *reference_uri, const char *uri_to_match, size_t match_len) -> bool {
      // If the incoming request is for /ws, only match the handler specifically registered for /ws
      if (match_len == 3 && strncmp(uri_to_match, "/ws", 3) == 0) {
        return (reference_uri != nullptr && strcmp(reference_uri, "/ws") == 0);
      }
      if (reference_uri != nullptr && strlen(reference_uri) > 0) {
        return (strncmp(reference_uri, uri_to_match, match_len) == 0 &&
                strlen(reference_uri) == match_len);
      }
      return true;
    };

    // Design Decision: Proactive Socket Descriptor Lifecycle Tracking.
    // Intercept httpd's session close callback so when any connection drops or closes,
    // its socket descriptor is immediately pruned from ws_client_fds_. This prevents
    // stale descriptors from colliding with newly opened connections (such as Home Assistant API).
    static httpd_close_func_t s_prev_close_fn = nullptr;
    s_mirror_instance = this;
    s_prev_close_fn = data->config.close_fn;

    data->config.close_fn = [](httpd_handle_t server_hd, int sockfd) {
      if (s_mirror_instance != nullptr) {
        s_mirror_instance->remove_ws_client(sockfd);
      }
      if (s_prev_close_fn != nullptr) {
        s_prev_close_fn(server_hd, sockfd);
      } else {
        close(sockfd);
      }
    };

    httpd_uri_t ws_uri = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = ScreenMirrorComponent::ws_handler,
      .user_ctx = this,
      .is_websocket = true,
      .handle_ws_control_frames = true,
      .supported_subprotocol = nullptr,
    };
    esp_err_t err = httpd_register_uri_handler(hd, &ws_uri);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "WebSocket touch endpoint registered at /ws");
    } else {
      ESP_LOGW(TAG, "Failed to register WebSocket endpoint: %d", err);
    }
  }
#endif

  ESP_LOGI(TAG, "Screen mirror endpoints active at: %s, /screenshot.jpg, /screenshot.bmp, /tap, /touch, /swipe, /ws",
           this->web_path_.c_str());
}

#if defined(USE_ESP32) && defined(CONFIG_HTTPD_WS_SUPPORT)
esp_err_t ScreenMirrorComponent::ws_handler(httpd_req_t *req) {
  auto *mirror = static_cast<ScreenMirrorComponent *>(req->user_ctx);
  if (mirror == nullptr) {
    return ESP_OK;
  }

  int fd = httpd_req_to_sockfd(req);

  if (req->method == HTTP_GET) {
    mirror->add_ws_client(fd);
    ESP_LOGI(TAG, "WebSocket client handshake established (fd: %d)", fd);
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

  // 1. Query frame length and type
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "httpd_ws_recv_frame failed to get frame length: %d", ret);
    mirror->remove_ws_client(fd);
    return ret;
  }

  if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
    mirror->remove_ws_client(fd);
    return ESP_OK;
  }

  if (ws_pkt.len == 0) {
    return ESP_OK;
  }

  // 2. Read frame payload into stack buffer
  char buf[64];
  if (ws_pkt.len >= sizeof(buf)) {
    ESP_LOGW(TAG, "WebSocket payload too large (%zu bytes), ignoring", ws_pkt.len);
    return ESP_OK;
  }

  ws_pkt.payload = reinterpret_cast<uint8_t *>(buf);
  ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "httpd_ws_recv_frame failed to receive payload: %d", ret);
    mirror->remove_ws_client(fd);
    return ret;
  }
  buf[ws_pkt.len] = '\0';

  auto cmd = parse_ws_packet(buf, ws_pkt.len);
  switch (cmd.type) {
    case WsCmdType::KEYFRAME:
      mirror->send_full_keyframe(fd);
      break;
    case WsCmdType::TAP:
      mirror->simulate_tap(cmd.x, cmd.y, cmd.duration, fd);
      break;
    case WsCmdType::DOWN:
      mirror->simulate_touch(cmd.x, cmd.y, "down", fd);
      break;
    case WsCmdType::MOVE:
      mirror->simulate_touch(cmd.x, cmd.y, "move", fd);
      break;
    case WsCmdType::UP:
      mirror->simulate_touch(cmd.x, cmd.y, "up", fd);
      break;
    default:
      break;
  }

  return ESP_OK;
}
#endif

bool ScreenMirrorComponent::send_ws_binary_packet(int fd, const uint8_t *payload, size_t len) {
  int sock_err = 0;
  socklen_t opt_len = sizeof(sock_err);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &opt_len) < 0 || sock_err != 0) {
    return false;
  }

  auto ws_frame = encode_ws_frame_header(len);

  uint32_t elapsed_ms = 0;
  const uint32_t max_budget_ms = 15; // 15ms hard transmission budget to protect loopTask

  auto send_all = [](int sockfd, const uint8_t *data, size_t total, uint32_t max_budget, uint32_t &elapsed) -> bool {
    size_t sent = 0;
    uint32_t start_time = millis();
    while (sent < total) {
      uint32_t current_elapsed = (millis() - start_time) + elapsed;
      if (current_elapsed >= max_budget) {
        elapsed = current_elapsed;
        return false; // Exceeded transmission budget for this client
      }
      ssize_t ret = send(sockfd, reinterpret_cast<const char *>(data + sent), total - sent, MSG_DONTWAIT);
      if (ret > 0) {
        sent += ret;
        continue;
      }
      if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          uint32_t now = millis();
          current_elapsed = (now - start_time) + elapsed;
          auto budget_res = calculate_budget_wait(current_elapsed, max_budget);
          if (budget_res.timed_out) {
            elapsed = current_elapsed;
            return false;
          }

          fd_set wset;
          FD_ZERO(&wset);
          FD_SET(sockfd, &wset);
          struct timeval tv = {.tv_sec = 0, .tv_usec = static_cast<suseconds_t>(budget_res.wait_us)};
          int sel = select(sockfd + 1, nullptr, &wset, nullptr, &tv);
          if (sel > 0) {
            continue;
          }
          if (sel == 0) {
            continue; // Slice timed out; loop to re-check cumulative budget
          }
        }
        elapsed += (millis() - start_time);
        return false;
      }
      elapsed += (millis() - start_time);
      return false; // ret == 0 (closed)
    }
    elapsed += (millis() - start_time);
    return true;
  };

  if (!send_all(fd, ws_frame.bytes, ws_frame.length, max_budget_ms, elapsed_ms))
    return false;
  return send_all(fd, payload, len, max_budget_ms, elapsed_ms);
}

void ScreenMirrorComponent::add_ws_client(int fd) {
  int nodelay = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &nodelay, sizeof(nodelay));

  std::lock_guard<std::mutex> lock(this->ws_mutex_);
  if (std::find(this->ws_client_fds_.begin(), this->ws_client_fds_.end(), fd) == this->ws_client_fds_.end()) {
    this->ws_client_fds_.push_back(fd);
    ESP_LOGI(TAG, "WebSocket client registered (fd: %d, total active: %zu)", fd, this->ws_client_fds_.size());
    if (this->ws_client_fds_.size() == 1) {
      this->init_keyframe_encoder_();
    }
  }
}

void ScreenMirrorComponent::remove_ws_client(int fd) {
  std::lock_guard<std::mutex> lock(this->ws_mutex_);
  auto it = std::find(this->ws_client_fds_.begin(), this->ws_client_fds_.end(), fd);
  if (it != this->ws_client_fds_.end()) {
    this->ws_client_fds_.erase(it);
    ESP_LOGI(TAG, "WebSocket client removed (fd: %d, total active: %zu)", fd, this->ws_client_fds_.size());
    if (this->ws_client_fds_.empty()) {
      this->release_keyframe_encoder_();
    }
  }
  if (this->touch_lease_.get_active_client() == fd) {
    this->touch_lease_.handle_disconnect(fd);
    this->defer([this]() {
      this->release_touch_();
    });
  }
}

void ScreenMirrorComponent::setup_lvgl_hooks() {
#if defined(HAS_LVGL)
  lv_display_t *disp = lv_display_get_default();
  if (disp == nullptr) {
    ESP_LOGW(TAG, "No default LVGL display found; delta region streaming unavailable");
    return;
  }

  // Hook flush start events: accumulates bounding box of dirty areas actually being flushed
  lv_display_add_event_cb(disp, [](lv_event_t *e) {
    auto *mirror = static_cast<ScreenMirrorComponent *>(lv_event_get_user_data(e));
    if (mirror == nullptr) return;
    auto *area = static_cast<const lv_area_t *>(lv_event_get_param(e));
    if (area == nullptr) return;

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    int hw_w = (mirror->display_ != nullptr) ? mirror->display_->get_native_width() : 800;
    int hw_h = (mirror->display_ != nullptr) ? mirror->display_->get_native_height() : 480;

    int native_x1 = x1, native_y1 = y1, native_x2 = x2, native_y2 = y2;
    uint16_t rot = mirror->get_rotation();

    if (rot == 90) {
      native_x1 = hw_w - y2 - 1;
      native_x2 = hw_w - y1 - 1;
      native_y1 = x1;
      native_y2 = x2;
    } else if (rot == 180) {
      native_x1 = hw_w - x2 - 1;
      native_x2 = hw_w - x1 - 1;
      native_y1 = hw_h - y2 - 1;
      native_y2 = hw_h - y1 - 1;
    } else if (rot == 270) {
      native_x1 = y1;
      native_x2 = y2;
      native_y1 = hw_h - x2 - 1;
      native_y2 = hw_h - x1 - 1;
    }

    if (native_x1 > native_x2) std::swap(native_x1, native_x2);
    if (native_y1 > native_y2) std::swap(native_y1, native_y2);

    mirror->add_dirty_area(native_x1, native_y1, native_x2, native_y2);
  }, LV_EVENT_FLUSH_START, this);

  this->lvgl_hooked_ = true;
  ESP_LOGI(TAG, "LVGL dirty region flush hooks installed successfully");
#endif
}

void ScreenMirrorComponent::add_dirty_area(int x1, int y1, int x2, int y2) {
  std::lock_guard<std::mutex> lock(this->dirty_mutex_);
  int hw_w = (this->display_ != nullptr) ? this->display_->get_native_width() : 800;
  int hw_h = (this->display_ != nullptr) ? this->display_->get_native_height() : 480;
  this->dirty_tracker_.add(x1, y1, x2, y2, hw_w, hw_h);
}

void ScreenMirrorComponent::on_lvgl_render_ready() {
}

bool ScreenMirrorComponent::send_dirty_update() {
  if (this->jpeg_buffer_ == nullptr)
    return false;

  {
    std::lock_guard<std::mutex> ws_lock(this->ws_mutex_);
    if (this->ws_client_fds_.empty()) {
      std::lock_guard<std::mutex> dirty_lock(this->dirty_mutex_);
      this->dirty_tracker_.clear();
      return true;
    }
  }

  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  {
    std::lock_guard<std::mutex> lock(this->dirty_mutex_);
    if (!this->dirty_tracker_.take_dirty(x1, y1, x2, y2))
      return true;
  }

  int w = x2 - x1 + 1;
  int h = y2 - y1 + 1;
  if (w <= 0 || h <= 0)
    return true;

  const uint8_t *raw_buf = nullptr;
  int hw_w = 0, hw_h = 0;
  if (!this->capture_frame_rgb565(raw_buf, hw_w, hw_h) || raw_buf == nullptr)
    return false;

  if (x1 + w > hw_w) w = hw_w - x1;
  if (y1 + h > hw_h) h = hw_h - y1;

  // Design Decision: Automatic Keyframe Escalation.
  // If the accumulated dirty bounding box covers >= 60% of the display area (e.g. full-screen
  // page navigation or complex animations), escalate to sending a full keyframe rather than
  // extracting and streaming a large partial tile. This prevents visual tearing and reduces
  // tile extraction CPU overhead.
  if (DirtyAreaTracker::should_escalate_to_keyframe(w, h, hw_w, hw_h, 60)) {
    return this->send_full_keyframe(-1);
  }

  const uint8_t *pixel_src = nullptr;
  if (DirtyAreaTracker::extract_tile_rgb565(raw_buf, hw_w, hw_h, x1, y1, w, h,
                                           this->tile_scratch_, this->tile_scratch_size_)) {
    pixel_src = this->tile_scratch_;
  } else {
    return this->send_full_keyframe(-1);
  }

  size_t total_payload_len = 0;
  {
    std::lock_guard<std::mutex> jpeg_lock(this->jpeg_mutex_);

    // Encode JPEG into jpeg_buffer_ + 10 (leaving 10 bytes for binary header)
    uint8_t *jpeg_dest = this->jpeg_buffer_ + 10;
    size_t max_out = (this->jpeg_buffer_size_ > 10) ? (this->jpeg_buffer_size_ - 10) : 0;
    size_t out_size = 0;
    if (!this->encode_jpeg(pixel_src, w, h, jpeg_dest, out_size, max_out) || out_size == 0) {
      return false;
    }

    // Fill 10-byte binary header
    uint8_t rot_code = static_cast<uint8_t>((this->rotation_ / 90) & 0x03);
    encode_mirror_header(false, rot_code, x1, y1, w, h, this->jpeg_buffer_);

    total_payload_len = 10 + out_size;
  }

#if defined(USE_ESP32) && defined(CONFIG_HTTPD_WS_SUPPORT)
  std::vector<int> clients;
  {
    std::lock_guard<std::mutex> lock(this->ws_mutex_);
    clients = this->ws_client_fds_;
  }

  std::vector<int> dead_fds;
  for (int fd : clients) {
    if (!this->send_ws_binary_packet(fd, this->jpeg_buffer_, total_payload_len)) {
      dead_fds.push_back(fd);
    }
  }
  for (int dead_fd : dead_fds) {
    this->remove_ws_client(dead_fd);
  }
#endif

  return true;
}

bool ScreenMirrorComponent::send_full_keyframe(int target_fd) {
  if (this->jpeg_buffer_ == nullptr)
    return false;

  const uint8_t *raw_buf = nullptr;
  int hw_w = 0, hw_h = 0;
  if (!this->capture_frame_rgb565(raw_buf, hw_w, hw_h) || raw_buf == nullptr)
    return false;

  size_t total_payload_len = 0;
  {
    std::lock_guard<std::mutex> jpeg_lock(this->jpeg_mutex_);

    uint8_t *jpeg_dest = this->jpeg_buffer_ + 10;
    size_t max_out = (this->jpeg_buffer_size_ > 10) ? (this->jpeg_buffer_size_ - 10) : 0;
    size_t out_size = 0;
    if (!this->encode_jpeg(raw_buf, hw_w, hw_h, jpeg_dest, out_size, max_out) || out_size == 0) {
      return false;
    }

    // Fill 10-byte binary header
    uint8_t rot_code = static_cast<uint8_t>((this->rotation_ / 90) & 0x03);
    encode_mirror_header(true, rot_code, 0, 0, hw_w, hw_h, this->jpeg_buffer_);

    total_payload_len = 10 + out_size;
  }

#if defined(USE_ESP32) && defined(CONFIG_HTTPD_WS_SUPPORT)
  std::vector<int> dead_fds;
  if (target_fd >= 0) {
    if (!this->send_ws_binary_packet(target_fd, this->jpeg_buffer_, total_payload_len)) {
      dead_fds.push_back(target_fd);
    }
  } else {
    std::vector<int> clients;
    {
      std::lock_guard<std::mutex> lock(this->ws_mutex_);
      clients = this->ws_client_fds_;
    }
    for (int fd : clients) {
      if (!this->send_ws_binary_packet(fd, this->jpeg_buffer_, total_payload_len)) {
        dead_fds.push_back(fd);
      }
    }
  }
  for (int dead_fd : dead_fds) {
    this->remove_ws_client(dead_fd);
  }
#endif

  return true;
}

void ScreenMirrorComponent::loop() {
#ifdef USE_TOUCHSCREEN
  if (this->swipe_gesture_.is_active() && this->touchscreen_ != nullptr) {
    SwipeStepOutput step_out;
    if (this->swipe_gesture_.step(millis(), step_out)) {
      int client_id = this->swipe_gesture_.get_client_id();
      if (step_out.is_final) {
        // Final coordinate
        this->simulate_touch_internal_(step_out.x, step_out.y, "move", client_id);
        // Release touch
        this->simulate_touch_internal_(step_out.x, step_out.y, "up", client_id);
        ESP_LOGD(TAG, "Swipe completed at (%d, %d)", step_out.x, step_out.y);
      } else {
        this->simulate_touch_internal_(step_out.x, step_out.y, "move", client_id);
      }
    }
  }
#endif

  // Handle pending LVGL dirty update or continuous streaming over WebSocket
  bool has_clients = false;
  {
    std::lock_guard<std::mutex> lock(this->ws_mutex_);
    has_clients = !this->ws_client_fds_.empty();
  }

  bool has_dirty = false;
  {
    std::lock_guard<std::mutex> dirty_lock(this->dirty_mutex_);
    has_dirty = this->dirty_tracker_.has_dirty();
  }

  if (has_clients) {
    FrameAction action = FrameScheduler::evaluate(millis(), this->fps_, this->gated_streaming_,
                                                  has_dirty, has_clients, this->last_frame_time_);
    if (action == FrameAction::FULL_KEYFRAME) {
      this->send_full_keyframe(-1);
    } else if (action == FrameAction::DIRTY_DELTA) {
      this->send_dirty_update();
    }
  } else if (has_dirty) {
    std::lock_guard<std::mutex> dirty_lock(this->dirty_mutex_);
    this->dirty_tracker_.clear();
  }
}

void ScreenMirrorComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Screen Mirror:");
  ESP_LOGCONFIG(TAG, "  Rotation: %u°", this->rotation_);
  ESP_LOGCONFIG(TAG, "  Byte Order: %s", this->big_endian_ ? "Big-Endian" : "Little-Endian");
  ESP_LOGCONFIG(TAG, "  FPS Target: %.1f", this->fps_);
  ESP_LOGCONFIG(TAG, "  JPEG Quality: %u", this->jpeg_quality_);
  ESP_LOGCONFIG(TAG, "  Gated Streaming: %s", YESNO(this->gated_streaming_));
  ESP_LOGCONFIG(TAG, "  Web Path: %s", this->web_path_.c_str());
}

bool ScreenMirrorComponent::capture_frame_rgb565(const uint8_t *&out_buffer, int &width, int &height) {
  if (this->display_ == nullptr)
    return false;

  width = this->display_->get_native_width();
  height = this->display_->get_native_height();

#if __has_include("esphome/components/mipi_rgb/mipi_rgb.h") && __has_include("esp_lcd_panel_rgb.h")
  // Design Decision: Direct Hardware Scanout DMA Access.
  // On ESP32-S3 RGB panels, LVGL streams pixel data directly into the ESP-IDF hardware RGB panel.
  // The hardware DMA scanout framebuffer in PSRAM (obtained via esp_lcd_rgb_panel_get_frame_buffer)
  // is the only location containing the active, fully composited screen image.
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
                                        uint8_t *out_jpeg, size_t &out_size, size_t max_out_capacity) {
  if (rgb565_buffer == nullptr || out_jpeg == nullptr || width <= 0 || height <= 0)
    return false;

  size_t capacity = (max_out_capacity > 0) ? max_out_capacity : this->jpeg_buffer_size_;
  int native_w = (this->display_ != nullptr) ? this->display_->get_native_width() : 800;
  int native_h = (this->display_ != nullptr) ? this->display_->get_native_height() : 480;

  bool is_full_frame = (width == native_w && height == native_h);

  jpeg_enc_handle_t enc_handle = nullptr;
  bool is_persistent = false;

  if (is_full_frame && this->keyframe_enc_handle_ != nullptr) {
    enc_handle = static_cast<jpeg_enc_handle_t>(this->keyframe_enc_handle_);
    is_persistent = true;
  } else {
    // Dynamic tile or unmanaged keyframe: apply pre-flight internal DRAM guard (require at least 35 KB free internal memory)
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (is_dram_low_for_delta(free_internal, 35 * 1024)) {
      ESP_LOGW(TAG, "Skipping encode: low internal DRAM (%zu bytes free)", free_internal);
      return false;
    }

    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = width;
    cfg.height = height;
    cfg.src_type = this->big_endian_ ? JPEG_PIXEL_FORMAT_RGB565_BE : JPEG_PIXEL_FORMAT_RGB565_LE;
    cfg.subsampling = JPEG_SUBSAMPLE_420;
    cfg.quality = this->jpeg_quality_;
    cfg.task_enable = false;

    jpeg_error_t open_err = jpeg_enc_open(&cfg, &enc_handle);
    if (open_err != JPEG_ERR_OK || enc_handle == nullptr) {
      ESP_LOGE(TAG, "jpeg_enc_open failed for frame (%dx%d): %d", width, height, open_err);
      return false;
    }
  }

  int in_size = width * height * 2;
  int out_len = 0;
  jpeg_error_t err = jpeg_enc_process(enc_handle, (uint8_t *) rgb565_buffer, in_size,
                                      out_jpeg, (int) capacity, &out_len);

  if (!is_persistent) {
    jpeg_enc_close(enc_handle);
  }

  if (err != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "jpeg_enc_process failed: %d", err);
    return false;
  }

  out_size = (size_t) out_len;
  return (out_size > 0);
}

bool ScreenMirrorComponent::build_bmp_header(int width, int height, uint8_t *header_66) {
  return screen_mirror::build_bmp_header(width, height, header_66);
}

void ScreenMirrorComponent::release_touch_() {
#ifdef USE_TOUCHSCREEN
  this->cancel_timeout("min_touch_hold");
  this->cancel_timeout("release_tap");
  this->touch_lease_.record_up();
  if (this->touchscreen_ != nullptr && this->is_touch_down_) {
    for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
      listener->release();
    }
    this->touchscreen_->get_release_trigger()->trigger();
    this->is_touch_down_ = false;
    this->mark_dirty();
  }
#endif
}

bool ScreenMirrorComponent::simulate_tap(int x, int y, uint16_t duration_ms, int client_id) {
  if (duration_ms == 0) {
    duration_ms = 80;
  }
#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ == nullptr) {
    ESP_LOGW(TAG, "Cannot simulate tap: no touchscreen component configured under screen_mirror");
    return false;
  }

  int hw_w = (this->display_ != nullptr) ? this->display_->get_native_width() : 800;
  int hw_h = (this->display_ != nullptr) ? this->display_->get_native_height() : 480;
  clamp_coordinates(x, y, hw_w, hw_h);

  // Marshal execution to main loop task for LVGL and ESPHome thread safety
  this->defer([this, x, y, duration_ms, client_id]() {
    ESP_LOGD(TAG, "Executing tap at (%d, %d) for %u ms on main loop (client %d)", x, y, duration_ms, client_id);
    this->cancel_timeout("release_tap");
    this->cancel_timeout("min_touch_hold");

    this->simulate_touch_internal_(x, y, "down", client_id);
    this->set_timeout("release_tap", duration_ms, [this]() {
      this->release_touch_();
    });
  });
  return true;
#else
  ESP_LOGW(TAG, "Cannot simulate tap: touchscreen support not compiled in");
  return false;
#endif
}

bool ScreenMirrorComponent::simulate_touch(int x, int y, const std::string &action, int client_id) {
#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ == nullptr) {
    ESP_LOGW(TAG, "Cannot simulate touch: no touchscreen configured");
    return false;
  }

  int hw_w = (this->display_ != nullptr) ? this->display_->get_native_width() : 800;
  int hw_h = (this->display_ != nullptr) ? this->display_->get_native_height() : 480;
  clamp_coordinates(x, y, hw_w, hw_h);

  // Marshal execution to main loop task for LVGL and ESPHome thread safety
  this->defer([this, x, y, action, client_id]() {
    this->simulate_touch_internal_(x, y, action, client_id);
  });
  return true;
#else
  return false;
#endif
}

void ScreenMirrorComponent::simulate_touch_internal_(int x, int y, const std::string &action, int client_id) {
#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ == nullptr) return;

  uint32_t now = millis();

  if (action == "down") {
    // If a touch is already held down by another client and lease hasn't expired, reject conflicting touch
    if (!this->touch_lease_.can_process_down(client_id, now, this->is_touch_down_)) {
      ESP_LOGD(TAG, "Touch rejected: active lease held by client %d", this->touch_lease_.get_active_client());
      return;
    }

    // If a touch was already down, cleanly release it first before starting a new touch
    if (this->is_touch_down_) {
      this->release_touch_();
    }
    this->cancel_timeout("min_touch_hold");
    this->cancel_timeout("release_tap");

    this->touch_lease_.record_down(client_id, now);
    this->last_touch_x_ = x;
    this->last_touch_y_ = y;

    touchscreen::TouchPoint tp{};
    tp.id = 0;
    tp.state = touchscreen::STATE_PRESSED;
    tp.x = x;
    tp.y = y;
    tp.x_raw = x;
    tp.y_raw = y;

    // Design Decision: Touchscreen Subsystem Indirection.
    // Dispatched directly through ESPHome's Touchscreen subsystem rather than bypassing it into LVGL.
    // This guarantees that display wake-on-touch routines, YAML on_touch: triggers, and UI event
    // listeners execute identically to physical hardware touch controller interrupts.
    this->touchscreen_->get_touch_trigger()->trigger(tp, {tp});

    // Notify all registered listeners (LVGL)
    for (auto *listener : TouchscreenAccess::get_listeners(this->touchscreen_)) {
      listener->touch(tp);
      listener->update({tp});
    }
    this->is_touch_down_ = true;
    this->mark_dirty();

  } else if (action == "move") {
    if (!this->is_touch_down_) {
      // If move arrives without preceding down, initialize down state at current coordinate
      this->simulate_touch_internal_(x, y, "down", client_id);
      return;
    }
    // Only the client holding the lease (or any client after lease expiration) can move
    if (!this->touch_lease_.can_process_move(client_id)) {
      return;
    }

    this->last_touch_x_ = x;
    this->last_touch_y_ = y;

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

  } else if (action == "up") {
    if (!this->is_touch_down_) {
      return;
    }
    if (!this->touch_lease_.can_process_up(client_id)) {
      return;
    }
    uint32_t elapsed = millis() - this->touch_lease_.get_last_down_time();
    // Design Decision: LVGL Touch Dwell Time Guarantee.
    // Enforce a minimum 180ms touch hold time on the device. Because LVGL's input indev handler
    // runs periodically in ticks (typically every 30-50ms), a momentary touch release (<100ms)
    // might be missed across loop latency spikes. 180ms guarantees multi-tick sampling while
    // staying comfortably below LVGL's default 400ms long-press threshold.
    const uint32_t min_hold_ms = 180;
    if (elapsed < min_hold_ms) {
      uint32_t delay_ms = min_hold_ms - elapsed;
      this->set_timeout("min_touch_hold", delay_ms, [this]() {
        this->release_touch_();
      });
      return;
    }
    this->release_touch_();

  } else {
    ESP_LOGW(TAG, "Invalid touch action: %s (expected 'down', 'move', or 'up')", action.c_str());
  }
#endif
}

bool ScreenMirrorComponent::simulate_swipe(int start_x, int start_y, int end_x, int end_y, uint16_t duration_ms, int client_id) {
#ifdef USE_TOUCHSCREEN
  if (this->touchscreen_ == nullptr) {
    ESP_LOGW(TAG, "Cannot simulate swipe: no touchscreen configured");
    return false;
  }

  int hw_w = (this->display_ != nullptr) ? this->display_->get_native_width() : 800;
  int hw_h = (this->display_ != nullptr) ? this->display_->get_native_height() : 480;
  start_x = std::max(0, std::min(hw_w - 1, start_x));
  start_y = std::max(0, std::min(hw_h - 1, start_y));
  end_x = std::max(0, std::min(hw_w - 1, end_x));
  end_y = std::max(0, std::min(hw_h - 1, end_y));

  duration_ms = std::max<uint16_t>(50, duration_ms);

  // Marshal swipe initialization to main loop task
  this->defer([this, start_x, start_y, end_x, end_y, duration_ms, client_id]() {
    if (this->swipe_gesture_.is_active() || this->is_touch_down_) {
      this->release_touch_();
    }

    ESP_LOGD(TAG, "Starting swipe from (%d, %d) to (%d, %d) over %u ms on main loop (client %d)",
             start_x, start_y, end_x, end_y, duration_ms, client_id);

    // 1. Initial touch down at start coordinates
    this->simulate_touch_internal_(start_x, start_y, "down", client_id);

    // 2. Initialize active swipe state for loop() interpolation
    this->swipe_gesture_.start(start_x, start_y, end_x, end_y, duration_ms, millis(), client_id);
  });

  return true;
#else
  return false;
#endif
}

}  // namespace screen_mirror
}  // namespace esphome
