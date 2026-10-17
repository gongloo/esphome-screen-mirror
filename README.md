# ESPHome Screen Mirror (`screen_mirror`)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![ESPHome External Component](https://img.shields.io/badge/ESPHome-External%20Component-orange.svg)](https://esphome.io)

A lightweight, low-latency screen mirroring and remote touch testing component for ESPHome displays on the ESP32.

Think of it as **VNC / `scrcpy` built directly into your ESP32**, accessible from any browser without installing extra software.

---

## Salient Use Cases

- 🛋️ **Remote HMI Control:** Monitor and interact with wall-mounted dashboards, campervan consoles, or enclosed touchscreens directly from your phone or desktop browser without physical access.
- 🧪 **Automated & Headless UI Testing:** Script end-to-end interface testing in CI/CD using Python, Playwright, or `curl` to capture screenshots (`/screenshot.jpg`) and inject touch interactions (`POST /tap`) to assert UI states before firmware deploys.
- 🤖 **AI Agent Pair Programming:** Enables autonomous AI coding agents to modify LVGL widget layouts and themes in YAML, pull screenshots to visually inspect alignments and colors, and tap through UI screens in a closed loop.

---

## Current Features & Capabilities

- ⚡ **Delta-Region WebSocket Streaming (`/ws`):** Hooked directly into LVGL's render pipeline (`LV_EVENT_FLUSH_START`). Only modified screen bounding boxes are compressed and transmitted over WebSocket as binary JPEG tiles, reducing Wi-Fi bandwidth to ~1–5 KB/s during UI interactions.
- 🎯 **Automatic Keyframe Escalation & Gated Streaming:**
  - If a screen transition modifies $\ge 60\%$ of the display area, the stream automatically escalates to a full keyframe to eliminate visual tearing.
  - With `gated_streaming: true`, zero CPU cycles and network bytes are spent when the display is idle.
- 📸 **Hardware-Accelerated JPEG Snapshots:** High-speed JPEG capture via `GET /screenshot.jpg` powered by ESP-IDF SIMD/hardware acceleration (`esp_new_jpeg`), requiring ~0 ms extra conversion overhead.
- 🖼️ **Raw BMP Snapshots:** Pixel-perfect uncompressed frame capture via `GET /screenshot.bmp` for exact color & alignment debugging.
- 👆 **Comprehensive Touch & Gesture Simulation:**
  - **Single Taps:** `POST /tap?x=...&y=...` with automatic screen-wake support and guaranteed LVGL dwell times (180ms).
  - **Continuous Drag & Touch:** `POST /touch?action=down|move|up` for sliders, scrolling, and hold gestures.
  - **Automated Swipes:** `POST /swipe?start_x=...&end_x=...` with smooth linear interpolation.
  - Routed directly through ESPHome's `Touchscreen` subsystem, executing both user-defined YAML `on_touch:` automations (e.g. waking sleeping displays) and UI listeners (such as LVGL).
- 🌐 **Interactive Web Mirror (`/remote`):** Embedded, standalone HTML5 viewer preprocessed with gzip and served directly from PROGMEM:
  - Canvas rotation & coordinate transformation for 0°, 90°, 180°, and 270° orientations.
  - Real-time pointer dragging, holds, and tap-to-touch with visual ripple feedback.
  - Bidirectional WebSocket communication with automatic HTTP snapshot polling fallback if the socket closes.
  - Live HUD telemetry displaying dynamic streaming rate (FPS), HTTP latency, and resolution.
- 🎨 **Automatic Byte-Order & Endianness Handling:** Automatically detects display and LVGL byte orders (`big_endian` vs `little_endian`) to ensure 100% accurate color reproduction with zero CPU overhead.
- 🔄 **Automatic Orientation Detection:** Seamlessly inherits display/LVGL rotation without requiring manual YAML configuration.
- 🧩 **Display Support:** Optimized for ESP32-S3 RGB parallel panels (`mipi_rgb`) with PSRAM and ESP-IDF. *(See compatibility matrix below).*

---

## Hardware & Display Compatibility

### Currently Supported
- **SoC:** ESP32-S3 with external PSRAM enabled.
- **Framework:** ESP-IDF only (`framework: type: esp-idf`).
- **Displays:** RGB parallel panels driven by the ESPHome **`mipi_rgb`** component (e.g., Waveshare 4.3"/7", Guition ESP32-S3-4848S040, Sunton ESP32-S3 800x480).
- **UI Framework:** LVGL is recommended/required for real-time delta WebSocket streaming (`/ws`). Non-LVGL setups can poll static HTTP snapshots.

