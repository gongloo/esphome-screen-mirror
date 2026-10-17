#include <doctest/doctest.h>
#include "screen_mirror/swipe_gesture.h"

using namespace esphome::screen_mirror;

TEST_CASE("Swipe Gesture: Controller basic lifecycle and interpolation") {
  SwipeGestureController ctrl;
  CHECK(ctrl.is_active() == false);

  SwipeStepOutput out;
  CHECK(ctrl.step(100, out) == false);

  // Start a swipe from (100, 200) to (300, 600) over 200ms at t = 1000
  ctrl.start(100, 200, 300, 600, 200, 1000, 42);
  CHECK(ctrl.is_active() == true);
  CHECK(ctrl.get_client_id() == 42);

  // Attempt step too soon (< 16ms)
  CHECK(ctrl.step(1008, out) == false);

  // Step at t = 1050 (50ms elapsed of 200ms -> 25% progress)
  // expected x = 100 + (300 - 100) * 0.25 = 150
  // expected y = 200 + (600 - 200) * 0.25 = 300
  CHECK(ctrl.step(1050, out) == true);
  CHECK(out.is_final == false);
  CHECK(out.x == 150);
  CHECK(out.y == 300);

  // Step at t = 1100 (100ms elapsed of 200ms -> 50% progress)
  // expected x = 100 + 200 * 0.50 = 200
  // expected y = 200 + 400 * 0.50 = 400
  CHECK(ctrl.step(1100, out) == true);
  CHECK(out.is_final == false);
  CHECK(out.x == 200);
  CHECK(out.y == 400);

  // Step at t = 1150 (150ms elapsed -> 75% progress)
  // expected x = 100 + 200 * 0.75 = 250
  // expected y = 200 + 400 * 0.75 = 500
  CHECK(ctrl.step(1150, out) == true);
  CHECK(out.is_final == false);
  CHECK(out.x == 250);
  CHECK(out.y == 500);

  // Step at t = 1200 (200ms elapsed -> final step)
  CHECK(ctrl.step(1200, out) == true);
  CHECK(out.is_final == true);
  CHECK(out.x == 300);
  CHECK(out.y == 600);
  CHECK(ctrl.is_active() == false);

  // Subsequent step returns false because swipe has finished
  CHECK(ctrl.step(1220, out) == false);
}

TEST_CASE("Swipe Gesture: Zero duration guard") {
  SwipeGestureController ctrl;
  // duration 0 should be guarded to at least 1ms to avoid divide by zero
  ctrl.start(0, 0, 100, 100, 0, 500, 1);
  CHECK(ctrl.is_active() == true);

  SwipeStepOutput out;
  // Step after 16ms
  CHECK(ctrl.step(520, out) == true);
  CHECK(out.is_final == true);
  CHECK(out.x == 100);
  CHECK(out.y == 100);
  CHECK(ctrl.is_active() == false);
}

TEST_CASE("Swipe Gesture: Cancellation") {
  SwipeGestureController ctrl;
  ctrl.start(0, 0, 100, 100, 500, 1000);
  CHECK(ctrl.is_active() == true);

  ctrl.cancel();
  CHECK(ctrl.is_active() == false);

  SwipeStepOutput out;
  CHECK(ctrl.step(1050, out) == false);
}
