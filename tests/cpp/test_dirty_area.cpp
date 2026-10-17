#include <doctest/doctest.h>
#include "screen_mirror/dirty_area.h"

using namespace esphome::screen_mirror;

TEST_CASE("Dirty Area: Initial state and empty take") {
  DirtyAreaTracker tracker;
  CHECK(tracker.has_dirty() == false);

  int x1 = -1, y1 = -1, x2 = -1, y2 = -1;
  CHECK(tracker.take_dirty(x1, y1, x2, y2) == false);
  CHECK(x1 == -1);
}

TEST_CASE("Dirty Area: MCU 16-pixel snapping and coordinate clamping") {
  DirtyAreaTracker tracker;
  // Screen size: 800x480
  // Add rect (5, 18) to (25, 30)
  // Expected snapped:
  // x1: (5 / 16) * 16 = 0
  // y1: (18 / 16) * 16 = 16
  // x2: ((25 + 16) / 16) * 16 - 1 = 31
  // y2: ((30 + 16) / 16) * 16 - 1 = 31
  tracker.add(5, 18, 25, 30, 800, 480);
  CHECK(tracker.has_dirty() == true);

  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  CHECK(tracker.take_dirty(x1, y1, x2, y2) == true);
  CHECK(x1 == 0);
  CHECK(y1 == 16);
  CHECK(x2 == 31);
  CHECK(y2 == 31);

  // After taking, tracker should be reset
  CHECK(tracker.has_dirty() == false);
  CHECK(tracker.take_dirty(x1, y1, x2, y2) == false);
}

TEST_CASE("Dirty Area: Boundary clamping at screen edges") {
  DirtyAreaTracker tracker;
  // Screen size: 320x240
  // Add coordinates extending beyond boundaries
  tracker.add(-20, -50, 350, 260, 320, 240);
  CHECK(tracker.has_dirty() == true);

  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  CHECK(tracker.take_dirty(x1, y1, x2, y2) == true);
  CHECK(x1 == 0);
  CHECK(y1 == 0);
  CHECK(x2 == 319);
  CHECK(y2 == 239);
}

TEST_CASE("Dirty Area: Multiple dirty area union merging") {
  DirtyAreaTracker tracker;
  // First rect: (32, 32) to (63, 63)
  tracker.add(32, 32, 63, 63, 800, 480);
  // Second rect: (100, 100) to (150, 150)
  // 100 -> snap_x1 = 96, snap_y1 = 96
  // 150 -> snap_x2 = 159, snap_y2 = 159
  tracker.add(100, 100, 150, 150, 800, 480);

  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  CHECK(tracker.take_dirty(x1, y1, x2, y2) == true);
  CHECK(x1 == 32);
  CHECK(y1 == 32);
  CHECK(x2 == 159);
  CHECK(y2 == 159);
}

TEST_CASE("Dirty Area: Manual clear()") {
  DirtyAreaTracker tracker;
  tracker.add(10, 10, 50, 50, 800, 480);
  CHECK(tracker.has_dirty() == true);

  tracker.clear();
  CHECK(tracker.has_dirty() == false);
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  CHECK(tracker.take_dirty(x1, y1, x2, y2) == false);
}

TEST_CASE("Dirty Area: Escalation to full keyframe check") {
  // Screen: 800 x 480 = 384,000 pixels
  // Threshold 60% = 230,400 pixels

  // Small button update: 100 x 50 = 5,000 (< 60%)
  CHECK(DirtyAreaTracker::should_escalate_to_keyframe(100, 50, 800, 480, 60) == false);

  // Large scroll update: 600 x 400 = 240,000 (>= 60%)
  CHECK(DirtyAreaTracker::should_escalate_to_keyframe(600, 400, 800, 480, 60) == true);

  // Exactly 60%: 480 x 480 = 230,400 (== 60%)
  CHECK(DirtyAreaTracker::should_escalate_to_keyframe(480, 480, 800, 480, 60) == true);

  // Invalid screen size guard
  CHECK(DirtyAreaTracker::should_escalate_to_keyframe(100, 100, 0, 480, 60) == true);
  CHECK(DirtyAreaTracker::should_escalate_to_keyframe(100, 100, 800, 0, 60) == true);
}
