import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import cover, sensor, text_sensor, uart
from esphome.const import (
    CONF_INVERTED,
    DEVICE_CLASS_BATTERY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_PERCENT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]

AM43 = cg.esphome_ns.namespace("am43_uart").class_(
    "AM43", cover.Cover, cg.Component, uart.UARTDevice
)

SENSORS = {
    "reported_position": sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "battery": sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        device_class=DEVICE_CLASS_BATTERY,
        accuracy_decimals=0,
    ),
    "light": sensor.sensor_schema(accuracy_decimals=0),
    "speed": sensor.sensor_schema(
        unit_of_measurement="rpm",
        accuracy_decimals=0,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}
TEXT_SENSORS = ("direction", "operating_mode", "upper_limit", "lower_limit")
RESET_PIN = "reset_pin"

def non_inverted_reset(config):
    if config[CONF_INVERTED]:
        raise cv.Invalid("AM43 reset pin is active-low and must be configured without inversion")
    return config


CONFIG_SCHEMA = (
    cover.cover_schema(AM43)
    .extend({cv.Optional(key): schema for key, schema in SENSORS.items()})
    .extend({
        cv.Optional(key): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        )
        for key in TEXT_SENSORS
    })
    .extend({
        cv.Optional(RESET_PIN): cv.All(
            pins.internal_gpio_output_pin_schema, non_inverted_reset
        ),
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "am43_uart", baud_rate=19200, require_tx=True, require_rx=True,
    data_bits=8, parity="NONE", stop_bits=1,
)


async def to_code(config):
    var = await cover.new_cover(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    for key in SENSORS:
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await sensor.new_sensor(config[key])))
    for key in TEXT_SENSORS:
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await text_sensor.new_text_sensor(config[key])))
    if RESET_PIN in config:
        cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[RESET_PIN])))
