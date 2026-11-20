#include "am43_uart.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::am43_uart {

static const char *const TAG = "am43_uart";
static constexpr uint8_t POLLS[] = {SETTINGS, LIGHT, BATTERY};
static constexpr uint32_t REPLY_TIMEOUT = 750;
static constexpr uint32_t FRAME_GAP = 150;
static constexpr uint32_t IDLE_POLL_DELAY = 15000;
static constexpr uint32_t ACTIVE_POLL_DELAY = 1000;
static constexpr uint32_t RESET_PULSE = 100;
static constexpr uint32_t RESET_SETTLE = 250;
static constexpr uint32_t NO_REPLY_RESET = 300000;

void AM43::setup() {
  if (reset_pin_ != nullptr)
    reset_pin_->pin_mode(gpio::FLAG_INPUT);
  last_reply_ = next_poll_ = millis();
}

void AM43::dump_config() {
  ESP_LOGCONFIG(TAG, "AM43 motor UART");
  LOG_PIN("  Motor reset: ", reset_pin_);
}

cover::CoverTraits AM43::get_traits() {
  cover::CoverTraits traits;
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  // Keep both direction buttons available with a stale position or after a reversal.
  traits.set_is_assumed_state(true);
  return traits;
}

void AM43::send_(uint8_t command, uint8_t value) {
  const auto packet = request(command, value);
  ESP_LOGD(TAG, "TX command %02X data %02X", command, value);
  write_array(packet.data(), packet.size());
}

void AM43::control(const cover::CoverCall &call) {
  if (resetting_ || settling_) {
    ESP_LOGW(TAG, "Motor reset in progress. Command discarded");
    return;
  }

  auto target = call.get_position();
  if (call.get_stop()) {
    requested_position_ = -1;
    send_(ACTION, STOP);
  } else if (target.has_value() && std::isfinite(*target)) {
    requested_position_ = motor_position(*target);
    command_at_ = millis();
    if (requested_position_ == 0)
      send_(ACTION, OPEN);
    else if (requested_position_ == 100)
      send_(ACTION, CLOSE);
    else
      send_(TARGET, requested_position_);
  } else {
    return;
  }

  // Refresh settings (which contain a position snapshot) promptly after control.
  // Allow any in-flight reply or timeout to finish before issuing another poll.
  refresh_position_ = true;
  if (!waiting_)
    next_poll_ = millis() + 250;
}

bool AM43::handle_(uint8_t command, const uint8_t *data, uint8_t length) {
  ESP_LOGD(TAG, "RX command %02X length %u", command, length);
  switch (command) {
    case SETTINGS:
      if (length < 7)
        return false;
      if (direction_)
        direction_->publish_state(data[0] & 1 ? "Forward" : "Reverse");
      if (operating_mode_)
        operating_mode_->publish_state(data[0] & 2 ? "Inching" : "Continuous");
      if (upper_limit_)
        upper_limit_->publish_state(data[0] & 4 ? "Set" : "Unset");
      if (lower_limit_)
        lower_limit_->publish_state(data[0] & 8 ? "Set" : "Unset");
      if (speed_)
        speed_->publish_state(data[1]);
      break;
    case LIGHT:
      if (length < 2)
        return false;
      if (light_)
        light_->publish_state(data[1]);
      break;
    case BATTERY:
      if (length < 5 || data[4] > 100)
        return false;
      if (battery_)
        battery_->publish_state(data[4]);
      break;
    case POSITION:
      if (length < 2)
        return false;
      break;
    case SEASON:
      return length >= 16;
    case VERIFY:
      if (length < 2)
        return false;
      if (data[0] == 0xA5 || data[1] == 0xCE)
        ESP_LOGW(TAG, "Motor reported a rejection (%02X %02X)", data[0], data[1]);
      else
        ESP_LOGD(TAG, "Motor verification %02X %02X", data[0], data[1]);
      return true;
    default:
      ESP_LOGV(TAG, "Unhandled motor response %02X", command);
      return false;
  }

  if (command == SETTINGS || command == POSITION) {
    const int reported = position_byte(command, data, length);
    if (reported < 0) {
      ESP_LOGW(TAG, "Invalid motor position in %02X", command);
      return command == SETTINGS;  // Settings fields are independent of the position byte.
    }
    if (reported != reported_position_) {
      reported_position_ = reported;
      position = (100 - reported) / 100.0f;
      publish_state(false);
      if (position_sensor_)
        position_sensor_->publish_state(100 - reported);
      ESP_LOGD(TAG, "Motor reports %d%% open", 100 - reported);
    }
    if (reported == requested_position_) {
      ESP_LOGD(TAG, "Motor reports requested position");
      requested_position_ = -1;
    }
  }
  return true;
}

