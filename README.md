# ESPHome Screen Mirror (`screen_mirror`)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![ESPHome External Component](https://img.shields.io/badge/ESPHome-External%20Component-orange.svg)](https://esphome.io)

A lightweight, low-latency screen mirroring and remote touch testing component for ESPHome displays on the ESP32.

Think of it as **VNC / `scrcpy` built directly into your ESP32**, accessible from any browser without installing extra software.

---

## Features

- 📸 **Fast JPEG & BMP Snapshots:** Grab the current screen via `GET /screenshot.jpg` or `GET /screenshot.bmp` in milliseconds.
- ⚡ **Gated MJPEG Streaming:** Live video stream (`GET /stream.mjpeg`) that automatically drops to 0 FPS / 0 KB/s when the screen is idle, saving CPU cycles and Wi-Fi airtime.
- 👆 **Remote Touch Simulation:** Send taps via `POST /tap?x=...&y=...` to automate UI navigation or click directly inside the web mirror.
- 🌐 **Interactive Web Mirror:** Embedded, zero-dependency HTML5 viewer served directly from the device (`GET /remote`).
- 🤖 **Automation Ready:** Designed for CI/CD test runners, Python test suites, and AI coding agents to visually verify and navigate UI layouts without physical hardware access.
- 🧩 **Display Abstraction:** Supports both ESP32-S3 RGB panels (`mipi_rgb` + LVGL) and standard SPI displays (`DisplayBuffer` such as ST7789, ILI9341).

---

## Quick Start (Example Configuration)

```yaml
external_components:
  - source: github://gongloo/esphome-screen-mirror
    components: [screen_mirror]

web_server:
  port: 80

screen_mirror:
  id: hmi_mirror
  display: rpi_disp            # Required: reference to display
  touchscreen: my_touchscreen  # Required for tap simulation (/tap & web mirror clicks)
  fps: 1                       # Max streaming framerate
  jpeg_quality: 75             # JPEG quality (1-100)
  gated_streaming: true        # Skip frames when screen has no changes
```

> **Note on Touch Simulation:** The `touchscreen:` reference is required for remote tap simulation (`POST /tap` and interactive clicks in `/remote`). Simulated touches are forwarded directly through ESPHome's `Touchscreen` subsystem, executing any user-defined `on_touch:` automations (such as waking displays from sleep) as well as UI touch listeners (like LVGL).

Once flashed, open your browser:
* **Interactive Web Mirror:** `http://<your-device-ip>/remote`
* **Direct JPEG Snapshot:** `http://<your-device-ip>/screenshot.jpg`
* **Direct MJPEG Stream:** `http://<your-device-ip>/stream.mjpeg`

---

## Architecture & Specifications

For the comprehensive design document, performance benchmarks, and phased roadmap, see [SPEC.md](SPEC.md).

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
