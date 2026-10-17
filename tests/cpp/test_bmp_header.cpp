#include <doctest/doctest.h>
#include <cstdint>
#include <vector>
#include <cstring>

namespace {

std::vector<uint8_t> generate_bmp_header(int32_t w, int32_t h) {
  uint32_t file_size = 66 + (uint32_t)w * h * 2;
  int32_t neg_h = -h;
  uint32_t img_size = (uint32_t)w * h * 2;
  uint8_t header[66] = {
      'B', 'M',
      static_cast<uint8_t>(file_size), static_cast<uint8_t>(file_size >> 8),
      static_cast<uint8_t>(file_size >> 16), static_cast<uint8_t>(file_size >> 24),
      0, 0, 0, 0,
      66, 0, 0, 0,
      40, 0, 0, 0,
      static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8),
      static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 24),
      static_cast<uint8_t>(neg_h), static_cast<uint8_t>(neg_h >> 8),
      static_cast<uint8_t>(neg_h >> 16), static_cast<uint8_t>(neg_h >> 24),
      1, 0,
      16, 0,
      3, 0, 0, 0,  // BI_BITFIELDS
      static_cast<uint8_t>(img_size), static_cast<uint8_t>(img_size >> 8),
      static_cast<uint8_t>(img_size >> 16), static_cast<uint8_t>(img_size >> 24),
      0x13, 0x0B, 0, 0,  // 2835 ppm (~72 DPI)
      0x13, 0x0B, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0x00, 0xF8, 0x00, 0x00,  // Red mask:   0x0000F800
      0xE0, 0x07, 0x00, 0x00,  // Green mask: 0x000007E0
      0x1F, 0x00, 0x00, 0x00   // Blue mask:  0x0000001F
  };
  return std::vector<uint8_t>(header, header + 66);
}

inline uint16_t swap_rgb565(uint16_t v) {
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

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

TEST_CASE("BMP Header: 66-byte BI_BITFIELDS structure validation") {
  const int32_t width = 800;
  const int32_t height = 480;
  auto hdr = generate_bmp_header(width, height);

  REQUIRE(hdr.size() == 66);

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

TEST_CASE("BMP Header: Arbitrary resolutions") {
  SUBCASE("Square 320x320") {
    auto hdr = generate_bmp_header(320, 320);
    CHECK(hdr.size() == 66);
    CHECK(read_u32_le(&hdr[2]) == 66 + 320 * 320 * 2);
    CHECK(read_s32_le(&hdr[18]) == 320);
    CHECK(read_s32_le(&hdr[22]) == -320);
  }

  SUBCASE("High resolution 1024x600") {
    auto hdr = generate_bmp_header(1024, 600);
    CHECK(hdr.size() == 66);
    CHECK(read_u32_le(&hdr[2]) == 66 + 1024 * 600 * 2);
    CHECK(read_s32_le(&hdr[18]) == 1024);
    CHECK(read_s32_le(&hdr[22]) == -600);
  }
}

TEST_CASE("RGB565: Big-endian byte swap helper") {
  // Pure Red in RGB565 little-endian: 0xF800
  // When stored in big-endian hardware memory: 0x00 0xF8 -> uint16_t is 0x00F8.
  uint16_t be_red = 0x00F8;
  CHECK(swap_rgb565(be_red) == 0xF800);

  // Pure Green: 0x07E0 -> big-endian bytes: 0xE0 0x07 -> 0xE007
  uint16_t be_green = 0xE007;
  CHECK(swap_rgb565(be_green) == 0x07E0);

  // Pure Blue: 0x001F -> big-endian bytes: 0x1F 0x00 -> 0x1F00
  uint16_t be_blue = 0x1F00;
  CHECK(swap_rgb565(be_blue) == 0x001F);
}
