# Licensing

Everything in this repository is open source. Each folder is licensed as
follows; the SPDX identifier links to the full licence text.

| Folder        | Licence                                                                    | Contents                                         |
| ------------- | -------------------------------------------------------------------------- | ------------------------------------------------ |
| `gateway/`    | [GPL-3.0-or-later](https://spdx.org/licenses/GPL-3.0-or-later.html)        | Firmware; links the GPL'd thelsing/knx stack      |
| `knxprod/`    | [MIT](https://spdx.org/licenses/MIT.html)                                  | ETS product definitions and generator tooling     |
| `simulation/` | [MIT](https://spdx.org/licenses/MIT.html)                                  | Docker integration lab and simulated devices      |
| `docker/`     | [MIT](https://spdx.org/licenses/MIT.html)                                  | Container definitions for every tool              |
| `hardware/`   | [CERN-OHL-P-2.0](https://spdx.org/licenses/CERN-OHL-P-2.0.html)            | Circuits, PCBs, releases and hardware tooling     |
| `mechanical/` | [CERN-OHL-P-2.0](https://spdx.org/licenses/CERN-OHL-P-2.0.html)            | Parametric enclosure and fit checks               |
| documentation | [CC-BY-4.0](https://spdx.org/licenses/CC-BY-4.0.html)                      | `README.md`, `AGENTS.md`, every `docs/` folder    |

Copyright the KNX-DALI gateway contributors.

## Third-party components

Linked into the firmware image:

| Component | Version | Licence | Source |
| --- | --- | --- | --- |
| knx (TP device stack) | 980c047ad7fc5e27bf2fae95e48acde5d5e0b4fd | GPL-3.0 | <https://github.com/thelsing/knx> |
| espressif/led_strip | 3.0.3 | Apache-2.0 | <https://components.espressif.com/components/espressif/led_strip> |
| ESP-IDF | 5.5.0 (PlatformIO espressif32 6.12.0) | Apache-2.0 and per-component licences | <https://github.com/espressif/esp-idf> |

ESP-IDF ships components under their own licences, among them the FreeRTOS
kernel (MIT), newlib (BSD-style), lwIP (BSD-3-Clause), mbed TLS (Apache-2.0)
and Espressif driver/HAL code (Apache-2.0); see the ESP-IDF `LICENSE` file
and the per-component headers. `espressif/cmake_utilities` 1.1.1 (Apache-2.0)
only provides CMake helpers. The firmware also links Espressif's binary-only
radio libraries (libphy, libcoexist, Wi-Fi), as comparable open-source ESP32
KNX projects do.

The boards use KiCad's stock symbol, footprint and 3D libraries (CC-BY-SA-4.0
with an exception for designs made with them). Build, test and viewer tools
are not distributed with anything; the container images download them at
build time under their own licences (PlatformIO, Unity, OpenKNXproducer,
KiCad, SKiDL, Freerouting, CadQuery, VTK, LLVM, ruff, three.js, KiCanvas).
