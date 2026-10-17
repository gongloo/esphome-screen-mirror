#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace screen_mirror {

class DirtyAreaTracker {
 public:
  void add(int x1, int y1, int x2, int y2, int hw_w, int hw_h) {
    if (hw_w <= 0 || hw_h <= 0) return;

    x1 = std::max(0, std::min(hw_w - 1, x1));
    y1 = std::max(0, std::min(hw_h - 1, y1));
    x2 = std::max(0, std::min(hw_w - 1, x2));
    y2 = std::max(0, std::min(hw_h - 1, y2));

    // Snap coordinates to 16-pixel multiples for JPEG MCU compatibility
    int snap_x1 = (x1 / 16) * 16;
    int snap_y1 = (y1 / 16) * 16;
    int snap_x2 = std::min(hw_w - 1, ((x2 + 16) / 16) * 16 - 1);
    int snap_y2 = std::min(hw_h - 1, ((y2 + 16) / 16) * 16 - 1);

    if (!has_dirty_) {
      dirty_x1_ = snap_x1;
      dirty_y1_ = snap_y1;
      dirty_x2_ = snap_x2;
      dirty_y2_ = snap_y2;
      has_dirty_ = true;
    } else {
      dirty_x1_ = std::min(dirty_x1_, snap_x1);
      dirty_y1_ = std::min(dirty_y1_, snap_y1);
      dirty_x2_ = std::max(dirty_x2_, snap_x2);
      dirty_y2_ = std::max(dirty_y2_, snap_y2);
    }
  }

  bool has_dirty() const { return has_dirty_; }

  bool take_dirty(int &out_x1, int &out_y1, int &out_x2, int &out_y2) {
    if (!has_dirty_) return false;
    out_x1 = dirty_x1_;
    out_y1 = dirty_y1_;
    out_x2 = dirty_x2_;
    out_y2 = dirty_y2_;
    has_dirty_ = false;
    return true;
  }

  void clear() { has_dirty_ = false; }

  // Returns true if dirty area covers >= threshold_percent of the total screen
  static bool should_escalate_to_keyframe(int w, int h, int hw_w, int hw_h, int threshold_percent = 60) {
    if (hw_w <= 0 || hw_h <= 0) return true;
    int64_t tile_area = static_cast<int64_t>(w) * h;
    int64_t screen_area = static_cast<int64_t>(hw_w) * hw_h;
    return (tile_area * 100) >= (screen_area * threshold_percent);
  }

  // Extracts a 2D sub-rectangle of 16-bit RGB565 pixels into a contiguous destination buffer
  static bool extract_tile_rgb565(const uint8_t *raw_buf, int hw_w, int hw_h,
                                  int x1, int y1, int w, int h,
                                  uint8_t *tile_scratch, size_t tile_scratch_size) {
    if (raw_buf == nullptr || tile_scratch == nullptr) return false;
    if (w <= 0 || h <= 0 || hw_w <= 0 || hw_h <= 0) return false;
    if (x1 < 0 || y1 < 0 || x1 + w > hw_w || y1 + h > hw_h) return false;
    size_t needed = static_cast<size_t>(w) * h * 2;
    if (needed > tile_scratch_size) return false;

    for (int r = 0; r < h; r++) {
      const uint8_t *src_row = raw_buf + ((y1 + r) * hw_w + x1) * 2;
      uint8_t *dst_row = tile_scratch + (r * w) * 2;
      std::memcpy(dst_row, src_row, w * 2);
    }
    return true;
  }

 private:
  bool has_dirty_{false};
  int dirty_x1_{0};
  int dirty_y1_{0};
  int dirty_x2_{0};
  int dirty_y2_{0};
};

}  // namespace screen_mirror
}  // namespace esphome
