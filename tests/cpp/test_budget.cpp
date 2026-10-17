#include <doctest/doctest.h>
#include "screen_mirror/budget_limiter.h"

using namespace esphome::screen_mirror;

TEST_CASE("Socket Budget: Production calculate_budget_wait 15ms limit calculation") {
  SUBCASE("Fresh start (0ms elapsed)") {
    auto res = calculate_budget_wait(0, 15);
    CHECK(res.timed_out == false);
    // Remaining is 15ms (15000 us); clamped to max 10000 us
    CHECK(res.wait_us == 10000);
  }

  SUBCASE("3ms elapsed") {
    auto res = calculate_budget_wait(3, 15);
    CHECK(res.timed_out == false);
    // Remaining is 12ms (12000 us); clamped to 10000 us
    CHECK(res.wait_us == 10000);
  }

  SUBCASE("7ms elapsed (remaining 8ms)") {
    auto res = calculate_budget_wait(7, 15);
    CHECK(res.timed_out == false);
    // Remaining is 8ms (8000 us); less than 10000 us, so exactly 8000 us
    CHECK(res.wait_us == 8000);
  }

  SUBCASE("14ms elapsed (remaining 1ms)") {
    auto res = calculate_budget_wait(14, 15);
    CHECK(res.timed_out == false);
    CHECK(res.wait_us == 1000);
  }

  SUBCASE("Exact budget limit reached (15ms elapsed)") {
    auto res = calculate_budget_wait(15, 15);
    CHECK(res.timed_out == true);
    CHECK(res.wait_us == 0);
  }

  SUBCASE("Budget exceeded (20ms elapsed)") {
    auto res = calculate_budget_wait(20, 15);
    CHECK(res.timed_out == true);
    CHECK(res.wait_us == 0);
  }
}
