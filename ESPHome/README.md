# AM43 UART on ESPHome

This external component runs on the ESP-12F wired directly to the AM43 motor controller. The example uses ESPHome **2026.11.0**, the native Home Assistant API, and ESPHome’s built-in web server with its assets stored on the device.

## Install

1. Check out the migration branch. Keep `ESPHome/am43_blinds.yaml` and `components/am43_uart/` in their repository locations so the example can find the local external component.
2. Copy `ESPHome/secrets.example.yaml` to `ESPHome/secrets.yaml`. Fill in the Wi-Fi and web credentials. Generate the API key with `openssl rand -base64 32`. Encrypted ESPHome OTA uses the same key.
3. Keep the existing ESP-12F connections. ESP GPIO1 (TX) goes to the motor controller RX, ESP GPIO3 (RX) to motor TX, with a common ground. The bus is 19200 baud, 8N1. ESPHome uses the `esp12e` board profile for the ESP-12F. Leave `reset_pin` omitted until the reset wiring has been checked.
4. Run the following from the repository root.

   ```sh
   python -m pip install esphome==2026.11.0
   esphome compile ESPHome/am43_blinds.yaml
   esphome run ESPHome/am43_blinds.yaml
   ```

   Use a 3.3 V serial adapter for the first flash, with the motor UART disconnected to avoid bus contention. Follow the repository’s hardware guide for the ESP’s boot straps. Reconnect the motor UART after flashing. Once this firmware is running, use encrypted ESPHome OTA for updates.

5. Add the device through Home Assistant’s **ESPHome** integration with the API key. Browse to `http://am43-blinds.local/` or the device’s LAN IP and sign in with the web credentials. The **Cover** row has Open, Close and Stop. Enter a percentage in **Blind Position**. **Motor-reported position** displays the latest valid report, with 0% fully closed and 100% fully open. Battery is a percentage and Light Level is the controller’s raw reading. Speed is shown in RPM as identified by the upstream implementation. Limit flags show whether each limit is set.
6. For network diagnostics, run `esphome logs ESPHome/am43_blinds.yaml --device am43-blinds.local`. UART logger output is disabled so logs do not share the motor serial line.

The standard ESPHome cover can show its default value when first connected before motor feedback arrives. Use **Motor-reported position**, which starts without a reading, to verify whether the controller has reported a position. Once received, the last good reading remains visible during communication loss. The cover advertises assumed state to keep direction controls available when the last reading is stale or the motor is being reversed. Its position is updated exclusively from validated controller frames. The number field reads back the same motor position on its next one-second update.

### Reset wiring

With power removed, check the wiring and continuity from the candidate ESP GPIO to the motor controller’s reset pad using the board documentation and upstream photos. GPIO5 is the upstream choice. This unit’s reset connection has not been verified. Confirm the line is compatible with a 3.3 V ESP pin and resets when pulled low before setting `reset_pin: GPIO5` (or the verified GPIO). The firmware leaves the pin high impedance, pulses it low for 100 ms after five minutes without a valid known motor response, then releases it and resumes polling. Commands during reset and the brief settling interval are discarded. The upstream author observed that the controller can move toward the upper limit after reset. Keep the area clear when testing recovery.

### Set the physical travel limits

