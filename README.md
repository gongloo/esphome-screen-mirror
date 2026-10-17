# ESPHome Screen Mirror (`screen_mirror`)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![ESPHome External Component](https://img.shields.io/badge/ESPHome-External%20Component-orange.svg)](https://esphome.io)

A lightweight, low-latency screen mirror and remote touch controller for ESPHome displays on the ESP32-S3.

Think of it as **VNC / `scrcpy` built directly into your ESP32**, accessible from any browser without installing extra software.

---

### Key Use Cases

- 🛋️ **Remote HMI Control:** Monitor and interact with wall-mounted dashboards, campervan consoles, or enclosed touchscreens directly from your phone or desktop.
- 🧪 **Headless UI Testing:** Automate end-to-end interface testing in CI/CD using Python, Playwright, or `curl` to capture screenshots and inject touch events before firmware deploys.
- 🤖 **AI Agent Pair Programming:** Enable autonomous AI coding agents to tweak LVGL layouts in YAML, pull visual snapshots to inspect alignments, and tap through screens in a closed loop.

---

## Quickstart

Add `screen_mirror` to your ESPHome configuration:

```yaml
external_components:
  - source: github://gongloo/esphome-screen-mirror
    components: [screen_mirror]

web_server:
  port: 80

screen_mirror:
  display: my_display
  touchscreen: my_touchscreen
```

Flash your device and navigate to **`http://<device-ip>/remote`** in your browser.

---

## Features

- 🌐 **Interactive Web Remote (`/remote`):** Lightweight HTML5 viewer served directly from flash memory with canvas rotation, real-time pointer dragging, touch ripple feedback, and live FPS/latency telemetry.
- ⚡ **Ultra-Efficient Delta Streaming:** Transmits only modified screen regions over WebSocket as binary JPEG tiles (~1–5 KB/s under active interaction), dropping to zero network and CPU overhead when the screen is idle.
- 👆 **Native Touch & Gesture Injection:** Supports single taps (`POST /tap`), continuous drag/holds (`POST /touch`), and swipes (`POST /swipe`). Injected touches route through ESPHome's native `touchscreen` subsystem, waking sleeping screens and triggering standard LVGL events.
- 📸 **Fast Snapshots for CI & AI:** High-speed JPEG capture via hardware acceleration (`GET /screenshot.jpg`) and raw uncompressed BMP (`GET /screenshot.bmp`) for pixel-perfect assertions.
- 🔄 **Zero-Config Display Matching:** Automatically detects display rotation and byte-order / endianness from your LVGL and display settings.

<details>
<summary><b>How It Works Under the Hood</b></summary>
<br>

- **Direct PSRAM Framebuffer Access:** Reads the live scanout buffer directly via ESP-IDF's `esp_lcd_rgb_panel_get_frame_buffer()`, bypassing intermediate rendering copies.
- **LVGL Pipeline Hook:** Hooks into `LV_EVENT_FLUSH_START` to intercept dirty bounding boxes as they are drawn by LVGL.
- **Hardware SIMD JPEG Compression:** Encodes tiles in real time using ESP32-S3 hardware acceleration (`esp_new_jpeg`).
- **Dynamic Keyframe Escalation:** If an animation or screen change updates $\ge 60\%$ of the display area, the stream automatically escalates to a full keyframe to prevent visual tearing.
- **Binary Wire Protocol:** Streams delta tiles over WebSocket using compact binary frames (`0x44` for delta tiles, `0x46` for keyframes) and receives low-latency touch packets directly from the web client.

</details>

---

## Hardware & Requirements

### Supported
- **SoC:** ESP32-S3 with PSRAM enabled.
- **Framework:** ESP-IDF only (`framework: type: esp-idf`).
- **Displays:** RGB parallel panels driven by the ESPHome **`mipi_rgb`** component (e.g. Waveshare 4.3"/7", Guition ESP32-S3-4848S040, Sunton ESP32-S3 800x480).
- **UI Framework:** LVGL is recommended for real-time delta WebSocket streaming (`/ws`). Non-LVGL setups can still capture static HTTP snapshots.

### Current Limitations
- **SPI / I2C / 8080 Displays (`st7789v`, `ili9341`, etc.):** The component reads directly from the hardware scanout framebuffer in PSRAM. Standard SPI/I2C `DisplayBuffer` readback is not currently supported.
- **Monochrome & E-Paper:** Requires 16-bit RGB565 framebuffers; 1-bit bitmasks and grayscale LUTs are not supported.

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
  display: rpi_disp            # Required: ID of your display component
  touchscreen: my_touchscreen  # Optional: ID of your touchscreen (required for touch simulation & web clicks)
  fps: 1.0                     # Optional: max delta framerate (default: 1.0, range: 0.1 - 30.0)
  jpeg_quality: 75             # Optional: JPEG quality (default: 75, range: 10 - 100)
  gated_streaming: true        # Optional: pause streaming when screen is static (default: true)
  web_path: "/remote"          # Optional: web viewer path (default: /remote)
  show_telemetry: true         # Optional: show HUD badge in web viewer (default: true)
  rotation: 90                 # Optional: 0, 90, 180, 270 (auto-detected if omitted)
  byte_order: big_endian       # Optional: big_endian, little_endian (auto-detected if omitted)
  core: 0                      # Optional: ESP32 CPU core for worker task (default: 0)
```

---

## Endpoints & Protocols

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/remote` | `GET` | Interactive web mirror with live stream, pointer dragging, touch ripple feedback, and HUD telemetry. |
| `/ws` | `WebSocket` | Bidirectional binary stream (receives delta JPEG tiles & keyframes, sends low-latency touch events). |
| `/screenshot.jpg` | `GET` | Hardware-accelerated JPEG snapshot (`/screenshot.jpeg` also supported). |
| `/screenshot.bmp` | `GET` | Uncompressed 16-bit RGB565 BMP snapshot for pixel-perfect assertions. |
| `/mirror/info` | `GET` | JSON metadata (resolution, rotation, byte order, target FPS, JPEG quality, memory stats). |
| `/tap?x=..&y=..` | `POST` | Simulate a touch tap at hardware coordinates `(x, y)` (`duration` ms optional, default: 180ms). |
| `/touch?x=..&y=..&action=..` | `POST` | Inject continuous touch state (`action=down`, `move`, `up`) for live dragging & holds. |
| `/swipe?start_x=..&start_y=..&end_x=..&end_y=..` | `POST` | Simulate a smooth swipe gesture (`duration` ms optional, default: 300ms). |
