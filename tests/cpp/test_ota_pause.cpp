#include <doctest/doctest.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include "screen_mirror/dirty_area.h"
#include "screen_mirror/frame_scheduler.h"

namespace {

enum class MockOTAState {
  OTA_COMPLETED = 0,
  OTA_STARTED,
  OTA_IN_PROGRESS,
  OTA_ABORT,
  OTA_ERROR,
};

class MockScreenMirrorOTAState {
 public:
  bool is_ota_in_progress() const { return ota_in_progress_; }

  void add_ws_client(int fd) {
    if (ota_in_progress_) {
      closed_fds_.push_back(fd);
      return;
    }
    ws_client_fds_.push_back(fd);
  }

  void on_ota_state(MockOTAState state) {
    if (state == MockOTAState::OTA_STARTED || state == MockOTAState::OTA_IN_PROGRESS) {
      if (ota_in_progress_) return;
      ota_in_progress_ = true;

      // Close all active clients
      for (int fd : ws_client_fds_) {
        closed_fds_.push_back(fd);
      }
      ws_client_fds_.clear();
      dirty_tracker_.clear();
      dirty_ = false;
    } else if (state == MockOTAState::OTA_COMPLETED ||
               state == MockOTAState::OTA_ABORT ||
               state == MockOTAState::OTA_ERROR) {
      ota_in_progress_ = false;
      dirty_ = true;
    }
  }

  void add_dirty_area(int x1, int y1, int x2, int y2, int w, int h) {
    if (ota_in_progress_) return;
    dirty_tracker_.add(x1, y1, x2, y2, w, h);
  }

  const std::vector<int> &get_active_clients() const { return ws_client_fds_; }
  const std::vector<int> &get_closed_clients() const { return closed_fds_; }
  bool has_dirty() const { return dirty_tracker_.has_dirty(); }

 private:
  std::atomic<bool> ota_in_progress_{false};
  bool dirty_{true};
  std::vector<int> ws_client_fds_{};
  std::vector<int> closed_fds_{};
  esphome::screen_mirror::DirtyAreaTracker dirty_tracker_{};
};

}  // namespace

TEST_CASE("OTA Pause: Lifecycle state transitions") {
  MockScreenMirrorOTAState mirror;
  CHECK_FALSE(mirror.is_ota_in_progress());

  // Connect two clients before OTA
  mirror.add_ws_client(10);
  mirror.add_ws_client(11);
  CHECK(mirror.get_active_clients().size() == 2);

  // Dirty rect accumulated before OTA
  mirror.add_dirty_area(10, 10, 50, 50, 800, 480);
  CHECK(mirror.has_dirty());

  // 1. OTA Starts
  mirror.on_ota_state(MockOTAState::OTA_STARTED);
  CHECK(mirror.is_ota_in_progress());
  CHECK(mirror.get_active_clients().empty());
  CHECK(mirror.get_closed_clients().size() == 2);
  CHECK(mirror.get_closed_clients()[0] == 10);
  CHECK(mirror.get_closed_clients()[1] == 11);
  CHECK_FALSE(mirror.has_dirty());

  // 2. Incoming connection while OTA in progress is immediately rejected
  mirror.add_ws_client(12);
  CHECK(mirror.get_active_clients().empty());
  CHECK(mirror.get_closed_clients().size() == 3);
  CHECK(mirror.get_closed_clients()[2] == 12);

  // 3. Dirty updates while OTA in progress are dropped
  mirror.add_dirty_area(20, 20, 60, 60, 800, 480);
  CHECK_FALSE(mirror.has_dirty());

  // 4. In progress notifications don't re-trigger or alter state
  mirror.on_ota_state(MockOTAState::OTA_IN_PROGRESS);
  CHECK(mirror.is_ota_in_progress());

  // 5. OTA Finishes (completed)
  mirror.on_ota_state(MockOTAState::OTA_COMPLETED);
  CHECK_FALSE(mirror.is_ota_in_progress());

  // New clients and dirty regions work again
  mirror.add_ws_client(13);
  CHECK(mirror.get_active_clients().size() == 1);
  mirror.add_dirty_area(0, 0, 10, 10, 800, 480);
  CHECK(mirror.has_dirty());
}

TEST_CASE("OTA Pause: Error and Abort recovery") {
  MockScreenMirrorOTAState mirror;

  mirror.on_ota_state(MockOTAState::OTA_STARTED);
  CHECK(mirror.is_ota_in_progress());

  // Abort resets in_progress flag
  mirror.on_ota_state(MockOTAState::OTA_ABORT);
  CHECK_FALSE(mirror.is_ota_in_progress());

  mirror.on_ota_state(MockOTAState::OTA_STARTED);
  CHECK(mirror.is_ota_in_progress());

  // Error resets in_progress flag
  mirror.on_ota_state(MockOTAState::OTA_ERROR);
  CHECK_FALSE(mirror.is_ota_in_progress());
}
