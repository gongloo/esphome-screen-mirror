#include <doctest/doctest.h>
#include <cstdint>
#include <sys/time.h>
#include <algorithm>

namespace {

struct SocketBudgetResult {
  bool timed_out;
  struct timeval tv;
};

SocketBudgetResult calculate_socket_timeout(int64_t start_time_us, int64_t current_time_us, int64_t budget_us = 15000) {
  int64_t elapsed = current_time_us - start_time_us;
  if (elapsed >= budget_us) {
    return {true, {0, 0}};
  }
  int64_t remaining_us = budget_us - elapsed;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = static_cast<suseconds_t>(remaining_us > 10000 ? 10000 : remaining_us);
  return {false, tv};
}

}  // namespace

TEST_CASE("Socket Budget: Dynamic 15ms timeout calculation") {
  const int64_t start = 1000000; // t = 1.0s

  SUBCASE("Fresh start (0ms elapsed)") {
    auto res = calculate_socket_timeout(start, start);
    CHECK(res.timed_out == false);
    CHECK(res.tv.tv_sec == 0);
    // Remaining is 15000 us; clamped to max 10000 us
    CHECK(res.tv.tv_usec == 10000);
  }

  SUBCASE("3ms elapsed") {
    auto res = calculate_socket_timeout(start, start + 3000);
    CHECK(res.timed_out == false);
    CHECK(res.tv.tv_sec == 0);
    // Remaining is 12000 us; clamped to 10000 us
    CHECK(res.tv.tv_usec == 10000);
  }

  SUBCASE("7ms elapsed (remaining 8ms)") {
    auto res = calculate_socket_timeout(start, start + 7000);
    CHECK(res.timed_out == false);
    CHECK(res.tv.tv_sec == 0);
    // Remaining is 8000 us; less than 10000 us, so exactly 8000 us
    CHECK(res.tv.tv_usec == 8000);
  }

  SUBCASE("14.5ms elapsed (remaining 500us)") {
    auto res = calculate_socket_timeout(start, start + 14500);
    CHECK(res.timed_out == false);
    CHECK(res.tv.tv_sec == 0);
    CHECK(res.tv.tv_usec == 500);
  }

  SUBCASE("Exact budget limit reached (15ms elapsed)") {
    auto res = calculate_socket_timeout(start, start + 15000);
    CHECK(res.timed_out == true);
  }

  SUBCASE("Budget exceeded (20ms elapsed)") {
    auto res = calculate_socket_timeout(start, start + 20000);
    CHECK(res.timed_out == true);
  }
}
