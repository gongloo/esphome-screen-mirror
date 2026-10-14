# ESPHome Screen Mirror (`screen_mirror`)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![ESPHome External Component](https://img.shields.io/badge/ESPHome-External%20Component-orange.svg)](https://esphome.io)

A lightweight, low-latency screen mirroring and remote touch testing component for ESPHome displays on the ESP32.

Think of it as **VNC / `scrcpy` built directly into your ESP32**, accessible from any browser without installing extra software.

---

## Current Features

- 📸 **Hardware-Accelerated JPEG Snapshots:** High-speed JPEG capture via `GET /screenshot.jpg` powered by ESP-IDF SIMD/hardware acceleration (`esp_new_jpeg`), requiring ~0 ms extra conversion overhead.
- 🖼️ **Raw BMP Snapshots:** Pixel-perfect uncompressed frame capture via `GET /screenshot.bmp` for exact color & alignment debugging.
- 👆 **Direct ESPHome Touch Simulation:** Send taps via `POST /tap?x=...&y=...` with automatic screen-wake support. Taps are routed directly through ESPHome's `Touchscreen` subsystem, executing both user-defined YAML `on_touch:` automations (e.g. waking sleeping displays) and UI listeners (such as LVGL).
- 🌐 **Interactive Web Mirror (`/remote`):** Embedded, standalone HTML5 viewer preprocessed with gzip and served directly from PROGMEM:
  - Automatic canvas rotation & coordinate transformation for 0°, 90°, 180°, and 270° orientations.
  - Interactive click/tap-to-touch with visual ripple feedback.
  - Live latency telemetry and direct full-screen snapshot links.
- 🎨 **Automatic Byte-Order & Endianness Handling:** Automatically detects display and LVGL byte orders (`big_endian` vs `little_endian`) to ensure 100% accurate color reproduction with zero CPU overhead.
- 🔄 **Automatic Orientation Detection:** Seamlessly inherits display/LVGL rotation without requiring manual YAML configuration.
- 🧩 **Display Support:** Supports ESP32-S3 RGB panels (`mipi_rgb`) as well as standard SPI displays implementing `DisplayBuffer` (e.g. ST7789, ILI9341).

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
  fps: 1.0                     # Optional: max framerate (default: 1.0, range: 0.1 - 10.0)
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

## REST Endpoints

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/remote` | `GET` | Interactive web mirror with live preview and touch simulation. |
| `/screenshot.jpg` | `GET` | Hardware-accelerated JPEG snapshot of current screen. |
| `/screenshot.bmp` | `GET` | Uncompressed 16-bit RGB565 BMP snapshot. |
| `/mirror/info` | `GET` | JSON metadata (resolution, rotation, byte order, memory). |
| `/tap?x=..&y=..` | `POST` | Simulate touch event at raw hardware coordinates `(x, y)` (`duration_ms` optional). |

---

## Roadmap & Upcoming Features

- [x] Phase 1: Hardware-accelerated JPEG (`/screenshot.jpg`) and BMP (`/screenshot.bmp`) capture.
- [x] Phase 2: Touch input simulation (`POST /tap`) via ESPHome `Touchscreen` subsystem.
- [x] Phase 2.5: Interactive web UI (`/remote`) with auto-rotation, coordinate transformation, and build-time gzip asset embedding.
- [ ] Phase 3: Swipe & drag gestures (`POST /swipe`, continuous touch dragging).
- [ ] Phase 4: Tile-based delta blitting via WebSockets for ultra-low bandwidth streaming.

For the comprehensive architectural specification, see [SPEC.md](SPEC.md).

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
