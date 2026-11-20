#pragma once

#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "protocol.h"

namespace esphome::am43_uart {

class AM43 : public cover::Cover, public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  cover::CoverTraits get_traits() override;

  void set_reported_position(sensor::Sensor *value) { position_sensor_ = value; }
  void set_battery(sensor::Sensor *value) { battery_ = value; }
  void set_light(sensor::Sensor *value) { light_ = value; }
  void set_speed(sensor::Sensor *value) { speed_ = value; }
  void set_direction(text_sensor::TextSensor *value) { direction_ = value; }
  void set_operating_mode(text_sensor::TextSensor *value) { operating_mode_ = value; }
  void set_upper_limit(text_sensor::TextSensor *value) { upper_limit_ = value; }
  void set_lower_limit(text_sensor::TextSensor *value) { lower_limit_ = value; }
  void set_reset_pin(InternalGPIOPin *value) { reset_pin_ = value; }

 protected:
  void control(const cover::CoverCall &call) override;
  void send_(uint8_t command, uint8_t value);
  bool handle_(uint8_t command, const uint8_t *data, uint8_t length);
  void poll_(uint32_t now);
  uint32_t poll_delay_() const;
  void reset_motor_();

  sensor::Sensor *position_sensor_{nullptr};
  sensor::Sensor *battery_{nullptr};
  sensor::Sensor *light_{nullptr};
  sensor::Sensor *speed_{nullptr};
  text_sensor::TextSensor *direction_{nullptr};
  text_sensor::TextSensor *operating_mode_{nullptr};
  text_sensor::TextSensor *upper_limit_{nullptr};
  text_sensor::TextSensor *lower_limit_{nullptr};
  InternalGPIOPin *reset_pin_{nullptr};

  Parser parser_;
  uint32_t last_byte_{0};
  uint32_t last_reply_{0};
  uint32_t next_poll_{0};
  uint32_t command_at_{0};
  uint32_t reset_at_{0};
  uint8_t poll_index_{0};
  bool waiting_{false};
  bool refresh_position_{false};
  bool resetting_{false};
  bool settling_{false};
  int requested_position_{-1};
  int reported_position_{-1};
};

}  // namespace esphome::am43_uart
