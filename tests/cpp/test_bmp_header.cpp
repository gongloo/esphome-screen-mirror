#include <doctest/doctest.h>
#include "screen_mirror/bmp_builder.h"
#include <cstdint>
#include <vector>

using namespace esphome::screen_mirror;

namespace {

uint32_t read_u32_le(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int32_t read_s32_le(const uint8_t *p) {
  return static_cast<int32_t>(read_u32_le(p));
}

uint16_t read_u16_le(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

TEST_CASE("BMP Header: Real production build_bmp_header 66-byte BI_BITFIELDS") {
  const int32_t width = 800;
  const int32_t height = 480;
  uint8_t hdr[66];

  // Call the actual production build_bmp_header function from bmp_builder.h
  REQUIRE(build_bmp_header(width, height, hdr) == true);

  SUBCASE("BITMAPFILEHEADER (Bytes 0-13)") {
    CHECK(hdr[0] == 'B');
    CHECK(hdr[1] == 'M');
    uint32_t expected_file_size = 66 + (800 * 480 * 2);
    CHECK(read_u32_le(&hdr[2]) == expected_file_size);
    CHECK(read_u32_le(&hdr[6]) == 0);   // Reserved
    CHECK(read_u32_le(&hdr[10]) == 66); // Data offset to pixels
  }

  SUBCASE("BITMAPINFOHEADER (Bytes 14-53)") {
    CHECK(read_u32_le(&hdr[14]) == 40);         // biSize
    CHECK(read_s32_le(&hdr[18]) == width);      // biWidth
    CHECK(read_s32_le(&hdr[22]) == -height);    // biHeight: negative for top-down scan
    CHECK(read_u16_le(&hdr[26]) == 1);          // biPlanes
    CHECK(read_u16_le(&hdr[28]) == 16);         // biBitCount (RGB565)
    CHECK(read_u32_le(&hdr[30]) == 3);          // biCompression: 3 = BI_BITFIELDS
    CHECK(read_u32_le(&hdr[34]) == static_cast<uint32_t>(width * height * 2)); // biSizeImage
    CHECK(read_u32_le(&hdr[38]) == 2835);       // biXPelsPerMeter
    CHECK(read_u32_le(&hdr[42]) == 2835);       // biYPelsPerMeter
    CHECK(read_u32_le(&hdr[46]) == 0);          // biClrUsed
    CHECK(read_u32_le(&hdr[50]) == 0);          // biClrImportant
  }

  SUBCASE("RGB565 Bitmasks (Bytes 54-65)") {
    CHECK(read_u32_le(&hdr[54]) == 0x0000F800); // Red mask (5 bits)
    CHECK(read_u32_le(&hdr[58]) == 0x000007E0); // Green mask (6 bits)
    CHECK(read_u32_le(&hdr[62]) == 0x0000001F); // Blue mask (5 bits)
  }
}

TEST_CASE("BMP Header: Arbitrary resolutions via real production code") {
  SUBCASE("Square 320x320") {
    uint8_t hdr[66];
    REQUIRE(build_bmp_header(320, 320, hdr) == true);
    CHECK(read_u32_le(&hdr[2]) == 66 + 320 * 320 * 2);
    CHECK(read_s32_le(&hdr[18]) == 320);
    CHECK(read_s32_le(&hdr[22]) == -320);
  }

  SUBCASE("High resolution 1024x600") {
    uint8_t hdr[66];
    REQUIRE(build_bmp_header(1024, 600, hdr) == true);
    CHECK(read_u32_le(&hdr[2]) == 66 + 1024 * 600 * 2);
    CHECK(read_s32_le(&hdr[18]) == 1024);
    CHECK(read_s32_le(&hdr[22]) == -600);
  }

  SUBCASE("Null buffer guard") {
    CHECK(build_bmp_header(800, 480, nullptr) == false);
  }
}

TEST_CASE("RGB565: Production byte swap helper") {
  // Pure Red in RGB565 little-endian: 0xF800
  uint16_t be_red = 0x00F8;
  CHECK(swap_rgb565(be_red) == 0xF800);

  // Pure Green: 0x07E0 -> big-endian bytes: 0xE0 0x07 -> 0xE007
  uint16_t be_green = 0xE007;
  CHECK(swap_rgb565(be_green) == 0x07E0);

  // Pure Blue: 0x001F -> big-endian bytes: 0x1F 0x00 -> 0x1F00
  uint16_t be_blue = 0x1F00;
  CHECK(swap_rgb565(be_blue) == 0x001F);
}

TEST_CASE("RGB565: Production swap_rgb565_buffer batch buffer swap") {
  // Stream of 3 RGB565 pixels in big-endian:
  // Pixel 0: 0x12 0x34 -> should swap to 0x34 0x12
  // Pixel 1: 0xAB 0xCD -> should swap to 0xCD 0xAB
  // Pixel 2: 0x00 0xFF -> should swap to 0xFF 0x00
  const uint8_t src[6] = {0x12, 0x34, 0xAB, 0xCD, 0x00, 0xFF};
  uint8_t dst[6] = {0};

  swap_rgb565_buffer(src, dst, 6);
  CHECK(dst[0] == 0x34);
  CHECK(dst[1] == 0x12);
  CHECK(dst[2] == 0xCD);
  CHECK(dst[3] == 0xAB);
  CHECK(dst[4] == 0xFF);
  CHECK(dst[5] == 0x00);

  // Null safety guards
  swap_rgb565_buffer(nullptr, dst, 6);
  swap_rgb565_buffer(src, nullptr, 6);

  // Odd length guard (swaps only complete 2-byte pairs)
  uint8_t odd_dst[3] = {0, 0, 0x77};
  swap_rgb565_buffer(src, odd_dst, 3);
  CHECK(odd_dst[0] == 0x34);
  CHECK(odd_dst[1] == 0x12);
  CHECK(odd_dst[2] == 0x77); // Unchanged
}
