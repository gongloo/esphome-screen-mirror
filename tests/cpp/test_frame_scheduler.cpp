#include <doctest/doctest.h>
#include "screen_mirror/frame_scheduler.h"

using namespace esphome::screen_mirror;

TEST_CASE("Frame Scheduler: Client connection requirement") {
  uint32_t last_frame_time = 1000;
  // Even if time elapsed and dirty rect present, no clients connected -> NONE
  FrameAction action = FrameScheduler::evaluate(2000, 15.0f, true, true, false, last_frame_time);
  CHECK(action == FrameAction::NONE);
  CHECK(last_frame_time == 1000);
}

TEST_CASE("Frame Scheduler: Rate limit interval throttling") {
  uint32_t last_frame_time = 1000;
  float fps = 20.0f; // min_interval = 1000 / 20 = 50ms

  // At t = 1030 (30ms < 50ms), should throttle
  FrameAction action = FrameScheduler::evaluate(1030, fps, false, false, true, last_frame_time);
  CHECK(action == FrameAction::NONE);
  CHECK(last_frame_time == 1000);

  // At t = 1050 (50ms >= 50ms), ungated streaming triggers FULL_KEYFRAME
  action = FrameScheduler::evaluate(1050, fps, false, false, true, last_frame_time);
  CHECK(action == FrameAction::FULL_KEYFRAME);
  CHECK(last_frame_time == 1050);

  // Immediate next evaluation at t = 1055 throttles again
  action = FrameScheduler::evaluate(1055, fps, false, false, true, last_frame_time);
  CHECK(action == FrameAction::NONE);
  CHECK(last_frame_time == 1050);
}

TEST_CASE("Frame Scheduler: Gated streaming logic") {
  uint32_t last_frame_time = 1000;
  float fps = 10.0f; // min_interval = 100ms

  // Time elapsed (t = 1200), gated = true, but NO dirty rect
  FrameAction action = FrameScheduler::evaluate(1200, fps, true, false, true, last_frame_time);
  CHECK(action == FrameAction::NONE);
  CHECK(last_frame_time == 1000); // Does not advance timestamp if nothing sent

  // Time elapsed (t = 1250), gated = true, WITH dirty rect
  action = FrameScheduler::evaluate(1250, fps, true, true, true, last_frame_time);
  CHECK(action == FrameAction::DIRTY_DELTA);
  CHECK(last_frame_time == 1250);
}

TEST_CASE("Frame Scheduler: Fallback FPS for 0.0 or negative") {
  uint32_t last_frame_time = 1000;
  // fps = 0.0f should fallback to 66ms (approx 15 FPS)
  FrameAction action = FrameScheduler::evaluate(1040, 0.0f, false, false, true, last_frame_time);
  CHECK(action == FrameAction::NONE);

  action = FrameScheduler::evaluate(1070, 0.0f, false, false, true, last_frame_time);
  CHECK(action == FrameAction::FULL_KEYFRAME);
  CHECK(last_frame_time == 1070);
}

TEST_CASE("Frame Scheduler: OTA in progress suppresses all frame emission") {
  uint32_t last_frame_time = 1000;
  float fps = 30.0f;

  // Even if enough time has passed, dirty rect exists, and clients are connected,
  // ota_in_progress = true MUST suppress both keyframes and dirty deltas.
  FrameAction action = FrameScheduler::evaluate(2000, fps, false, false, true, last_frame_time, true);
  CHECK(action == FrameAction::NONE);
  CHECK(last_frame_time == 1000);

  action = FrameScheduler::evaluate(2000, fps, true, true, true, last_frame_time, true);
  CHECK(action == FrameAction::NONE);
  CHECK(last_frame_time == 1000);

  // Once OTA completes (ota_in_progress = false), scheduling resumes immediately
  action = FrameScheduler::evaluate(2000, fps, true, true, true, last_frame_time, false);
  CHECK(action == FrameAction::DIRTY_DELTA);
  CHECK(last_frame_time == 2000);
}

