#pragma once

#include <cstdint>

namespace esphome {
namespace screen_mirror {

struct SwipeStepOutput {
  int x{0};
  int y{0};
  bool is_final{false};
};

class SwipeGestureController {
 public:
  void start(int start_x, int start_y, int end_x, int end_y, uint16_t duration_ms, uint32_t now_ms, int client_id = 0) {
    active_ = true;
    start_x_ = start_x;
    start_y_ = start_y;
    end_x_ = end_x;
    end_y_ = end_y;
    duration_ms_ = (duration_ms == 0) ? 1 : duration_ms;
    start_time_ = now_ms;
    last_step_time_ = now_ms;
    client_id_ = client_id;
  }

  // Returns true if a step was produced (either an intermediate move or final position)
  bool step(uint32_t now_ms, SwipeStepOutput &out) {
    if (!active_) {
      return false;
    }

    // Step at ~16ms intervals (approx 60 Hz)
    if (now_ms - last_step_time_ < 16) {
      return false;
    }

    last_step_time_ = now_ms;
    uint32_t elapsed = now_ms - start_time_;

    if (elapsed >= duration_ms_) {
      out.x = end_x_;
      out.y = end_y_;
      out.is_final = true;
      active_ = false;
      return true;
    }

    float progress = static_cast<float>(elapsed) / static_cast<float>(duration_ms_);
    out.x = start_x_ + static_cast<int>((end_x_ - start_x_) * progress);
    out.y = start_y_ + static_cast<int>((end_y_ - start_y_) * progress);
    out.is_final = false;
    return true;
  }

  bool is_active() const { return active_; }
  int get_client_id() const { return client_id_; }
  void cancel() { active_ = false; }

 private:
  bool active_{false};
  int start_x_{0};
  int start_y_{0};
  int end_x_{0};
  int end_y_{0};
  uint32_t start_time_{0};
  uint32_t duration_ms_{0};
  uint32_t last_step_time_{0};
  int client_id_{0};
};

}  // namespace screen_mirror
}  // namespace esphome
