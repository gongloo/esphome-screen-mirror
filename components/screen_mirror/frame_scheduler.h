#pragma once

#include <cstdint>

namespace esphome {
namespace screen_mirror {

enum class FrameAction {
  NONE,
  FULL_KEYFRAME,
  DIRTY_DELTA
};

class FrameScheduler {
 public:
  static FrameAction evaluate(uint32_t now_ms, float fps, bool gated_streaming,
                              bool has_dirty_rect, bool has_clients, uint32_t &last_frame_time) {
    if (!has_clients) {
      return FrameAction::NONE;
    }

    uint32_t min_interval_ms = (fps > 0.0f) ? static_cast<uint32_t>(1000.0f / fps) : 66;
    if (now_ms - last_frame_time < min_interval_ms) {
      return FrameAction::NONE;
    }

    if (!gated_streaming) {
      last_frame_time = now_ms;
      return FrameAction::FULL_KEYFRAME;
    }

    if (has_dirty_rect) {
      last_frame_time = now_ms;
      return FrameAction::DIRTY_DELTA;
    }

    return FrameAction::NONE;
  }
};

}  // namespace screen_mirror
}  // namespace esphome