### Not Supported (Future Roadmap)
- **SPI / I2C / Parallel 8080 Displays (`st7789v`, `ili9341`, `gc9a01`, etc.):** The component directly accesses the hardware scanout framebuffer in PSRAM via `esp_lcd_rgb_panel_get_frame_buffer()`. Reading back from `display::DisplayBuffer::buffer_` is not implemented at this time.
- **Monochrome & E-Paper Displays (`ssd1306`, `waveshare_epaper`, etc.):** The hardware JPEG encoder and BMP generator require 16-bit RGB565 buffers and do not support 1-bit bitmasks or grayscale LUTs.
- **Non-ESP32 Architectures:** ESP8266, RP2040, and other microcontrollers are not supported.

> **Note:** While an adapter for standard SPI/I2C `DisplayBuffer` displays may be considered in the future, it is not currently prioritized or implemented. Pull requests are welcome!

---

## Configuration Reference

```yaml
external_components:
  - source: github://gongloo/esphome-screen-mirror
    components: [screen_mirror]

web_server:
  port: 80

screen_mirror:
  id: hmi_mirror               # Optional: component ID
  display: rpi_disp            # Required: reference to display component
  touchscreen: my_touchscreen  # Optional: reference to touchscreen (required for tap simulation & web mirror clicks)
  fps: 1.0                     # Optional: max delta framerate (default: 1.0, range: 0.1 - 30.0)
  jpeg_quality: 75             # Optional: JPEG quality (default: 75, range: 10 - 100)
  gated_streaming: true        # Optional: skip frames when screen has no changes (default: true)
  web_path: "/remote"          # Optional: web viewer URL path (default: /remote)
  show_telemetry: true         # Optional: show latency/device info badge in web viewer (default: true)
  rotation: 90                 # Optional: 0, 90, 180, 270 (auto-detected from lvgl/display if omitted)
  byte_order: big_endian       # Optional: big_endian, little_endian (auto-detected from lvgl/display if omitted)
  core: 0                      # Optional: ESP32 CPU core (default: 0)
```

> **Note on Touch Simulation:** The `touchscreen:` reference is required for remote tap simulation (`POST /tap` and interactive clicks in `/remote`). Simulated touches are dispatched directly through ESPHome's `Touchscreen` subsystem, guaranteeing that display wake-on-touch routines and LVGL event listeners trigger identically to physical screen touches.

---

## Endpoints & Protocols

| Endpoint | Method / Protocol | Description |
| :--- | :--- | :--- |
| `/remote` | `GET` | Interactive web mirror with live preview, pointer dragging, touch simulation, and HUD telemetry. |
| `/ws` | `WebSocket` | **Bidirectional streaming channel:**<br>• Device-to-client: Binary delta JPEG tiles (`0x44`) and full keyframes (`0x46`).<br>• Client-to-device: Low-latency touch events (`T`, `D`, `M`, `U`) and keyframe requests (`K`). |
| `/screenshot.jpg` | `GET` | Hardware-accelerated JPEG snapshot of current screen (`/screenshot.jpeg` also supported). |
| `/screenshot.bmp` | `GET` | Uncompressed 16-bit RGB565 BMP snapshot for pixel-perfect debugging. |
| `/mirror/info` | `GET` | JSON metadata (resolution, rotation, byte order, target FPS, JPEG quality, memory stats). |
| `/tap?x=..&y=..` | `POST` | Simulate a touch tap at raw hardware coordinates `(x, y)` (`duration` ms optional, default 180ms). |
| `/touch?x=..&y=..&action=..` | `POST` | Continuous touch state injection (`action=down`, `move`, `up`) for live dragging & holds. |
| `/swipe?start_x=..&start_y=..&end_x=..&end_y=..` | `POST` | Discrete swipe/drag gesture interpolation (`duration` ms optional, default 300ms). |

---

## Roadmap & Status

- [x] **Phase 1: Hardware-Accelerated Snapshots:** Fast JPEG (`/screenshot.jpg`) and uncompressed BMP (`/screenshot.bmp`) endpoints powered by `esp_new_jpeg`.
- [x] **Phase 2: Touch Input Simulation:** ESPHome `Touchscreen` integration for waking sleeping screens, single taps (`POST /tap`), and auto-rotation mapping.
- [x] **Phase 2.5: Interactive Embedded Web UI:** Lightweight HTML5 mirror served from PROGMEM at `/remote` with touch ripple feedback and zero external CDN dependencies.
- [x] **Phase 3: Gestures & Drag Controls:** Swipe gestures (`POST /swipe`), continuous touch tracking (`POST /touch`), and canvas pointer dragging.
- [x] **Phase 4: Real-Time Delta Tile WebSocket Streaming:** Live delta blitting (`/ws`), idle gating, automatic keyframe escalation, and graceful FPS coalescing.
- [ ] **Future / Community Contributions:**
  - `DisplayBuffer` adapter for SPI / I2C displays (e.g. ST7789, ILI9341).
  - Multi-client connection optimizations.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