uint32_t AM43::poll_delay_() const {
  return requested_position_ >= 0 ? ACTIVE_POLL_DELAY : IDLE_POLL_DELAY;
}

void AM43::poll_(uint32_t now) {
  if (static_cast<int32_t>(now - next_poll_) < 0)
    return;
  if (waiting_) {
    ESP_LOGW(TAG, "Timed out waiting for motor response %02X", POLLS[poll_index_]);
    waiting_ = false;
    poll_index_ = (poll_index_ + 1) % 3;
    if (poll_index_ == 0 && !refresh_position_) {
      next_poll_ = now + poll_delay_();
      return;
    }
  }

  if (refresh_position_) {
    poll_index_ = 0;
    refresh_position_ = false;
  }
  send_(POLLS[poll_index_], 1);
  waiting_ = true;
  next_poll_ = now + REPLY_TIMEOUT;
}

void AM43::reset_motor_() {
  ESP_LOGW(TAG, "No valid motor response for five minutes");
  if (reset_pin_ == nullptr)
    return;

  ESP_LOGW(TAG, "Pulsing configured motor reset");
  parser_.reset();
  requested_position_ = -1;
  waiting_ = false;
  poll_index_ = 0;
  refresh_position_ = false;
  reset_pin_->digital_write(false);
  reset_pin_->pin_mode(gpio::FLAG_OUTPUT);
  reset_pin_->digital_write(false);
  resetting_ = true;
  reset_at_ = millis();
}

void AM43::loop() {
  const uint32_t now = millis();
  if (resetting_ && now - reset_at_ >= RESET_PULSE) {
    reset_pin_->pin_mode(gpio::FLAG_INPUT);
    resetting_ = false;
    settling_ = true;
    reset_at_ = now;
  }
  if (parser_.partial() && now - last_byte_ >= FRAME_GAP) {
    ESP_LOGD(TAG, "Discarding incomplete motor frame");
    parser_.reset();
  }

  uint8_t byte;
  for (unsigned count = 0; count < 128 && available() && read_byte(&byte); count++) {
    if (resetting_ || settling_)
      continue;
    last_byte_ = now;
    const auto result = parser_.push(byte);
    if (result == Parser::Result::BAD_CHECKSUM) {
      ESP_LOGD(TAG, "Invalid motor frame checksum");
      continue;
    }
    if (result != Parser::Result::FRAME)
      continue;

    const uint8_t command = parser_.command();
    if (!handle_(command, parser_.data(), parser_.length()))
      continue;
    last_reply_ = now;
    if (waiting_ && command == POLLS[poll_index_]) {
      waiting_ = false;
      poll_index_ = (poll_index_ + 1) % 3;
      next_poll_ = now + (poll_index_ != 0 || refresh_position_ ? 100 : poll_delay_());
    }
  }

  if (resetting_)
    return;
  if (settling_) {
    if (now - reset_at_ < RESET_SETTLE)
      return;
    settling_ = false;
    next_poll_ = now;
  }
  if (requested_position_ >= 0 && now - command_at_ >= 60000) {
    if (reported_position_ >= 0)
      ESP_LOGW(TAG, "Requested %d%% open still unconfirmed. Last report %d%% open",
               100 - requested_position_, 100 - reported_position_);
    else
      ESP_LOGW(TAG, "Requested %d%% open still unconfirmed. No position received",
               100 - requested_position_);
    requested_position_ = -1;
  }
  if (now - last_reply_ >= NO_REPLY_RESET) {
    last_reply_ = now;
    reset_motor_();
  }
  if (!resetting_)
    poll_(now);
}

}  // namespace esphome::am43_uart
