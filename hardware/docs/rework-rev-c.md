# Rework of boards and prints ordered from commit `59eea1f`

For PCBA and enclosures made from the `59eea1f` releases (v2 rev B, v3 rev C). The sources and
releases after the 2026-10-22 review already contain these changes. Background:
[review-2026-10-22.md](review-2026-10-22.md).

Do the steps before the first power-up. Work ESD-safe; all changed parts are 0603 / 1206 chip resistors.

## 1. Parts to buy (per gateway)

| Value | Package | Qty | LCSC | Replaces |
|---|---|---|---|---|
| 4.7 kΩ 1 % | 0603 | 2 | C23162 | TX gate series 22k, RX pull-up 10k |
| 10 kΩ 1 % | 0603 | 1 | C25804 | TX gate pull-down 100k |
| 2.0 Ω 1 % 250 mW | 1206 | 1 | C17942 | TX clamp sense 3.3 Ω |
| 100 kΩ 1 % | 0603 | 1 | C25803 | 5 V buck EN divider top 120k (optional, see 2c) |

Also: flush cutters, 4 mm PU/EPDM foam (10 × 20 mm), four 2 mm self-adhesive foam or rubber pads
(about 3 × 1 mm), a fine flat file / scalpel / 3 mm ball burr, PETG for two small printed pins (v3).

## 2. Re-soldering

### 2a. v3 main board (all on the BACK side, the side without the tall parts)

| Ref | Fitted | Fit | Function |
|---|---|---|---|
| R16 | 22k | **4.7k** | TX gate series |
| R17 | 100k | **10k** | TX gate pull-down |
| R18 | 3.3 Ω (1206) | **2.0 Ω** | TX clamp sense |
| R21 | 10k | **4.7k** | DALI_RX pull-up |
| R5 (front side) | 120k | **100k** | buck EN, optional (2c) |

No 2.0 Ω resistor at hand? Remove **Q4** (BC847 next to R18) instead. The bus supply alone then limits
the transmit current. That is allowed by DALI, but there is no clamp left against a wrongly connected
supply.

### 2b. v2

| Board | Ref | Side | Fitted | Fit |
|---|---|---|---|---|
| interface | R3 | front | 22k | **4.7k** |
| interface | R4 | back | 100k | **10k** |
| interface | R5 | back (1206) | 3.3 Ω | **2.0 Ω** (or remove Q2, front) |
| interface | R8 | front | 10k | **4.7k** |
| main | R5 | front | 120k | **100k** (optional, 2c) |

### 2c. Buck EN (R5), optional

Needed when the KNX bus voltage at the gateway can fall below about 23 V (long line, many devices). On
a short line with a 29–30 V supply, the 120k version starts too. Check: with the bus connected, `+5V`
must be present (step 7).

### 2e. XIAO ideal diode U7, optional (USB back-feed)

U7 (LM66100, SC70-6; v3 main under the XIAO, v2 UI board) has its CE pin 3 on GND. That makes it an
always-on switch, so USB power on the XIAO back-feeds the 5 V rail, the UI LEDs and, through the buck,
the KNX storage capacitor. It is harmless for USB service with everything else disconnected. Fix it
if you want to plug in USB while KNX is connected:

1. Lift pin 3 (CE; the corner pin after VIN 1 and GND 2 on the same side) off its pad with a fine tip.
2. Bridge it to pin 6 (VOUT; the diagonally opposite corner, across from VIN) with 0.1 mm enamelled wire.
3. Check: no continuity from pin 3 to GND.

Without the fix, unplug the XIAO's USB before connecting KNX.

### 2d. Check before power

Measure on the unpowered board (DMM, resistance between the pads of each new part): 4.6–4.8k, 9.9–10.1k,
2.0 Ω (subtract the lead resistance), 99–101k. No solder bridges to neighbouring pads.

## 3. Through-hole leads (v3 main)

Cut every lead on the back side to **at most 1.5 mm** above the copper with flush cutters. That covers U3
(REC3: 6 pins; first clip unused pins 3 and 11 flush if not done), U8 sockets (14), J1/J2 (PST strips),
J3 (KF142R, 4), JP1 (4). Then run a fingertip over the back: nothing may catch. The cover wall is
2.0 mm away.

## 4. Grinding the printed v3 parts (rev C print)

Coordinates: **y** from the bottom outer face of the part (KNX side = 0, DALI side = 81 mm); **z** from the
rear (rail-side) edge of the case. The cover's left wall starts at z = 1.6 and ends at z = 42.4 (its
front edge, against the shoulder plates). Depth is measured into the wall from its inner face. Test-fit
after each step; take off a little at a time.

### 4a. Cover, left wall inner face: three channels over back-side parts

The cover slides on from the front, so each channel runs from the given z to the **front edge of the
wall** (open there).

| For | y | z from | Depth into the wall (1.6 mm thick) |
|---|---|---|---|
| D6 bridge | 66.5–78.5 | 7.9 | 0.9 mm (0.7 mm wall left) |
| D7 TVS | 64.0–68.2 | 33.3 | 0.75 mm |
| D8 diode | 52.2–56.4 | 33.9 | 0.75 mm |

