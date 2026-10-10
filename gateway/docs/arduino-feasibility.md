# Moving the firmware to the Arduino framework: feasibility

Status: analysis only, nothing changed.

## 1. How much of the code depends on ESP-IDF today

| Area | ESP-IDF specific? | Arduino equivalent |
| --- | --- | --- |
| Domain and protocol code (lighting, DALI frames/bus logic, KNX mapping, commissioning, inputs, config) | No — portable C++17, tested natively and in the lab | Unchanged |
| DALI physical layer (RMT TX/RX, bus task) | Yes (ESP-IDF RMT driver, FreeRTOS) | Arduino-ESP32 3.x is built on ESP-IDF 5.x; the same RMT driver headers are available, so the code would compile with small include changes. Arduino's own RMT wrapper is less suitable (no continuous RX with our timing needs). |
| Tasks and queues | FreeRTOS | Same FreeRTOS underneath Arduino-ESP32; no change |
| Persistence (NVS) | nvs.h | Preferences library (wraps NVS) or keep nvs.h |
| USB console | esp_console | Serial + a small command parser, or keep esp_console |
| WS2812 prog LED | espressif/led_strip | Adafruit NeoPixel / FastLED, or keep led_strip |
| KNX stack platform | thelsing Esp32IdfPlatform | thelsing Esp32Platform (Arduino); OpenKNX/knx fork also becomes usable |
| Build configuration | sdkconfig.defaults, CMake files | Arduino ships precompiled ESP-IDF libraries. Changing sdkconfig options (watchdog timeout, tick rate 1000 Hz, console) needs pioarduino's `custom_sdkconfig`, which rebuilds the libraries. |

Only src/platform (today firmware/ and espidf/) would change.

## 2. Toolchain reality

- The ESP32-C6 needs Arduino-ESP32 **3.x**. The official PlatformIO `espressif32` platform only ships Arduino 2.x, which has no C6 support. Arduino 3.x on PlatformIO means the community platform **pioarduino**: a different platform package with its own pinning and release cadence.
- Our FreeRTOS tick (1000 Hz) and 30 s task watchdog are sdkconfig settings. With Arduino they need `custom_sdkconfig`, which turns builds into ESP-IDF library rebuilds (slower, more moving parts).
- Arduino as an ESP-IDF component (`framework = arduino, espidf`) is a middle ground. It keeps our ESP-IDF build and adds Arduino libraries, at the cost of more CMake/sdkconfig plumbing.

## 3. Licensing: does Arduino change anything?

| Component | Licence | Effect |
| --- | --- | --- |
| thelsing/knx (or OpenKNX/knx) | GPL-3.0 | Unchanged: the firmware is GPL either way |
| Arduino-ESP32 core | LGPL-2.1 | Adds a (mild) LGPL obligation for distributed binaries: users must be able to relink against a modified core (object files or source) |
| ESP-IDF | Apache-2.0 | Unchanged |
| Espressif radio/PHY binary libraries | Proprietary, redistributable | Unchanged: Arduino links the same blobs |

**Arduino does not remove the GPL/binary-blob situation.** It slightly adds to it. Per the user's decision this is handled with a note, like comparable projects.

## 4. What Arduino would give us

- Familiar ecosystem for contributors and for the user: WiFiManager-style provisioning, ESPAsyncWebServer, PubSubClient/AsyncMqttClient, ArduinoJson, Home Assistant MQTT discovery helpers.
- The OpenKNX ecosystem (OpenKNX/knx fork, OpenKNX common modules, tooling) works with ESP32 only through Arduino.

## 5. What we would lose or pay

- A second platform package (pioarduino) with its own update risk, instead of the official ESP-IDF platform.
- sdkconfig control becomes indirect (library rebuilds).
- The precise RMT timing code stays ESP-IDF code anyway, so the DALI layer gets no simpler.
- ESP-IDF already provides what the final networking part needs: esp_http_server, esp-mqtt, Wi-Fi SoftAP provisioning (wifi_provisioning), mDNS, OTA.

## 6. Recommendation

Stay on ESP-IDF for the firmware. The portable core already isolates the platform, so a later move stays cheap: only src/platform changes.

Re-evaluate if the networking part ends up needing a specific Arduino-only library. The preferred route then is "Arduino as ESP-IDF component" for that library, not a full framework switch.
