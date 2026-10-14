# ESPHome Screen Mirror (`screen_mirror`)

## Project Requirements & Architectural Specification

### 1. Vision & Purpose

`esphome-screen-mirror` is an open-source ESPHome external component for ESP32 devices that exposes an interactive, low-latency web mirror of graphical displays over local Wi-Fi. It enables:

- **Interactive Remote Control:** View the display in any modern desktop or mobile browser, and click/tap to interact with the device.
- **Headless & Automated Testing:** Simple REST endpoints for test harnesses (Python, Playwright, curl) and AI coding agents to capture screenshots, simulate touch interactions, and assert UI states.
- **Visual Regression Testing:** Fast JPEG captures for automated UI diffing across firmware updates.

---

### 2. User Stories & Use Cases

1. **Home Dashboard Developer:** Modifying an LVGL dashboard mounted inside a campervan or wall without walking over to the physical screen to inspect changes or tap buttons.
2. **Automated E2E Testing:** Running a test script in CI/CD or local environment that taps through all pages, captures frames, and validates UI logic.
3. **AI Pair Programmer:** Allowing an AI agent to autonomously adjust UI styling, pull `/screenshot.jpg`, inspect layout, and correct padding/colors without human intervention.

---

### 3. Display & Architecture Abstraction

To ensure compatibility across hardware, `esphome-screen-mirror` implements a unified **Framebuffer Adapter Interface**:

```
                  ┌──────────────────────────────────────────────┐
                  │            esphome-screen-mirror             │
                  │   (JPEG Encoder, HTTP Server, Web UI)        │
                  └──────────────────────┬───────────────────────┘
                                         │
                         ┌───────────────┴───────────────┐
                         ▼                               ▼
                 [ LVGL / RGB Adapter ]         [ DisplayBuffer Adapter ]
                         │                               │
                Target: CrowPanel, Sunton       Target: CYD, OLED, E-Paper
                - LVGL 8 & 9                    - ST7789, ILI9341, SSD1306
                - mipi_rgb (PSRAM DMA)          - ESPHome 2D drawing lambdas
```

---

### 4. Functional Requirements

#### 4.1 Screen Capture & Streaming

- **REQ-CAP-1 (Static JPEG):** Expose `GET /screenshot.jpg` (or `/screenshot.jpeg`) serving a single JPEG-encoded frame of the current screen.
  - Query parameters: `?quality=1-100` (default 75), `?scale=1|2`.
- **REQ-CAP-2 (Static BMP Fallback):** Expose `GET /screenshot.bmp` returning uncompressed RGB565/RGB888 BMP for pixel-perfect debugging and minimal CPU overhead.
- **Architectural Decision (MJPEG Deprecation):** Continuous HTTP multipart MJPEG streaming (`/stream.mjpeg`) was evaluated and deprecated. Because the ESP-IDF `httpd` daemon runs on a single FreeRTOS worker thread, a persistent multipart HTTP connection starves all other HTTP requests (e.g. concurrent `/tap`, `/remote`, or other web endpoints). Instead, real-time live preview is accomplished via client-adaptive snapshot polling (`/screenshot.jpg`) and asynchronous WebSockets (Phase 4).
- **REQ-CAP-3 (Dirty Tile Blitting via WebSockets):** Expose a WebSocket endpoint for partial-frame delta streaming:
  - Transmits binary packets containing bounding boxes `[x, y, w, h]` and JPEG-encoded tiles.
  - Client blits tiles onto an HTML5 `<canvas>` via `ctx.drawImage()`, reducing bandwidth to 1–5 KB/s for minor UI updates without blocking the HTTP server thread.
- **REQ-CAP-4 (Metadata Endpoint):** Expose `GET /mirror/info` returning JSON:
  - Screen dimensions (`width`, `height`, `rotation`)
  - Active display driver, color format, and adapter type
  - Memory statistics and telemetry

#### 4.2 Input Simulation

- **REQ-INP-1 (Single Tap):** Expose `POST /tap` accepting JSON `{ "x": int, "y": int }` or URL params `?x=...&y=...` to simulate a press and release.
  - Configurable press duration (default: ~50ms).
- **REQ-INP-2 (Drag / Swipe):** Expose `POST /swipe` accepting `{ "start_x": int, "start_y": int, "end_x": int, "end_y": int, "duration_ms": int }` for scrolling/gestures.
- **REQ-INP-3 (Continuous Touch):** Expose `POST /touch` accepting state changes (`down`, `move`, `up`) for live interactive dragging on the web canvas.
- **REQ-INP-4 (Touchscreen Subsystem Requirement):** Input simulation relies exclusively on the ESPHome `Touchscreen` subsystem (`touchscreen: <id>`). Simulated touches are dispatched through `touchscreen->get_touch_trigger()` and all registered listeners. This cleanly decouples `screen_mirror` from UI frameworks, triggers custom YAML `on_touch:` automations (such as waking displays from idle/snow states), and routes directly into UI listeners (like LVGL).
- **REQ-INP-5 (Hardware Touch Non-Interference):** Software-injected touches cleanly coexist with physical touch controller interrupts (e.g. GT911, XPT2046).
- **REQ-INP-6 (WebSocket Low-Latency Touch Channel):** Expose a dedicated WebSocket endpoint at `/ws` for high-frequency, bidirectional touch events (`T <x> <y>` for atomic taps, `D <x> <y>` for pointer down, `M <x> <y>` for throttled drag moves, and `U <x> <y>` for release). Simulated taps enforce a minimum 180ms dwell time on the ESP32 to guarantee multi-tick sampling by LVGL's internal indev handler across loop latency spikes, while remaining comfortably below the 400ms long-press threshold.

