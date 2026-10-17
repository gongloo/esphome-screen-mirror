#pragma once

#include <cstdint>
#include <cstring>

namespace esphome {
namespace screen_mirror {

inline uint16_t swap_rgb565(uint16_t v) {
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

inline void swap_rgb565_buffer(const uint8_t *src, uint8_t *dst, size_t num_bytes) {
  if (src == nullptr || dst == nullptr) return;
  for (size_t i = 0; i + 1 < num_bytes; i += 2) {
    uint8_t b0 = src[i];
    uint8_t b1 = src[i + 1];
    dst[i] = b1;
    dst[i + 1] = b0;
  }
}

inline bool build_bmp_header(int width, int height, uint8_t *header_66) {
  if (header_66 == nullptr) return false;

  std::memset(header_66, 0, 66);
  uint32_t image_size = static_cast<uint32_t>(width) * height * 2;
  uint32_t file_size = 66 + image_size;

  // Bitmap File Header (14 bytes)
  header_66[0] = 'B';
  header_66[1] = 'M';
  header_66[2] = static_cast<uint8_t>(file_size);
  header_66[3] = static_cast<uint8_t>(file_size >> 8);
  header_66[4] = static_cast<uint8_t>(file_size >> 16);
  header_66[5] = static_cast<uint8_t>(file_size >> 24);
  header_66[10] = 66; // Data offset (14 file header + 40 DIB header + 12 bitmasks)

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  header_66[14] = 40; // Header size
  header_66[18] = static_cast<uint8_t>(width);
  header_66[19] = static_cast<uint8_t>(width >> 8);
  header_66[20] = static_cast<uint8_t>(width >> 16);
  header_66[21] = static_cast<uint8_t>(width >> 24);

  // Negative height specifies top-down bitmap (origin upper-left)
  int32_t neg_height = -height;
  header_66[22] = static_cast<uint8_t>(neg_height);
  header_66[23] = static_cast<uint8_t>(neg_height >> 8);
  header_66[24] = static_cast<uint8_t>(neg_height >> 16);
  header_66[25] = static_cast<uint8_t>(neg_height >> 24);
  header_66[26] = 1;  // Planes
  header_66[28] = 16; // Bits per pixel (RGB565)
  header_66[30] = 3;  // BI_BITFIELDS (required for 16-bit RGB565, value 3)
  header_66[34] = static_cast<uint8_t>(image_size);
  header_66[35] = static_cast<uint8_t>(image_size >> 8);
  header_66[36] = static_cast<uint8_t>(image_size >> 16);
  header_66[37] = static_cast<uint8_t>(image_size >> 24);
  header_66[38] = 0x13; // 2835 ppm (~72 DPI)
  header_66[39] = 0x0B;
  header_66[42] = 0x13;
  header_66[43] = 0x0B;

  // BI_BITFIELDS RGB channel bitmasks (12 bytes, 3x DWORD little-endian)
  // Red mask: 0x0000F800 (bits 11-15)
  header_66[54] = 0x00;
  header_66[55] = 0xF8;
  header_66[56] = 0x00;
  header_66[57] = 0x00;

  // Green mask: 0x000007E0 (bits 5-10)
  header_66[58] = 0xE0;
  header_66[59] = 0x07;
  header_66[60] = 0x00;
  header_66[61] = 0x00;

  // Blue mask: 0x0000001F (bits 0-4)
  header_66[62] = 0x1F;
  header_66[63] = 0x00;
  header_66[64] = 0x00;
  header_66[65] = 0x00;

  return true;
}

}  // namespace screen_mirror
}  // namespace esphome