Use a flat file or a 3 mm ball burr and check the depth with a caliper depth rod. Do not break through.

### 4b. Cover, left wall inner face: four backing pads

Stick a 2 mm pad (compresses to 1.9 mm) on the wall's inner face at each point. Each sits in a
part-free strip along the board edge:

| y | z |
|---|---|
| 1.7–2.5 | 3.4–6.4 |
| 1.7–2.5 | 37.4–40.4 |
| 78.5–79.3 | 3.4–6.4 |
| 78.5–79.3 | 37.4–40.4 |

### 4c. Base, bottom and top walls: board edge clearance

The board is 0.2 mm larger than the gap between the walls. On the inner faces of the bottom wall (y =
1.6) and the top wall (y = 79.4), scrape **0.3 mm** off a strip running from the open left side **3.5 mm
inwards** (x = -9 to -3.9), along the whole depth. The board must slide in from the left by hand
without force.

### 4d. Base, top wall: DALI terminal slot (J3)

The slot for the KF142R wire entry stops about 1 mm short of the base's front edge, and the terminal
reaches it. Remove that bridge, so the slot runs open to the front edge (z 41.4–42.4), and widen the
slot by 0.2 mm on its right-hand side. Lightly sand the underside of the cover's top shoulder plate
over the terminal (0.3 mm).

### 4e. Cover: OLED supports

- Cut off the **two vertical rails** behind the OLED (x ±5.6–7.0, y 31.8–56.5), flush with the floor of
  the display recess.
- Cut off the **two round standoffs** near the top of the display (Ø 3 mm at x ±5.6, y 59).
- Keep the two small ledges just below the display (y 30.9–31.7); they locate the panel.
- In assembly: panel face down in the recess, then the 4 mm foam pad on the UI board's front under
  the panel (no parts there). Snapping in the UI board compresses the foam, which holds the panel
  against the front skin and the board against the snap hooks.

### 4f. Buttons: printed pins

Print `hardware/releases/v3/rework/button_pin.stl` twice (collar down, PETG, 0.1 mm layers). Each is a
Ø 3.0 × 2.25 mm collar plus a Ø 2.2 × 2.6 mm stem. Before snapping in the UI board, drop one pin into
each button hole from the inside, collar inwards. It rests on the B3U actuator, and its stem stands
about 1 mm proud of the front. Press check: a clear click with each pin, and no click at rest.

## 5. Grinding the printed v2 parts (rev B print)

- **Cover, top screw tab** (the tab hanging behind the top wall, between the AUX and DALI terminals):
  file **1.0 mm** off its left face (the AUX side, as seen from the front), over its full height.
- **Cover, right locating lip** (the 1.2 mm strip along the inside of the right wall): cut it away
  over the **top 9 mm**, next to the DALI terminal.

## 6. v3 FFC check before plugging in

Before plugging the 8-pin FFC in, fold a paper strip of the same width along the intended path
between main J4 and UI J1. Mark pin 1 at J4 (main side, the pin nearest the board's lower edge on the
silkscreen) and check it arrives at pin 1 of J1. Check also whether the contacts face the same side at
both ends (Type A) or opposite sides (Type B; the BOM lists Type B). If it disagrees, use the other
cable type.

## 7. Bring-up measurements (expected values)

Bench supply for KNX: a 29–30 V KNX supply with choke. AUX: 24 V. DALI: leave it open first.

| Point | Condition | Expected |
|---|---|---|
| VFILT (C4 +) | KNX only | bus voltage minus 2.3–4.3 V (e.g. 25–27 V at 29.5 V) |
| +5V (buck output) | KNX only | 4.85–5.05 V |
| XIAO 3V3 | KNX only | 3.25–3.35 V |
| NCN_SAVEB (XIAO D9) | KNX on / KNX off | 3.3 V / 0 V |
| BTN_LADDER (D2) | none / PRG / FUNC / both | 3.30 / 2.20 / 1.65 / 1.32 V (±5 %) |
| DA+ to DA- | AUX 24 V, JP1 both caps, firmware supply on | 14.2–14.6 V |
| DALI supply current limit | XIAO unplugged (supply stays on), short DA+ to DA- through an ammeter for under 2 s | 165–215 mA |
| DALI_RX (D8) | supply on, bus idle / bus shorted | 0 V / 3.3 V |
| DA low level | gateway transmitting, internal supply | 1.5–3.0 V |
| TX half-bit (scope on DA) | forward frame | 400–433 µs low and high |
| DALI_RX edges vs DA (scope) | gateway or other master transmitting | skew under 40 µs; note it for firmware |
| Bus current KNX | WiFi off / on (modem sleep) | write both down; must not exceed 40 mA |
| Case temperature next to Q1 | 10 min of continuous DALI polling | under 15 K above ambient |

A sustained DALI short must make the DALI LED blink fast red and drop DA to 0 V after about 80 ms (latched).
Clear it with the internal-PSU reset in the web UI or a reboot.
