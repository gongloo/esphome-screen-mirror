#include <doctest/doctest.h>
#include "screen_mirror/protocol.h"

using namespace esphome::screen_mirror;

TEST_CASE("WebSocket Text Command Parser: Production parse_ws_packet Keyframe Requests") {
  SUBCASE("Uppercase K") {
    auto cmd = parse_ws_packet("K", 1);
    CHECK(cmd.type == WsCmdType::KEYFRAME);
  }

  SUBCASE("Lowercase k") {
    auto cmd = parse_ws_packet("k", 1);
    CHECK(cmd.type == WsCmdType::KEYFRAME);
  }
}

TEST_CASE("WebSocket Text Command Parser: Production parse_ws_packet Touch Commands") {
  SUBCASE("Tap command") {
    auto cmd = parse_ws_packet("T 320 240", 9);
    CHECK(cmd.type == WsCmdType::TAP);
    CHECK(cmd.x == 320);
    CHECK(cmd.y == 240);
    CHECK(cmd.duration == 80);
  }

  SUBCASE("Down command") {
    auto cmd = parse_ws_packet("D 100 200", 9);
    CHECK(cmd.type == WsCmdType::DOWN);
    CHECK(cmd.x == 100);
    CHECK(cmd.y == 200);
  }

  SUBCASE("Move command") {
    auto cmd = parse_ws_packet("M 105 205", 9);
    CHECK(cmd.type == WsCmdType::MOVE);
    CHECK(cmd.x == 105);
    CHECK(cmd.y == 205);
  }

  SUBCASE("Up with explicit coordinates") {
    auto cmd = parse_ws_packet("U 105 205", 9);
    CHECK(cmd.type == WsCmdType::UP);
    CHECK(cmd.x == 105);
    CHECK(cmd.y == 205);
  }

  SUBCASE("Bare Up command without coordinates") {
    auto cmd = parse_ws_packet("U", 1);
    CHECK(cmd.type == WsCmdType::UP);
    CHECK(cmd.x == 0);
    CHECK(cmd.y == 0);
  }

  SUBCASE("Case insensitivity (lowercase t, d, m, u)") {
    auto t = parse_ws_packet("t 10 20", 7);
    CHECK(t.type == WsCmdType::TAP);
    auto d = parse_ws_packet("d 30 40", 7);
    CHECK(d.type == WsCmdType::DOWN);
    auto m = parse_ws_packet("m 50 60", 7);
    CHECK(m.type == WsCmdType::MOVE);
    auto u = parse_ws_packet("u", 1);
    CHECK(u.type == WsCmdType::UP);
  }
}

TEST_CASE("WebSocket Text Command Parser: Malformed and Edge Case Inputs") {
  SUBCASE("Empty buffer") {
    auto cmd = parse_ws_packet("", 0);
    CHECK(cmd.type == WsCmdType::NONE);
  }

  SUBCASE("Null pointer") {
    auto cmd = parse_ws_packet(nullptr, 0);
    CHECK(cmd.type == WsCmdType::NONE);
  }

  SUBCASE("Unknown command identifier") {
    auto cmd = parse_ws_packet("X 100 200", 9);
    CHECK(cmd.type == WsCmdType::NONE);
  }

  SUBCASE("Truncated coordinates") {
    auto cmd1 = parse_ws_packet("T 100", 5);
    CHECK(cmd1.type == WsCmdType::NONE);

    auto cmd2 = parse_ws_packet("D", 1);
    CHECK(cmd2.type == WsCmdType::NONE);
  }

  SUBCASE("Non-numeric coordinates") {
    auto cmd = parse_ws_packet("T abc def", 9);
    CHECK(cmd.type == WsCmdType::NONE);
  }
}
