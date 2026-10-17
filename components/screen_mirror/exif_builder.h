#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace esphome {
namespace screen_mirror {

// Macro defining the 38-byte SOI + EXIF APP1 metadata block.
// Combining SOI (FF D8) and APP1 (FF E1 ...) into a single compile-time table allows
// zero-copy chunked streaming directly from Flash (.rodata) without any memmove or memcpy.
#define EXIF_SOI_ORIENTATION_HEADER(orient_val) \
  { \
    0xFF, 0xD8,                                         /* SOI */ \
    0xFF, 0xE1,                                         /* APP1 marker */ \
    0x00, 0x22,                                         /* Length = 34 bytes (2-byte length + 32-byte payload) */ \
    'E', 'x', 'i', 'f', 0x00, 0x00,                     /* Exif identifier */ \
    0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,     /* TIFF Header: "II" (little-endian), 42, IFD0 offset 8 */ \
    0x01, 0x00,                                         /* IFD0: 1 directory entry */ \
    0x12, 0x01,                                         /* Tag: 0x0112 (Orientation) */ \
    0x03, 0x00,                                         /* Type: 3 (SHORT) */ \
    0x01, 0x00, 0x00, 0x00,                             /* Count: 1 */ \
    static_cast<uint8_t>(orient_val), 0x00, 0x00, 0x00, /* Value (SHORT) + 2 bytes padding */ \
    0x00, 0x00, 0x00, 0x00                              /* Next IFD offset (0 = none) */ \
  }

// Standard EXIF Orientation values corresponding to display rotation angles:
// - Rotation 90°:  Display rotated 90° CW, so viewer needs Rotate 270° CW / 90° CCW (EXIF 8)
// - Rotation 180°: Display rotated 180°, so viewer needs Rotate 180° (EXIF 3)
// - Rotation 270°: Display rotated 270° CW, so viewer needs Rotate 90° CW (EXIF 6)
static constexpr uint8_t EXIF_SOI_ROT_90[38]  = EXIF_SOI_ORIENTATION_HEADER(8);
static constexpr uint8_t EXIF_SOI_ROT_180[38] = EXIF_SOI_ORIENTATION_HEADER(3);
static constexpr uint8_t EXIF_SOI_ROT_270[38] = EXIF_SOI_ORIENTATION_HEADER(6);

inline const uint8_t *get_exif_soi_header(uint16_t rotation, size_t &header_len) {
  switch (rotation) {
    case 90:
      header_len = sizeof(EXIF_SOI_ROT_90);
      return EXIF_SOI_ROT_90;
    case 180:
      header_len = sizeof(EXIF_SOI_ROT_180);
      return EXIF_SOI_ROT_180;
    case 270:
      header_len = sizeof(EXIF_SOI_ROT_270);
      return EXIF_SOI_ROT_270;
    default:
      header_len = 0;
      return nullptr;
  }
}

}  // namespace screen_mirror
}  // namespace esphome
