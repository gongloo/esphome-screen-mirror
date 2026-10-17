#pragma once

#include <algorithm>
#include <cstdint>

namespace esphome {
namespace screen_mirror {

struct BudgetWaitResult {
  bool timed_out;
  uint32_t wait_us;
};

inline BudgetWaitResult calculate_budget_wait(uint32_t current_elapsed_ms, uint32_t max_budget_ms = 15) {
  if (current_elapsed_ms >= max_budget_ms) {
    return {true, 0};
  }
  uint32_t remaining_ms = max_budget_ms - current_elapsed_ms;
  uint32_t wait_us = std::min<uint32_t>(remaining_ms * 1000, 10000); // At most 10ms select wait
  return {false, wait_us};
}

// DRAM guard threshold check
inline bool is_dram_low_for_delta(size_t free_internal_heap, size_t threshold = 35000) {
  return free_internal_heap < threshold;
}

}  // namespace screen_mirror
}  // namespace esphome
