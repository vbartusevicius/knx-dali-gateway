# Third-party notices

Components that end up in the distributed firmware image, plus tools used only
to build or test it.

## Linked into the firmware

| Component | Version | Licence | Source |
| --- | --- | --- | --- |
| knx (TP device stack) | 980c047ad7fc5e27bf2fae95e48acde5d5e0b4fd | GPL-3.0 | <https://github.com/thelsing/knx> |
| espressif/dali | 1.1.0 | Apache-2.0 | <https://components.espressif.com/components/espressif/dali> |
| espressif/led_strip | 3.0.3 | Apache-2.0 | <https://components.espressif.com/components/espressif/led_strip> |
| ESP-IDF framework | 5.5.0 (platformio espressif32 6.12.0) | Apache-2.0, see below | <https://github.com/espressif/esp-idf> |

ESP-IDF is published under the Apache-2.0 licence and ships components under
their own licences, including the FreeRTOS kernel (MIT), newlib (BSD-style),
lwIP (BSD-3-Clause), mbed TLS (Apache-2.0), and Espressif driver/HAL code
(Apache-2.0). See the ESP-IDF `LICENSE` file and the per-component licence
headers for the full statement.

`espressif/cmake_utilities` 1.1.1 (Apache-2.0) is declared as a dependency but
provides CMake helper scripts only; it contributes no code to the firmware.

### Espressif binary libraries

The dev_c6 link map shows these closed-source Espressif archives actually
linked into `firmware.elf`:

- `esp_phy` → `libphy.a` (esp32c6): PHY/RF calibration objects, required by
  ESP-IDF for this chip.
- `esp_coex` → `libcoexist.a` (plus the compiled `esp_coex_adapter.c` shim):
  radio coexistence objects pulled in by the PHY component.

The WiFi/MAC/BT archives (`libesp_wifi.a`, `libnet80211.a`, `libpp.a`,
`libbtbb.a`, `libwpa_supplicant.a`) are on the link line but contribute no
objects to the image today. They are distributed under Espressif's binary
licence as part of ESP-IDF.

## Build and test tooling (not distributed in the firmware)

| Component | Version | Licence | Source |
| --- | --- | --- | --- |
| PlatformIO Core | 6.2.0 | Apache-2.0 | <https://platformio.org> |
| Unity test framework | 2.6.1 | MIT | <https://github.com/ThrowTheSwitch/Unity> |
| OpenKNXproducer | 4.3.12 | GPL-3.0 | <https://github.com/OpenKNX/OpenKNXproducer> |
