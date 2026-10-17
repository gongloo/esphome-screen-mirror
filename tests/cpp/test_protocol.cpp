#include <doctest/doctest.h>
#include "screen_mirror/protocol.h"
#include <cstdint>
#include <vector>

using namespace esphome::screen_mirror;

TEST_CASE("RFC 6455 Frame Header Framing via production encode_ws_frame_header") {
  SUBCASE("Small payload (< 126 bytes)") {
    auto hdr = encode_ws_frame_header(100);
    CHECK(hdr.length == 2);
    CHECK(hdr.bytes[0] == 0x82);
    CHECK(hdr.bytes[1] == 100);
  }

  SUBCASE("Medium payload (126 to 65535 bytes)") {
    auto hdr = encode_ws_frame_header(1024);
    CHECK(hdr.length == 4);
    CHECK(hdr.bytes[0] == 0x82);
    CHECK(hdr.bytes[1] == 126);
    CHECK(hdr.bytes[2] == 0x04);
    CHECK(hdr.bytes[3] == 0x00);

    auto hdr_boundary = encode_ws_frame_header(65535);
    CHECK(hdr_boundary.length == 4);
    CHECK(hdr_boundary.bytes[1] == 126);
    CHECK(hdr_boundary.bytes[2] == 0xFF);
    CHECK(hdr_boundary.bytes[3] == 0xFF);
  }

  SUBCASE("Large payload (> 65535 bytes)") {
    auto hdr = encode_ws_frame_header(70000); // 70,000 bytes = 0x00011170
    CHECK(hdr.length == 10);
    CHECK(hdr.bytes[0] == 0x82);
    CHECK(hdr.bytes[1] == 127);
    CHECK(hdr.bytes[2] == 0);
    CHECK(hdr.bytes[3] == 0);
    CHECK(hdr.bytes[4] == 0);
    CHECK(hdr.bytes[5] == 0);
    CHECK(hdr.bytes[6] == 0x00);
    CHECK(hdr.bytes[7] == 0x01);
    CHECK(hdr.bytes[8] == 0x11);
    CHECK(hdr.bytes[9] == 0x70);
  }
}

TEST_CASE("Screen Mirror 10-byte Binary Header Encoding via production encode_mirror_header") {
  SUBCASE("Keyframe Full Screen at 0 deg") {
    uint8_t hdr[10];
    encode_mirror_header(true, 0, 0, 0, 800, 480, hdr);
    CHECK(hdr[0] == 0x46); // 'F'
    CHECK(hdr[1] == 0x01); // (0 << 2) | 1 = 1
    CHECK(hdr[2] == 0x00); // X = 0
    CHECK(hdr[3] == 0x00);
    CHECK(hdr[4] == 0x00); // Y = 0
    CHECK(hdr[5] == 0x00);
    CHECK(hdr[6] == 0x03); // W = 800 (0x0320)
    CHECK(hdr[7] == 0x20);
    CHECK(hdr[8] == 0x01); // H = 480 (0x01E0)
    CHECK(hdr[9] == 0xE0);
  }

  SUBCASE("Delta Tile at 90 deg rotation") {
    uint8_t hdr[10];
    encode_mirror_header(false, 1, 120, 80, 64, 64, hdr);
    CHECK(hdr[0] == 0x44); // 'D'
    CHECK(hdr[1] == 0x04); // (1 << 2) | 0 = 4
    CHECK(hdr[2] == 0x00); // X = 120 (0x0078)
    CHECK(hdr[3] == 0x78);
    CHECK(hdr[4] == 0x00); // Y = 80 (0x0050)
    CHECK(hdr[5] == 0x50);
    CHECK(hdr[6] == 0x00); // W = 64 (0x0040)
    CHECK(hdr[7] == 0x40);
    CHECK(hdr[8] == 0x00); // H = 64 (0x0040)
    CHECK(hdr[9] == 0x40);
  }

  SUBCASE("Keyframe at 270 deg rotation") {
    uint8_t hdr[10];
    encode_mirror_header(true, 3, 0, 0, 480, 800, hdr);
    CHECK(hdr[0] == 0x46);
    CHECK(hdr[1] == 0x0D); // (3 << 2) | 1 = 13 (0x0D)
  }
}
