# Hardware variants (V2, V3)

Both variants share one set of circuit blocks (`hardware/circuits/blocks/`),
the same **RECOM `REC3-2415SRW/H2/A/CTRL`** (or `/H4/A/M/CTRL`; functional isolation, no reinforced part by design) isolated DALI supply with firmware shut-off,
and the exact same **Seeed XIAO ESP32-C6** pin map, LED chain and ADC button ladder
(`gateway/src/firmware/Board.hpp`).

| Variant | Width | Boards | Main-to-UI Link | DALI Terminal | KNX / AUX Terminals | OLED Display | Case Closure |
|---|---|---|---|---|---|---|---|
| **V2** (`v2`) | 2 DIN (`36 mm`) | 3 (`main`, `interface`, `ui`) | 10-pin JST-SH cable (`100 mm`) + 2 B2B headers (`1x4`, `1x3`) | MaiXu `MX126-5.0-02P` screw | Phoenix `PST 1,0` (KNX) + `MX126` screw (AUX) | Winstar `WEA012864D-03` 0.96" 128x64 (4-pin THT) | 2 x 2.5x10 screws + UI snap hooks |
| **V3** (`v3`) | 1 DIN (`18 mm`) | 2 (`main` two-sided vertical, `ui`) | 8-pin 0.5 mm Type B FFC (`50 mm`, `C42416274`) | WAGO `2086-1202/700-000/997-604` 3.5 mm SMD push-in, buttons in the wire-entry face (hand-soldered; 160 V III / 320 V II) | Two Phoenix `PST 1,0` pin strips on `main` (red/black KNX + yellow/white AUX, right-side pockets) | `QG-6428TSWCG02` / `NFP1312-15AY` 0.96" `64×128` portrait `SSD1312` (`14 × 28 mm`, 14-pin 0.62 mm solder FPC) | Side-opening clamshell with 6 side snap-clicks + UI snaps |

## Unified XIAO ESP32-C6 Pin Map (`V2`, `V3`)

| XIAO Pin | ESP32-C6 GPIO | Schematic Net | Direction | Function |
|---|---|---|---|---|
| **1 (`D0`)** | `GPIO0` | `LED_DATA` | Output (RMT) | Drives the 5 V `SN74AHCT1G125` level-shifter (`U1` on UI) into the 3-LED `WS2812B-MINI-V3J` chain |
| **2 (`D1`)** | `GPIO1` | `DALI_SUPPLY_OFF` | Output | High drives the `TLP383` optocoupler (`U4` on `main`) to pull `REC3-2415SRW/H2/A/CTRL` pin 1 (`CTRL`) to ~5.6 V (supply off); low/Hi-Z = supply on |
| **3 (`D2`)** | `GPIO2` (`ADC1_CH2`) | `BTN_LADDER` | Analog in | Reads the two-button resistor ladder on the UI board (`SW1` SCROLL left, `SW2` RESET right) |
| **4 (`D3`)** | `GPIO21` | `NC` | Unused | Unconnected spare GPIO on all variants |
| **5 (`D4`)** | `GPIO22` | `I2C_SDA` | I2C | SSD1306 OLED data (`4.7k` pull-up to `+3V3`, I2C address `0x3C`) |
| **6 (`D5`)** | `GPIO23` | `I2C_SCL` | I2C | SSD1306 OLED clock (`4.7k` pull-up to `+3V3`) |
| **7 (`D6`)** | `GPIO16` | `KNX_RXD` | Output (`UART1 TX`) | MCU TX -> `1k` series back-power-protection resistor -> `NCN5130` pin 27 (`SDI/RXD`, 19200 baud 9-bit) |
| **8 (`D7`)** | `GPIO17` | `KNX_TXD` | Input (`UART1 RX`) | `NCN5130` pin 28 (`SDO/TXD`) -> MCU RX |
| **9 (`D8`)** | `GPIO19` | `DALI_RX` | Input (RMT RX) | From DALI RX optocoupler (`U6` on V3 `main`, `U2` on V2 `interface`); bus idle high = `DALI_RX` low (`dali_invert_rx = true`) |
| **10 (`D9`)** | `GPIO20` | `NCN_SAVEB` | Input | `NCN5130` pin 36 (`SAVEB`, open-drain with `10k` pull-up to `+3V3`); goes low when KNX bus voltage drops |
| **11 (`D10`)** | `GPIO18` | `DALI_TX` | Output (RMT TX) | Drives DALI TX optocoupler LED (`U5` on V3 `main`, `U1` on V2 `interface`); high = bus pulled low (`dali_invert_tx = false`) |
| **12 (`3V3`)** | — | `+3V3` | Power out | 3.3 V output from the XIAO's onboard regulator, powering SELV logic, pull-ups and the OLED |
| **13 (`GND`)** | — | `GND` | Ground | SELV-KNX ground |
| **14 (`5V`)** | — | `+5V_XIAO` | Power in | Fed from the KNX `+5V` buck (`SCT2400`) through the `LM66100` ideal diode so USB-C power cannot back-feed the KNX buck |

