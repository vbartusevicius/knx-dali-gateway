# Hardware variants (V1, V2, V3)

All three variants share one set of circuit blocks (`hardware/circuits/blocks/`),
the same **RECOM `REM3-2415S/A/CTRL`** isolated DALI supply with firmware shut-off,
and the exact same **Seeed XIAO ESP32-C6** pin map, LED chain and ADC button ladder
(`gateway/src/firmware/Board.hpp`).

| Variant | Width | Boards | Main-to-UI Link | DALI Terminal | KNX / AUX Terminals | OLED Display | Case Closure |
|---|---|---|---|---|---|---|---|
| **V1** (`v1`) | 3 DIN (`54 mm`) | 2 (`main`, `ui`) | 10-pin JST-SH cable (`100 mm`) | MaiXu `MX126-5.0-02P` screw | Phoenix `PST 1,0` (KNX) + `MX126` screw (AUX) | Winstar `WEA012864D-03` 0.96" 128x64 (4-pin THT) | 2 x 2.5x10 screws + UI snap hooks |
| **V2** (`v2`) | 2 DIN (`36 mm`) | 3 (`main`, `interface`, `ui`) | 10-pin JST-SH cable (`100 mm`) + 2 B2B headers (`1x4`, `1x3`) | MaiXu `MX126-5.0-02P` screw | Phoenix `PST 1,0` (KNX) + `MX126` screw (AUX) | Winstar `WEA012864D-03` 0.96" 128x64 (4-pin THT) | 2 x 2.5x10 screws + UI snap hooks |
| **V3** (`v3`) | 1 DIN (`18 mm`) | 2 (`main` two-sided vertical, `ui`) | 8-pin 0.5 mm FFC (`50 mm`, `C6936295`) | Kefa `KF142R-5.08-2P` push-in spring (`C475144`, 250 V 15 A) | Two Phoenix `PST 1,0` pin strips on `main` (red/black KNX + yellow/white AUX, right-side pockets) | Crystalfontz `CFAL12832D-B` 0.91" 128x32 (12-pin 0.5 mm ZIF `C2856799`, no soldering) | Top/bottom shoulder snap-click tongues + UI snap hooks |

## Unified XIAO ESP32-C6 Pin Map (`V1`, `V2`, `V3`)

| XIAO Pin | ESP32-C6 GPIO | Schematic Net | Direction | Function |
|---|---|---|---|---|
| **1 (`D0`)** | `GPIO0` | `LED_DATA` | Output (RMT) | Drives the 5 V `SN74AHCT1G125` level-shifter (`U1` on UI) into the 3-LED `SK6805-EC15` chain |
| **2 (`D1`)** | `GPIO1` | `DALI_SUPPLY_OFF` | Output | High drives the `TLP383` optocoupler (`U4` on `main`) to pull `REM3-2415S/A/CTRL` pin 1 (`CTRL`) to ~5.6 V (supply off); low/Hi-Z = supply on |
| **3 (`D2`)** | `GPIO2` (`ADC1_CH2`) | `BTN_LADDER` | Analog in | Reads the two-button resistor ladder on the UI board (`SW1` PRG/RST, `SW2` FUNC) |
| **4 (`D3`)** | `GPIO21` | `NC` | Unused | Unconnected spare GPIO on all variants |
| **5 (`D4`)** | `GPIO22` | `I2C_SDA` | I2C | SSD1306 OLED data (`4.7k` pull-up to `+3V3`, I2C address `0x3C`) |
| **6 (`D5`)** | `GPIO23` | `I2C_SCL` | I2C | SSD1306 OLED clock (`4.7k` pull-up to `+3V3`) |
| **7 (`D6`)** | `GPIO16` | `KNX_RXD` | Output (`UART1 TX`) | MCU TX -> `1k` series back-power-protection resistor -> `NCN5130` pin 27 (`SDI/RXD`, 19200 baud 9-bit) |
| **8 (`D7`)** | `GPIO17` | `KNX_TXD` | Input (`UART1 RX`) | `NCN5130` pin 28 (`SDO/TXD`) -> MCU RX |
| **9 (`D8`)** | `GPIO19` | `DALI_RX` | Input (RMT RX) | From DALI RX optocoupler (`U6` on V1/V3 `main`, `U2` on V2 `interface`); bus idle high = `DALI_RX` low (`dali_invert_rx = true`) |
| **10 (`D9`)** | `GPIO20` | `NCN_SAVEB` | Input | `NCN5130` pin 36 (`SAVEB`, open-drain with `10k` pull-up to `+3V3`); goes low when KNX bus voltage drops |
| **11 (`D10`)** | `GPIO18` | `DALI_TX` | Output (RMT TX) | Drives DALI TX optocoupler LED (`U5` on V1/V3 `main`, `U1` on V2 `interface`); high = bus pulled low (`dali_invert_tx = false`) |
| **12 (`3V3`)** | — | `+3V3` | Power out | 3.3 V output from the XIAO's onboard regulator, powering SELV logic, pull-ups and the OLED |
| **13 (`GND`)** | — | `GND` | Ground | SELV-KNX ground |
| **14 (`5V`)** | — | `+5V_XIAO` | Power in | Fed from the KNX `+5V` buck (`SCT2400`) through the `LM66100` ideal diode so USB-C power cannot back-feed the KNX buck |