Use the procedure for your AM43 hardware revision. On the unit documented upstream, hold the setting button for about three seconds until the indicator turns red. Press **Up** for the upper limit or **Down** for the lower limit. With the red light flashing, move to the desired limit using the motor buttons. Hold the setting button for about three seconds to save. The indicator flashes blue three times. Repeat for the other limit. [Upstream button diagram](https://github.com/Smoria/AM43-WiFi/issues/3)

## Protocol review

| Area | Arduino and legacy ESPHome | Migration |
| --- | --- | --- |
| Position | Both overwrite the last report on command send. The ESPHome handler also publishes the target immediately. | `0x0D` sends intermediate targets. `0x0A` sends endpoint and stop actions documented by Arduino. The target is tracked independently of position from `0xA7` or `0xA1`. |
| Replies | Both delay 100 ms per byte, append without a buffer bound, and read the declared checksum offset without first checking for a full frame. | A bounded streaming parser keeps partial frames across loops, checks the complete frame XOR, discards incomplete frames after an idle gap, and preserves valid frames that arrive back to back. |
| Polling | Both request `0xA7` but wait for `0xA9` to begin the light/battery polls. The original code decodes `0xA7` as settings and `0xA9` as season data. | Valid `0xA7` advances to light (`0xAA`), then battery (`0xA2`). Each request has a timeout so a missing reply cannot starve the others. Command-triggered refresh and faster polling feed back positions during travel. |
| Command outcome | Sending or accepting a request can be mistaken for physical movement. | Checksummed position replies drive state. Verification frames are logged separately, and a target still unconfirmed after 60 seconds produces a network warning. |
| Recovery | Both hard-code GPIO5 and refresh the watchdog on a header before checking the frame. | Only recognized, well-formed replies refresh the watchdog. Reset GPIO is opt-in and the pulse uses loop timing. Commands are never replayed automatically. |

The comparison establishes the code paths and their consequences. UART captures are still needed to confirm how this motor firmware orders `0xA7`, optional `0xA9`, `0xA1`, and verification replies. If light feedback is missing, check the `0xAA` request and reply in the logs and verify the sensor wiring. The [original light report](https://github.com/Smoria/AM43-WiFi/issues/2) was traced to wiring.

## Validation

The host-side protocol test covers the command bytes and XOR, percentage conversion, partial and back-to-back frames, bad checksums, bad positions, noise, timeout recovery and 255-byte payloads.

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/protocol_test.cpp -o /tmp/am43-protocol-test
/tmp/am43-protocol-test
```

| Check | Result |
| --- | --- |
| Host protocol test, GCC with AddressSanitizer and UndefinedBehaviorSanitizer | Passed |
| ESPHome 2026.11.0, `esp12e`, example with reset disabled | Compiled. Flash 476,812 bytes (45.7%), RAM 31,476 bytes (38.4%). |
| ESPHome 2026.11.0, `esp12e`, example variant with `reset_pin: GPIO5` | Compiled. Flash 476,848 bytes (45.7%), RAM 31,488 bytes (38.4%). |

Physical travel, motor serial replies, the reset line, and end-to-end operation through Home Assistant and the webpage require bench testing.

### Hardware test

1. Set and check both physical travel limits. Start with a clear travel path and the reset option disabled. Read the network logs and confirm that settings, light and battery requests continue even if one reply is missing. Verify the two limit flags and compare the reported speed, mode and direction against known settings if available.
2. Open from Home Assistant and close from the webpage. Watch the blind and check that the displayed percentage follows the reported movement. Verify the physical endpoints and that both clients show the same final position. Record any motor-reported endpoint that differs from 0% or 100%.
3. From each interface, start movement and press Stop before the endpoint. Check that movement stops and both clients show the stopped position from the next valid report.
4. Use each interface to request 25%, 50% and 75% open from both directions. Confirm the target is displayed as actual only when the motor reports it. Also move using the physical buttons and verify the reported position changes without sending a network command.
5. Power down before isolating or reconnecting wires. With the reset option disabled and the motor unavailable, request a move and check for poll timeouts and an unconfirmed-target warning. Verify the displayed position retains its last valid reading within the same ESP boot. Restore communication and check that polling and feedback resume. Verify the old command was not replayed.
6. After confirming reset wiring, repeat with reset enabled. With the test area clear, simulate loss of motor replies on a bench harness and use a logic probe to verify a low pulse at about five minutes and release to high impedance. Restore the UART connection and confirm fresh telemetry and a new command work. Check whether the controller moves during reset recovery.
