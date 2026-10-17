#pragma once

#include <cstdint>

namespace esphome {
namespace screen_mirror {

class TouchLeaseManager {
 public:
  static constexpr uint32_t LEASE_TIMEOUT_MS = 3000;

  bool can_process_down(int client_id, uint32_t now_ms, bool is_touch_down) {
    if (is_touch_down && active_client_ != 0 &&
        active_client_ != client_id && (now_ms - last_touch_down_time_ < LEASE_TIMEOUT_MS)) {
      return false; // Active lease held by another client
    }
    return true;
  }

  void record_down(int client_id, uint32_t now_ms) {
    active_client_ = client_id;
    last_touch_down_time_ = now_ms;
  }

  bool can_process_move(int client_id) const {
    if (active_client_ != 0 && active_client_ != client_id) {
      return false;
    }
    return true;
  }

  bool can_process_up(int client_id) const {
    if (active_client_ != 0 && active_client_ != client_id) {
      return false;
    }
    return true;
  }

  void record_up() {
    active_client_ = 0;
  }

  void handle_disconnect(int client_id) {
    if (active_client_ == client_id) {
      active_client_ = 0;
    }
  }

  int get_active_client() const { return active_client_; }
  uint32_t get_last_down_time() const { return last_touch_down_time_; }

 private:
  int active_client_{0};
  uint32_t last_touch_down_time_{0};
};

}  // namespace screen_mirror
}  // namespace esphome
