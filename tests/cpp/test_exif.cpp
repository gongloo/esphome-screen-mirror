#include <doctest/doctest.h>
#include "screen_mirror/exif_builder.h"
#include <cstdint>
#include <vector>
#include <cstring>

using namespace esphome::screen_mirror;

namespace {

uint16_t read_u16_be(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint16_t read_u16_le(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_u32_le(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

TEST_CASE("EXIF SOI Orientation Header: Structure and byte layout") {
  size_t len_90 = 0;
  const uint8_t *hdr_90 = get_exif_soi_header(90, len_90);
  REQUIRE(hdr_90 != nullptr);
  CHECK(len_90 == 38);

  size_t len_180 = 0;
  const uint8_t *hdr_180 = get_exif_soi_header(180, len_180);
  REQUIRE(hdr_180 != nullptr);
  CHECK(len_180 == 38);

  size_t len_270 = 0;
  const uint8_t *hdr_270 = get_exif_soi_header(270, len_270);
  REQUIRE(hdr_270 != nullptr);
  CHECK(len_270 == 38);

  size_t len_0 = 0;
  CHECK(get_exif_soi_header(0, len_0) == nullptr);
  CHECK(len_0 == 0);

  size_t len_invalid = 0;
  CHECK(get_exif_soi_header(45, len_invalid) == nullptr);
  CHECK(len_invalid == 0);

  SUBCASE("JPEG SOI Marker (Bytes 0-1)") {
    CHECK(hdr_90[0] == 0xFF);
    CHECK(hdr_90[1] == 0xD8);
    CHECK(hdr_180[0] == 0xFF);
    CHECK(hdr_180[1] == 0xD8);
    CHECK(hdr_270[0] == 0xFF);
    CHECK(hdr_270[1] == 0xD8);
  }

  SUBCASE("EXIF APP1 Marker and TIFF Header validation (Bytes 2-37)") {
    // Marker 0xFF, 0xE1
    CHECK(hdr_90[2] == 0xFF);
    CHECK(hdr_90[3] == 0xE1);
    // Length (big endian 34 = 2 length bytes + 32 payload bytes)
    CHECK(read_u16_be(&hdr_90[4]) == 34);
    // Exif\0\0
    CHECK(std::memcmp(&hdr_90[6], "Exif\0\0", 6) == 0);
    // TIFF header: "II", 42, IFD0 offset 8
    CHECK(hdr_90[12] == 'I');
    CHECK(hdr_90[13] == 'I');
    CHECK(read_u16_le(&hdr_90[14]) == 42);
    CHECK(read_u32_le(&hdr_90[16]) == 8);

    // IFD0 count: 1 entry
    CHECK(read_u16_le(&hdr_90[20]) == 1);
    // Tag 0x0112 (Orientation)
    CHECK(read_u16_le(&hdr_90[22]) == 0x0112);
    // Type 3 (SHORT)
    CHECK(read_u16_le(&hdr_90[24]) == 3);
    // Count 1
    CHECK(read_u32_le(&hdr_90[26]) == 1);
  }

  SUBCASE("Orientation Tag Values for 90, 180, 270") {
    // 90 deg rotation -> EXIF Orientation 8 (Rotate 270 CW / 90 CCW)
    CHECK(read_u16_le(&hdr_90[30]) == 8);
    // 180 deg rotation -> EXIF Orientation 3 (Rotate 180)
    CHECK(read_u16_le(&hdr_180[30]) == 3);
    // 270 deg rotation -> EXIF Orientation 6 (Rotate 90 CW)
    CHECK(read_u16_le(&hdr_270[30]) == 6);
  }
}

TEST_CASE("Zero-Copy Stream Construction: Chunk assembly without memmove") {
  // Mock raw JPEG output from encoder in PSRAM:
  // Starts with SOI (FF D8) followed by DQT and payload
  const std::vector<uint8_t> raw_encoder_jpeg = {
    0xFF, 0xD8,             // SOI
    0xFF, 0xDB, 0x00, 0x43, // DQT marker
    0x01, 0x02, 0x03, 0x04, // sample payload
    0xFF, 0xD9              // EOI
  };

  SUBCASE("Rotation 90 stream concatenation: Flash chunk + PSRAM chunk") {
    size_t exif_len = 0;
    const uint8_t *exif_soi = get_exif_soi_header(90, exif_len);
    REQUIRE(exif_soi != nullptr);
    REQUIRE(exif_len == 38);

    // Replicate what the client receives via chunked transfer:
    // Chunk 1: 38-byte static SOI+EXIF directly from Flash
    // Chunk 2: raw_encoder_jpeg + 2 directly from PSRAM (skipping raw SOI)
    std::vector<uint8_t> received_stream;
    received_stream.insert(received_stream.end(), exif_soi, exif_soi + exif_len);
    received_stream.insert(received_stream.end(), raw_encoder_jpeg.data() + 2, raw_encoder_jpeg.data() + raw_encoder_jpeg.size());

    // Total size = 38 (exif_soi) + (raw_size - 2)
    CHECK(received_stream.size() == 38 + (raw_encoder_jpeg.size() - 2));

    // Starts with SOI
    CHECK(received_stream[0] == 0xFF);
    CHECK(received_stream[1] == 0xD8);

    // Followed by EXIF APP1
    CHECK(received_stream[2] == 0xFF);
    CHECK(received_stream[3] == 0xE1);
    CHECK(std::memcmp(&received_stream[6], "Exif\0\0", 6) == 0);

    // Contains Orientation 8
    CHECK(read_u16_le(&received_stream[30]) == 8);

    // Seamlessly transitions into encoder JPEG payload
    CHECK(received_stream[38] == 0xFF);
    CHECK(received_stream[39] == 0xDB);
    CHECK(received_stream.back() == 0xD9);
  }

  SUBCASE("Rotation 0 stream: Directly streams encoder JPEG unmodified") {
    size_t exif_len = 0;
    const uint8_t *exif_soi = get_exif_soi_header(0, exif_len);
    CHECK(exif_soi == nullptr);
    CHECK(exif_len == 0);

    // 0 deg sends raw_encoder_jpeg directly without EXIF chunk
  }
}
