#include <doctest/doctest.h>
#include <algorithm>
#include <cstdint>

namespace {

struct Point {
  int x;
  int y;
};

Point clamp_coordinates(int x, int y, int hw_w, int hw_h) {
  int clamped_x = std::max(0, std::min(hw_w - 1, x));
  int clamped_y = std::max(0, std::min(hw_h - 1, y));
  return {clamped_x, clamped_y};
}

// DRAM guard check from screen_mirror.cpp
bool should_drop_delta_tile_dram_guard(size_t free_internal_heap, size_t threshold = 35000) {
  return free_internal_heap < threshold;
}

}  // namespace

TEST_CASE("Coordinate Clamping to Hardware Boundaries") {
  const int hw_w = 800;
  const int hw_h = 480;

  SUBCASE("In-bounds points remain unaltered") {
    auto p1 = clamp_coordinates(0, 0, hw_w, hw_h);
    CHECK(p1.x == 0);
    CHECK(p1.y == 0);

    auto p2 = clamp_coordinates(799, 479, hw_w, hw_h);
    CHECK(p2.x == 799);
    CHECK(p2.y == 479);

    auto p3 = clamp_coordinates(400, 240, hw_w, hw_h);
    CHECK(p3.x == 400);
    CHECK(p3.y == 240);
  }

  SUBCASE("Negative coordinates clamp to zero") {
    auto p = clamp_coordinates(-10, -50, hw_w, hw_h);
    CHECK(p.x == 0);
    CHECK(p.y == 0);
  }

  SUBCASE("Over-boundary coordinates clamp to hw - 1") {
    auto p = clamp_coordinates(800, 480, hw_w, hw_h);
    CHECK(p.x == 799);
    CHECK(p.y == 479);

    auto p_far = clamp_coordinates(9999, 9999, hw_w, hw_h);
    CHECK(p_far.x == 799);
    CHECK(p_far.y == 479);
  }

  SUBCASE("Arbitrary non-standard screen sizes") {
    // 240x240 circular/square display
    auto p = clamp_coordinates(300, -5, 240, 240);
    CHECK(p.x == 239);
    CHECK(p.y == 0);
  }
}

TEST_CASE("DRAM Pre-flight Guard Threshold") {
  SUBCASE("Heap well above threshold (e.g. 80 KB free)") {
    CHECK(should_drop_delta_tile_dram_guard(80000) == false);
  }

  SUBCASE("Heap safely above threshold (e.g. 40 KB free)") {
    CHECK(should_drop_delta_tile_dram_guard(40000) == false);
  }

  SUBCASE("Heap precisely at 35 KB boundary") {
    CHECK(should_drop_delta_tile_dram_guard(35000) == false);
  }

  SUBCASE("Heap critically below 35 KB (e.g. 34.9 KB free)") {
    CHECK(should_drop_delta_tile_dram_guard(34999) == true);
  }

  SUBCASE("Severe memory pressure (e.g. 10 KB free)") {
    CHECK(should_drop_delta_tile_dram_guard(10000) == true);
  }
}
