#include "../components/am43_uart/protocol.h"

#include <cassert>
#include <initializer_list>
#include <vector>

using namespace esphome::am43_uart;

static std::vector<uint8_t> reply(uint8_t cmd, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> frame{0x9A, cmd, static_cast<uint8_t>(payload.size())};
  frame.insert(frame.end(), payload.begin(), payload.end());
  uint8_t checksum = 0;
  for (const auto byte : frame)
    checksum ^= byte;
  frame.push_back(checksum);
  return frame;
}

static void expect_frame(Parser &parser, const std::vector<uint8_t> &bytes) {
  for (size_t i = 0; i + 1 < bytes.size(); i++)
    assert(parser.push(bytes[i]) == Parser::Result::INCOMPLETE);
  assert(parser.push(bytes.back()) == Parser::Result::FRAME);
  assert(!parser.partial());
}

int main() {
  const std::array<uint8_t, 9> open{0, 0xFF, 0, 0, 0x9A, 0x0A, 1, 0xDD, 0x4C};
  const std::array<uint8_t, 9> close{0, 0xFF, 0, 0, 0x9A, 0x0A, 1, 0xEE, 0x7F};
  const std::array<uint8_t, 9> stop{0, 0xFF, 0, 0, 0x9A, 0x0A, 1, 0xCC, 0x5D};
  const std::array<uint8_t, 9> target{0, 0xFF, 0, 0, 0x9A, 0x0D, 1, 65, 0xD7};
  const std::array<uint8_t, 9> settings{0, 0xFF, 0, 0, 0x9A, 0xA7, 1, 1, 0x3D};
  assert(request(ACTION, OPEN) == open);
  assert(request(ACTION, CLOSE) == close);
  assert(request(ACTION, STOP) == stop);
  assert(request(TARGET, motor_position(0.35f)) == target);
  assert(request(SETTINGS, 1) == settings);
  assert(motor_position(0.0f) == 100);
  assert(motor_position(1.0f) == 0);

  Parser parser;
  for (uint8_t noise : {0, 0xFF, 0, 0})
    assert(parser.push(noise) == Parser::Result::INCOMPLETE);
  assert(!parser.partial());
  auto status = reply(SETTINGS, {0x1F, 30, 65, 0, 0x9A, 0, 0x30});
  expect_frame(parser, status);
  assert(parser.command() == SETTINGS && parser.length() == 7);
  assert(position_byte(parser.command(), parser.data(), parser.length()) == 65);
  expect_frame(parser, reply(POSITION, {0, 27}));
  assert(position_byte(parser.command(), parser.data(), parser.length()) == 27);
  expect_frame(parser, reply(POSITION, {0, 101}));
  assert(position_byte(parser.command(), parser.data(), parser.length()) == -1);
  expect_frame(parser, reply(POSITION, {0}));
  assert(position_byte(parser.command(), parser.data(), parser.length()) == -1);
  expect_frame(parser, reply(SETTINGS, {}));
  assert(position_byte(parser.command(), parser.data(), parser.length()) == -1);

  status.back() ^= 1;
  for (size_t i = 0; i + 1 < status.size(); i++)
    parser.push(status[i]);
  assert(parser.push(status.back()) == Parser::Result::BAD_CHECKSUM);
  expect_frame(parser, reply(BATTERY, {0, 0, 0, 0, 84}));

  parser.push(HEADER);
  parser.push(POSITION);
  parser.push(255);
  assert(parser.partial());
  parser.reset();  // Models a receive timeout.
  expect_frame(parser, reply(LIGHT, {0, 3}));
  expect_frame(parser, reply(0xFF, std::vector<uint8_t>(255, HEADER)));
}
