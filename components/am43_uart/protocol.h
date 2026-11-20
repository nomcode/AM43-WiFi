#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::am43_uart {

constexpr uint8_t HEADER = 0x9A;
constexpr uint8_t ACTION = 0x0A;
constexpr uint8_t TARGET = 0x0D;
constexpr uint8_t OPEN = 0xDD;
constexpr uint8_t CLOSE = 0xEE;
constexpr uint8_t STOP = 0xCC;
constexpr uint8_t SETTINGS = 0xA7;
constexpr uint8_t LIGHT = 0xAA;
constexpr uint8_t BATTERY = 0xA2;
constexpr uint8_t POSITION = 0xA1;
constexpr uint8_t SEASON = 0xA9;
constexpr uint8_t VERIFY = 0x00;

inline std::array<uint8_t, 9> request(uint8_t command, uint8_t data) {
  return {0x00, 0xFF, 0x00, 0x00, HEADER, command, 1, data,
          static_cast<uint8_t>(HEADER ^ command ^ 1 ^ data)};
}

// One length byte allows 255 payload bytes. Timing out incomplete frames also
// allows recovery from a corrupt length without treating payload 0x9A as a header.
class Parser {
 public:
  enum class Result { INCOMPLETE, FRAME, BAD_CHECKSUM };

  void reset() { used_ = 0; }
  bool partial() const { return used_ != 0; }
  uint8_t command() const { return bytes_[1]; }
  uint8_t length() const { return bytes_[2]; }
  const uint8_t *data() const { return bytes_.data() + 3; }

  Result push(uint8_t byte) {
    if (used_ == 0 && byte != HEADER)
      return Result::INCOMPLETE;
    bytes_[used_++] = byte;
    if (used_ < 4 || used_ != size_t(length()) + 4)
      return Result::INCOMPLETE;

    uint8_t checksum = 0;
    for (size_t i = 0; i < used_; i++)
      checksum ^= bytes_[i];
    reset();
    if (checksum == 0)
      return Result::FRAME;
    if (byte == HEADER)
      bytes_[used_++] = byte;
    return Result::BAD_CHECKSUM;
  }

 private:
  std::array<uint8_t, 259> bytes_{};
  size_t used_{0};
};

inline int position_byte(uint8_t command, const uint8_t *data, uint8_t length) {
  if (command == SETTINGS && length >= 7 && data[2] <= 100)
    return data[2];
  if (command == POSITION && length >= 2 && data[1] <= 100)
    return data[1];
  return -1;
}

inline uint8_t motor_position(float openness) {
  return 100 - static_cast<uint8_t>(openness * 100 + 0.5f);
}

}  // namespace esphome::am43_uart
