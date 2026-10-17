#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace esphome {
namespace screen_mirror {

enum class WsCmdType {
  NONE,
  KEYFRAME,
  TAP,
  DOWN,
  MOVE,
  UP
};

struct ParsedWsCmd {
  WsCmdType type{WsCmdType::NONE};
  int x{0};
  int y{0};
  uint16_t duration{80};
};

struct WsFrameHeader {
  uint8_t bytes[10];
  size_t length;
};

inline WsFrameHeader encode_ws_frame_header(size_t payload_len) {
  WsFrameHeader hdr{};
  hdr.bytes[0] = 0x82; // FIN bit (0x80) | Binary opcode (0x02)
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

inline void encode_mirror_header(bool is_keyframe, uint8_t rot_code,
                                 uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h,
                                 uint8_t *header_out) {
  if (header_out == nullptr) return;
  header_out[0] = is_keyframe ? 0x46 : 0x44; // 'F' : 'D'
  header_out[1] = static_cast<uint8_t>((rot_code << 2) | (is_keyframe ? 1 : 0));
  header_out[2] = static_cast<uint8_t>(x >> 8);
  header_out[3] = static_cast<uint8_t>(x & 0xFF);
  header_out[4] = static_cast<uint8_t>(y >> 8);
  header_out[5] = static_cast<uint8_t>(y & 0xFF);
  header_out[6] = static_cast<uint8_t>(w >> 8);
  header_out[7] = static_cast<uint8_t>(w & 0xFF);
  header_out[8] = static_cast<uint8_t>(h >> 8);
  header_out[9] = static_cast<uint8_t>(h & 0xFF);
}

inline ParsedWsCmd parse_ws_packet(const char *buf, size_t len) {
  ParsedWsCmd cmd{};
  if (buf == nullptr || len == 0) {
    return cmd;
  }

  char action_char = buf[0];
  int x = 0, y = 0;

  if (action_char == 'K' || action_char == 'k') {
    cmd.type = WsCmdType::KEYFRAME;
    return cmd;
  }
  if (action_char == 'T' || action_char == 't') {
    if (std::sscanf(buf + 1, "%d %d", &x, &y) == 2) {
      cmd.type = WsCmdType::TAP;
      cmd.x = x;
      cmd.y = y;
      cmd.duration = 80;
      return cmd;
    }
  } else if (action_char == 'D' || action_char == 'd') {
    if (std::sscanf(buf + 1, "%d %d", &x, &y) == 2) {
      cmd.type = WsCmdType::DOWN;
      cmd.x = x;
      cmd.y = y;
      return cmd;
    }
  } else if (action_char == 'M' || action_char == 'm') {
    if (std::sscanf(buf + 1, "%d %d", &x, &y) == 2) {
      cmd.type = WsCmdType::MOVE;
      cmd.x = x;
      cmd.y = y;
      return cmd;
    }
  } else if (action_char == 'U' || action_char == 'u') {
    if (std::sscanf(buf + 1, "%d %d", &x, &y) == 2) {
      cmd.type = WsCmdType::UP;
      cmd.x = x;
      cmd.y = y;
      return cmd;
    } else {
      cmd.type = WsCmdType::UP;
      cmd.x = 0;
      cmd.y = 0;
      return cmd;
    }
  }

  return cmd;
}

inline void clamp_coordinates(int &x, int &y, int hw_w, int hw_h) {
  x = std::max(0, std::min(hw_w - 1, x));
  y = std::max(0, std::min(hw_h - 1, y));
}

}  // namespace screen_mirror
}  // namespace esphome
