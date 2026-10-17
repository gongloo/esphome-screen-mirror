#include <doctest/doctest.h>
#include "screen_mirror/protocol.h"
#include "screen_mirror/budget_limiter.h"

using namespace esphome::screen_mirror;

TEST_CASE("Coordinate Clamping via production clamp_coordinates") {
  const int hw_w = 800;
  const int hw_h = 480;

  SUBCASE("In-bounds points remain unaltered") {
    int x = 0, y = 0;
    clamp_coordinates(x, y, hw_w, hw_h);
    CHECK(x == 0);
    CHECK(y == 0);

    x = 799; y = 479;
    clamp_coordinates(x, y, hw_w, hw_h);
    CHECK(x == 799);
    CHECK(y == 479);

    x = 400; y = 240;
    clamp_coordinates(x, y, hw_w, hw_h);
    CHECK(x == 400);
    CHECK(y == 240);
  }

  SUBCASE("Negative coordinates clamp to zero") {
    int x = -10, y = -50;
    clamp_coordinates(x, y, hw_w, hw_h);
    CHECK(x == 0);
    CHECK(y == 0);
  }

  SUBCASE("Over-boundary coordinates clamp to hw - 1") {
    int x = 800, y = 480;
    clamp_coordinates(x, y, hw_w, hw_h);
    CHECK(x == 799);
    CHECK(y == 479);

    x = 9999; y = 9999;
    clamp_coordinates(x, y, hw_w, hw_h);
    CHECK(x == 799);
    CHECK(y == 479);
  }

  SUBCASE("Arbitrary non-standard screen sizes") {
    int x = 300, y = -5;
    clamp_coordinates(x, y, 240, 240);
    CHECK(x == 239);
    CHECK(y == 0);
  }
}

TEST_CASE("DRAM Pre-flight Guard Threshold via production is_dram_low_for_delta") {
  SUBCASE("Heap well above threshold (e.g. 80 KB free)") {
    CHECK(is_dram_low_for_delta(80000) == false);
  }

  SUBCASE("Heap safely above threshold (e.g. 40 KB free)") {
    CHECK(is_dram_low_for_delta(40000) == false);
  }

  SUBCASE("Heap precisely at 35 KB boundary") {
    CHECK(is_dram_low_for_delta(35000) == false);
  }

  SUBCASE("Heap critically below 35 KB (e.g. 34.9 KB free)") {
    CHECK(is_dram_low_for_delta(34999) == true);
  }

  SUBCASE("Severe memory pressure (e.g. 10 KB free)") {
    CHECK(is_dram_low_for_delta(10000) == true);
  }

  SUBCASE("Session keyframe encoder init guard matches threshold") {
    // 35 KB requirement protects against heap exhaustion on ESP32-S3
    constexpr size_t threshold_35k = 35 * 1024;
    CHECK(is_dram_low_for_delta(threshold_35k, threshold_35k) == false);
    CHECK(is_dram_low_for_delta(threshold_35k - 1, threshold_35k) == true);
  }
}