## Addressable Status LEDs (`D0 / GPIO0 = LED_DATA`)

Three `SK6805-EC15` addressable RGB LEDs (`D1 -> D2 -> D3`) sit on the UI board,
buffered to 5 V by `U1` (`SN74AHCT1G125`) through a `330R` series resistor
(`gateway/src/device/StatusLeds.hpp`):

| Chain Index | Ref on UI PCB | Front Label | Meaning (`StatusLeds.cpp`) |
|---|---|---|---|
| **`0`** (`device::status_led`) | **`D1`** (left) | `STATUS` | Device & WiFi status: green = normal (with brief cyan beacon every 5 s when WiFi is connected, or slow blue blink in WiFi setup); fast white blink = ETS/dashboard identify; fast red blink = both buttons held (factory-reset arming); solid red = internal/config fault |
| **`1`** (`device::knx_led`) | **`D2`** (middle) | `KNX` | KNX bus & programming mode: **solid red = KNX programming mode active**; green (flickering dim-green on telegram activity) = KNX bus OK; slow red blink = KNX bus voltage missing (`NCN_SAVEB` low) |
| **`2`** (`device::dali_led`) | **`D3`** (right) | `DALI` | DALI line state: green (flickering dim-green on frame activity, brief cyan on multi-master frame) = DALI OK; slow amber blink = commissioning in progress; slow red blink = lamp/gear fault; fast red blink = DALI bus short / no power |

## Button Ladder (`D2 / GPIO2 = BTN_LADDER`) & Reset / Programming Behaviour

On the UI board, `BTN_LADDER` has a `10k` pull-up (`R1`) to `+3V3` and a `100nF`
filter capacitor (`C1`) to `GND`:
- **`SW1` (`PRG/RST`, left button):** switches `20k` (`R2`) to `GND`.
- **`SW2` (`FUNC`, right button):** switches `10k` (`R3`) to `GND`.

| Buttons Pressed | Equivalent Pull-Down | Nominal Voltage (`BTN_LADDER`) | ADC Threshold Window (`ButtonLadder.hpp`) | Gestures (`FrontPanel.hpp` / `KnxDevice.cpp`) |
|---|---|---|---|---|
| **None (idle)** | `open` | **`3300 mV`** | `>= 2750 mV` | Idle |
| **`SW1` (`PRG/RST`) only** | `20.0 kΩ` | **`2200 mV`** | `1925 .. 2749 mV` | **Tap (`< 5 s`):** `Gesture::Reboot` — calls `esp_restart()`, which reboots the ESP32-C6 and, on startup (`app_.start()`), sends the TP-UART `U_Reset.req` (`0x01`) command over `KNX_RXD` (`D6/GPIO16`) to reset the `NCN5130` transceiver state machine while preserving all NVS configuration.<br>**Hold `5 s`:** `Gesture::ToggleProgMode` — calls `knx_.toggleProgMode()`, putting the `thelsing/knx` stack into ETS programming mode (reported on the KNX bus via TP-UART and indicated by `D2 KNX` turning solid red). |
| **`SW2` (`FUNC`) only** | `10.0 kΩ` | **`1650 mV`** | `1485 .. 1924 mV` | **Tap (`< 5 s`):** `Gesture::NextPage` — wakes the OLED and advances the status page.<br>**Hold `5 s`:** `Gesture::WifiSetup` — enters WiFi provisioning mode. |
| **Both `SW1 + SW2`** | `6.67 kΩ` (`20k ∥ 10k`) | **`1320 mV`** | `< 1485 mV` | **Hold `10 s`:** `Gesture::FactoryReset` (`PanelHint::FactoryResetArming` blinks `D1 STATUS` fast red while held) — erases KNX (`eeprom`) and DALI (`targets_v1`) NVS namespaces and calls `esp_restart()`. |

## Cover Variants Generated in Each Release (`enclosure/`)

Every variant (`v1`, `v2`, `v3`) includes three ready-to-print cover files generated
from the exact same `front_legend.py` geometry:
1. **`cover.stl` / `cover.step`:** single-colour cover ( without text cuts on `cover.stl`, or with debossed cutouts in `cover_color`).
2. **`cover_embossed.stl` / `cover_embossed.step`:** single-colour cover with **`+0.3 mm` raised embossed text and LED rings** matching `front_legend.svg`.
3. **`cover_color.3mf` / `cover_color_obj.zip`:** multi-material / full-colour WJP resin cover with flush `0.3 mm` inlaid legend bodies.
