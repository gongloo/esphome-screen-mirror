#include <doctest/doctest.h>
#include <cstdint>
#include <vector>

namespace {

enum class TouchEventType {
  DOWN,
  MOVE,
  UP
};

struct TouchRecord {
  int client_fd;
  TouchEventType type;
  uint16_t x;
  uint16_t y;
};

class TouchLeaseManager {
 public:
  int touch_active_client_fd_{-1};
  uint32_t touch_last_interaction_ms_{0};
  std::vector<TouchRecord> dispatched_events;

  bool handle_touch(int client_fd, uint8_t type, uint16_t x, uint16_t y, uint32_t now_ms) {
    // Exclusive multi-user touch lease logic
    if (touch_active_client_fd_ != -1 && touch_active_client_fd_ != client_fd) {
      if (now_ms - touch_last_interaction_ms_ < 3000) {
        // Lease active and owned by another client; reject
        return false;
      }
      // Previous client timed out; clear it out
      touch_active_client_fd_ = -1;
    }

    touch_last_interaction_ms_ = now_ms;

    if (type == 0x01) { // DOWN
      touch_active_client_fd_ = client_fd;
      dispatched_events.push_back({client_fd, TouchEventType::DOWN, x, y});
      return true;
    } else if (type == 0x02) { // MOVE
      if (touch_active_client_fd_ == client_fd) {
        dispatched_events.push_back({client_fd, TouchEventType::MOVE, x, y});
        return true;
      }
    } else if (type == 0x03) { // UP
      if (touch_active_client_fd_ == client_fd) {
        dispatched_events.push_back({client_fd, TouchEventType::UP, x, y});
        touch_active_client_fd_ = -1;
        return true;
      }
    }
    return false;
  }

  void handle_disconnect(int client_fd) {
    if (touch_active_client_fd_ == client_fd) {
      touch_active_client_fd_ = -1;
    }
  }
};

}  // namespace

TEST_CASE("Touch Lease: Single client lifecycle") {
  TouchLeaseManager mgr;

  // Touch Down at t = 1000
  CHECK(mgr.handle_touch(10, 0x01, 100, 200, 1000) == true);
  CHECK(mgr.touch_active_client_fd_ == 10);
  CHECK(mgr.dispatched_events.size() == 1);
  CHECK(mgr.dispatched_events.back().type == TouchEventType::DOWN);

  // Touch Move at t = 1050
  CHECK(mgr.handle_touch(10, 0x02, 110, 210, 1050) == true);
  CHECK(mgr.touch_active_client_fd_ == 10);
  CHECK(mgr.dispatched_events.size() == 2);
  CHECK(mgr.dispatched_events.back().type == TouchEventType::MOVE);

  // Touch Up at t = 1100
  CHECK(mgr.handle_touch(10, 0x03, 110, 210, 1100) == true);
  CHECK(mgr.touch_active_client_fd_ == -1); // Released
  CHECK(mgr.dispatched_events.size() == 3);
  CHECK(mgr.dispatched_events.back().type == TouchEventType::UP);
}

TEST_CASE("Touch Lease: Multi-user contention") {
  TouchLeaseManager mgr;

  // Client A presses down at t = 1000
  CHECK(mgr.handle_touch(10, 0x01, 100, 100, 1000) == true);
  CHECK(mgr.touch_active_client_fd_ == 10);

  // Client B tries to press down at t = 1500 (< 3000ms lease)
  CHECK(mgr.handle_touch(20, 0x01, 300, 300, 1500) == false);
  CHECK(mgr.touch_active_client_fd_ == 10); // Still owned by Client A

  // Client B tries to send move at t = 1600
  CHECK(mgr.handle_touch(20, 0x02, 305, 305, 1600) == false);

  // Client B tries to send up at t = 1700
  CHECK(mgr.handle_touch(20, 0x03, 305, 305, 1700) == false);

  // Client A moves at t = 1800
  CHECK(mgr.handle_touch(10, 0x02, 105, 105, 1800) == true);

  // Client A releases touch at t = 2000
  CHECK(mgr.handle_touch(10, 0x03, 105, 105, 2000) == true);
  CHECK(mgr.touch_active_client_fd_ == -1);

  // Client B can now acquire lease immediately
  CHECK(mgr.handle_touch(20, 0x01, 300, 300, 2050) == true);
  CHECK(mgr.touch_active_client_fd_ == 20);
}

TEST_CASE("Touch Lease: Timeout takeover on unresponsive client") {
  TouchLeaseManager mgr;

  // Client A presses down at t = 1000
  CHECK(mgr.handle_touch(10, 0x01, 100, 100, 1000) == true);
  CHECK(mgr.touch_active_client_fd_ == 10);

  // Client A hangs (never sends UP). At t = 4500 (3500ms > 3000ms), Client B sends DOWN
  CHECK(mgr.handle_touch(20, 0x01, 400, 400, 4500) == true);
  CHECK(mgr.touch_active_client_fd_ == 20); // Lease successfully reassigned to Client B
}

TEST_CASE("Touch Lease: Client disconnect immediately clears lease") {
  TouchLeaseManager mgr;

  // Client A acquires lease
  CHECK(mgr.handle_touch(10, 0x01, 100, 100, 1000) == true);
  CHECK(mgr.touch_active_client_fd_ == 10);

  // Client A abruptly disconnects
  mgr.handle_disconnect(10);
  CHECK(mgr.touch_active_client_fd_ == -1);

  // Client B can immediately acquire lease without waiting 3 seconds
  CHECK(mgr.handle_touch(20, 0x01, 200, 200, 1050) == true);
  CHECK(mgr.touch_active_client_fd_ == 20);
}
