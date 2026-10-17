#include <doctest/doctest.h>
#include <cstdint>
#include <vector>

namespace {

struct WsFrameHeader {
  uint8_t bytes[10];
  size_t length;
};

WsFrameHeader encode_ws_frame_header(size_t payload_len) {
  WsFrameHeader hdr{};
  hdr.bytes[0] = 0x82; // FIN + Binary frame
  if (payload_len < 126) {
    hdr.bytes[1] = static_cast<uint8_t>(payload_len);
    hdr.length = 2;
  } else if (payload_len <= 65535) {
    hdr.bytes[1] = 126;
    hdr.bytes[2] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    hdr.bytes[3] = static_cast<uint8_t>(payload_len & 0xFF);
    hdr.length = 4;
  } else {
    hdr.bytes[1] = 127;
    for (int i = 0; i < 4; i++) hdr.bytes[2 + i] = 0;
    hdr.bytes[6] = static_cast<uint8_t>((payload_len >> 24) & 0xFF);
    hdr.bytes[7] = static_cast<uint8_t>((payload_len >> 16) & 0xFF);
    hdr.bytes[8] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    hdr.bytes[9] = static_cast<uint8_t>(payload_len & 0xFF);
    hdr.length = 10;
  }
  return hdr;
}

std::vector<uint8_t> encode_mirror_header(bool is_keyframe, uint8_t rot_code,
                                          uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h) {
  std::vector<uint8_t> header(10);
  header[0] = is_keyframe ? 0x46 : 0x44; // 'F' or 'D'
  header[1] = static_cast<uint8_t>((rot_code << 2) | (is_keyframe ? 1 : 0));
  header[2] = static_cast<uint8_t>(x >> 8);
  header[3] = static_cast<uint8_t>(x & 0xFF);
  header[4] = static_cast<uint8_t>(y >> 8);
  header[5] = static_cast<uint8_t>(y & 0xFF);
  header[6] = static_cast<uint8_t>(w >> 8);
  header[7] = static_cast<uint8_t>(w & 0xFF);
  header[8] = static_cast<uint8_t>(h >> 8);
  header[9] = static_cast<uint8_t>(h & 0xFF);
  return header;
}

}  // namespace

TEST_CASE("RFC 6455 Frame Header Framing") {
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
    auto hdr = encode_ws_frame_header(70000); // e.g. 70,000 bytes = 0x00011170
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

TEST_CASE("Screen Mirror 10-byte Binary Header Encoding") {
  SUBCASE("Keyframe Full Screen at 0 deg") {
    // 800x480 full screen keyframe
    auto hdr = encode_mirror_header(true, 0, 0, 0, 800, 480);
    REQUIRE(hdr.size() == 10);
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
    // Delta tile at (120, 80) with size (64, 64), rot_code = 1 (90 deg)
    auto hdr = encode_mirror_header(false, 1, 120, 80, 64, 64);
    REQUIRE(hdr.size() == 10);
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
    // rot_code = 3 (270 deg)
    auto hdr = encode_mirror_header(true, 3, 0, 0, 480, 800);
    CHECK(hdr[0] == 0x46);
    CHECK(hdr[1] == 0x0D); // (3 << 2) | 1 = 13 (0x0D)
  }
}