#### 4.3 Embedded Web UI (Web Mirror)

- **REQ-WEB-1 (Self-Contained Page):** Serve a responsive single-page application at `GET /remote` (or `/mirror`).
- **REQ-WEB-2 (Zero External CDN):** 100% offline-capable; no external fonts, CSS, or JS libraries.
- **REQ-WEB-3 (Interactive Canvas / Image):**
  - Renders the live mirror via adaptive frame refresh or HTML5 `<canvas>` Tile Blitting.
  - Automatically translates browser mouse/touch clicks to native device coordinates `(800x480)` regardless of window size and screen rotation.
  - Shows visual touch feedback (ripple or crosshair) at the clicked position.
- **REQ-WEB-4 (Device Telemetry Bar):** Display live overlay (FPS, encode latency, free PSRAM/heap, Wi-Fi RSSI).

---

### 5. Technical & Performance Requirements

#### 5.1 Hardware & Framework Targets

- **Framework:** **ESP-IDF only** (`framework: type: esp-idf`). The component is built exclusively around Espressif's native `espressif/esp_new_jpeg` codec and the ESP-IDF Component Manager. The Arduino framework is not supported.
- **Primary Tier (High Performance):** ESP32-S3 with Octal PSRAM (CrowPanel 5.0", Sunton 800×480) using `esp-idf` framework.
- **Secondary Tier:** Standard ESP32 (WROVER / WROOM) with SPI displays (`ili9xxx`, `st7789v`) under `esp-idf`.

#### 5.2 Performance Constraints

- **Encoding Latency:** < 50ms for 800×480 on ESP32-S3 using SIMD-accelerated DCT (`esp_jpeg` or optimized `JPEGENC`).
- **Framerate Target:** 1 to 5 FPS streaming without blocking LVGL’s internal 60 Hz rendering loop.
- **Zero-Idle Overhead:** 0 ms CPU time spent on encoding when the display has no pending invalidation rectangles.
- **Core Isolation:** JPEG compression and network I/O run on Core 0; LVGL and ESPHome component loops remain dedicated to Core 1.
- **Memory Management:** Pre-allocate static working buffers in PSRAM; zero dynamic heap fragmentation per frame.

---

### 6. Proposed ESPHome YAML Configuration Schema

```yaml
external_components:
  - source: github://your-username/esphome-screen-mirror
    components: [screen_mirror]

web_server:
  port: 80

screen_mirror:
  id: hmi_mirror
  display: rpi_disp # Reference to mipi_rgb or DisplayBuffer component
  touchscreen: my_touchscreen # Optional reference to hardware touchscreen for injection

  # Stream & Encoding Settings
  fps: 1 # Max streaming framerate (default: 1, range: 0.1 - 10)
  jpeg_quality: 75 # Default JPEG quality (1-100)
  gated_streaming: true # Skip frames when screen is idle

  # Web Interface Settings
  web_path: "/remote" # URL path for web mirror interface
  show_telemetry: true # Show FPS, latency, and memory overlay

  # Core Allocation (ESP-IDF only)
  core: 0 # Run encoder/streamer on Core 0
```

---

### 7. Phased Development Roadmap

| Phase       | Milestone                                      | Deliverables                                                                                                                                                                                    |
| :---------- | :--------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Phase 1** | **Core JPEG & Snapshot API**                   | - Integrate SIMD-accelerated `JPEGENC` / `esp_jpeg`.<br>- Implement `mipi_rgb` & LVGL 9 frame grabber.<br>- Working `GET /screenshot.jpg` and `GET /screenshot.bmp`.                            |
| **Phase 2** | **Touch Input Injection & Web Mirror UI**      | - Implement `POST /tap?x=..&y=..` routed via ESPHome Touchscreen subsystem.<br>- Embedded HTML5/JS Web Mirror at `/remote` with gzip asset embedding and auto-rotation.<br>- Coordinate scaling and click-to-tap feedback. |
| **Phase 3** | **Swipe & Drag Gestures & WebSocket Streaming** | - Implement `POST /swipe` with start/end coordinates and duration.<br>- Real-time WebSocket touch streaming at `/ws` for ultra-low latency interaction.<br>- Guaranteed 180ms LVGL sampling dwell times across CPU/loop stalls.<br>- Continuous touch drag gestures for scrolling and sliders.<br>- Web Mirror pointer dragging and sequential non-choking polling. |
| **Phase 4** | **DisplayBuffer & Strategy C (Tile Blitting)** | - Add `DisplayBuffer` adapter (ST7789, ILI9341, OLEDs).<br>- Asynchronous WebSocket tile blitting for sub-frame delta updates.<br>- Documentation, repository release, and CI validation.   |