Core rail (v2 and v3): `+5V` comes from a leaded priority path: the AUX converter (`U9`) through `AO3401A` `Q9`, switched on by the `TLV803M` supervisor `U10` (4.38 V) via `2N7002` `Q10` while AUX is healthy, and the KNX `SCT2400` buck (`+5V_BUCK`, 4.94 V) through the `B5819W` Schottky `D11`, which takes over without a gap when AUX goes away (rail ~4.6 V on KNX alone). The XIAO's `LM66100` has CE on VOUT for reverse current blocking (USB back-feed).

## Addressable Status LEDs (`D0 / GPIO0 = LED_DATA`)

Three `WS2812B-MINI-V3J` addressable RGB LEDs (gull-wing legs, LCSC C22371519) (`D1 -> D2 -> D3`) sit on the UI board,
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
- **`SW1` (`SCROLL`, left button, plain bump):** switches `10k` to `GND`.
- **`SW2` (`RESET`, right button, X bump):** switches `20k` to `GND`.

| Buttons Pressed | Equivalent Pull-Down | Nominal Voltage (`BTN_LADDER`) | ADC Threshold Window (`ButtonLadder.hpp`) | Gestures (`FrontPanel.hpp` / `KnxDevice.cpp`) |
|---|---|---|---|---|
| **None (idle)** | `open` | **`3300 mV`** | `>= 2750 mV` | Idle |
| **`SW2` (`RESET`) only** | `20.0 kΩ` | **`2200 mV`** | `1925 .. 2749 mV` | **Tap (`< 5 s`):** `Gesture::Reboot` — calls `esp_restart()`, which reboots the ESP32-C6 and, on startup (`app_.start()`), sends the TP-UART `U_Reset.req` (`0x01`) command over `KNX_RXD` (`D6/GPIO16`) to reset the `NCN5130` transceiver state machine while preserving all NVS configuration.<br>**Hold `5 s`:** `Gesture::ToggleProgMode` — calls `knx_.toggleProgMode()`, putting the `thelsing/knx` stack into ETS programming mode (reported on the KNX bus via TP-UART and indicated by `D2 KNX` turning solid red). |
| **`SW1` (`SCROLL`) only** | `10.0 kΩ` | **`1650 mV`** | `1485 .. 1924 mV` | **Tap (`< 5 s`):** `Gesture::NextPage` — wakes the OLED and advances the status page.<br>**Hold `5 s`:** `Gesture::WifiSetup` — enters WiFi provisioning mode. |
| **Both `SW1 + SW2`** | `6.67 kΩ` (`20k ∥ 10k`) | **`1320 mV`** | `< 1485 mV` | **Hold `10 s`:** `Gesture::FactoryReset` (`PanelHint::FactoryResetArming` blinks `D1 STATUS` fast red while held) — erases KNX (`eeprom`) and DALI (`targets_v1`) NVS namespaces and calls `esp_restart()`. |

## Cover Variants Generated in Each Release (`enclosure/`)

Every variant (`v2`, `v3`) includes two ready-to-print cover files generated
from the exact same `front_legend.py` geometry:
1. **`cover.stl` / `cover.step`:** plain cover without legend cuts.
2. **`cover_embossed.stl` / `cover_embossed.step` (v2 only):** single-colour cover with **`0.6 mm` recessed text** (no marks around the LEDs). V3 has no front text; its button bumps are plain (SCROLL) and X-grooved (RESET).
