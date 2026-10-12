# Hardware variants

Three variants share one set of circuits and, where possible, the same small sub-boards.
The previous attempt failed because everything (KNX transceiver, ESP32, USB, DALI interface,
DALI supply, terminals) was packed onto one 36 mm wide vertical board, placed by script and
routed by Freerouting. Splitting the design into small single-purpose boards is what makes
2 DIN modules comfortable; OpenKNX's REG1-Dali fits a complete KNX/DALI gateway into a
1-module case that way (controller board, a perpendicular 18 x 17 mm KNX module, a DALI
application board and a front board on a 10-pin FPC). Their files are CC-BY-NC-SA, so they
are inspiration only; nothing is copied.

## Envelope (user measurement, authoritative)

| | Per module | 2 DIN | 3 DIN |
|---|---|---|---|
| Hidden part (under the distribution-board cover) | 18 W x 81 H x 44 D | 36 x 81 x 44 | 54 x 81 x 44 |
| Front protrusion (visible) | 18 W x 45 H x 17 D | 36 x 45 x 17 | 54 x 45 x 17 |
| Max board width inside (1.6 mm walls, 0.5 mm clearance) | | 31.5 | 49.5 |
| Max board height inside (hidden part) | | 76.5 | 76.5 |
| Front board max (inside the protrusion) | | 31.5 x 40.5, 15 deep | 49.5 x 40.5, 15 deep |

The rail space is already excluded from the 44 mm depth. The front protrusion is centred
(user-confirmed), so the shoulders above and below it are (81 - 45) / 2 = 18 mm tall. The
terminals (DALI and AUX on top, KNX at the bottom) need about one module's width of a
shoulder; the rest of the shoulder area is ordinary component space.

## Building blocks

| Block | Content | Notes |
|---|---|---|
| MCU | Seeed XIAO ESP32C6 (21 x 17.5 mm) on 2 x 7 female headers | Pluggable, raised ~8.5 mm so passives fit underneath; brings USB-C, its own 3V3 regulator bypass, ceramic antenna and a U.FL option for metal cabinets. Removes USB-C, ESD and module footprint from our boards. |
| KNX | NCN5130 + passives + storage rail (~20 V / 470 µF) + 3V3 buck | KNX terminal pins on the bottom shoulder. |
| DALI | Bridge, TX sink, RX threshold, 2 x SMD long-creepage optocouplers, isolated DC-DC + current limiter + disable switch, DALI and AUX terminals | Hands only isolated logic TX/RX and SELV ground to the rest. |
| UI | Tall-actuator PROG and RESET buttons, 3 LEDs, OLED header | Always its own board in the front protrusion, connected by a 10-pin cable/FPC. |

## Variants

### POC: 3 DIN, 2 boards (released as v1)
- Main board 49.5 x 76.5 mm, 4 layers (In1 solid ground per isolation domain), parallel to the
  rail with its top face at z = 31 so the mated KNX plug ends just below the shoulder face; tall
  parts (storage capacitor, DC-DC) on the rear side.
- Top shoulder: DALI + AUX terminals and the internal-supply jumpers, DALI block below them
  behind a 6 mm slot.
- Middle: XIAO raised on headers, passives underneath.
- Bottom: KNX transceiver block, KNX pin strip in the bottom-right corner.
- UI board (2 layers) in the protrusion: 0.96" OLED, two pen-operated buttons, three LEDs.

### Second: 2 DIN, more than 2 boards, 2 layers
- Carrier board 31.5 x 76.5 mm parallel to the rail: terminals, XIAO on headers, board-to-board
  headers.
- KNX sub-board (about 18 x 20 mm), soldered perpendicular to the carrier at the bottom shoulder
  (castellated pads or a 1 x 7 2.54 mm header), like a NanoBCU.
- DALI sub-board (about 30 x 25 mm), perpendicular or stacked at the top shoulder; only the
  isolated logic signals and SELV ground cross to the carrier, so the carrier has no DALI copper
  and no isolation band.
- UI board in the protrusion.

### Golden: 2 DIN, 2 boards, 4 layers
- One main board 31.5 x 76.5 mm, 4 layers (signal / GND / power / signal), double-sided
  placement, XIAO, DALI block behind a slot at the top shoulder.
- UI board in the protrusion with a populated LCD/OLED (required for golden), LEDs and buttons.
- Stretch goal: the same in 1 DIN (front face 18 x 45 mm, e.g. a 0.91 in 128 x 32 OLED mounted
  vertically above the buttons and LEDs).

## Process
1. Each block's placement is designed deliberately (reference layouts, decoupling at the pins,
   escape routes kept free) and checked with top / bottom / overlay previews before routing.
2. GND and power first, Freerouting for the rest, KiCad DRC including creepage rules.
3. A connection that cannot be routed after one pass is reported, not iterated on blindly;
   the last few may be finished by hand in KiCad.
4. Sub-boards are routed once and reused by the variants that use them.

## Open electrical questions
- DALI supply voltage (user decision): anywhere between 12 V and 20 V, optimised for size. Plan:
  keep the 15 V isolated converter and replace the LM317 (3-4 V drop) with a small transistor
  current limiter (about 1 V drop) so the bus sits at about 14 V.
- The XIAO is powered from KNX in normal operation. Its power path (5 V pin vs 3V3 pin, what
  happens when USB is plugged in at the same time) must be checked against Seeed's schematic
  before the feed point is chosen.
- WiFi power from KNX alone: storage rail sizing against the real ESP32-C6 duty cycle.
- USB and KNX must not be connected at the same time without a USB isolator: the gateway's
  ground is KNX minus, and a computer's USB ground is often tied to protective earth.
