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

### V3 (draft): 1 DIN, board perpendicular to the rail, DALI supply kept

Status: concept for discussion; nothing below is placed, routed or verified yet. Part data
quoted from manufacturer datasheets/catalogues; every part still goes through the usual
supplier/pinout checks before layout.

#### Width budget (the one dimension that hurts)

| | mm |
|---|---|
| Module width | 18.0 |
| Walls 2 x 1.6, clearance 2 x 0.5 | -4.2 |
| **Space for one board + its parts, across the module** | **13.8** |
| Main board (1.6) + low SMD on its back (<= 1.5) | -3.1 |
| **Tallest part on the front of the main board** | **<= 10.7** |

Board area is plentiful (about 76 x 40 mm on a board perpendicular to the rail); thickness
is what decides every part choice below.

#### Architecture: one main board and three small boards
1. **Main board**, perpendicular to the rail (board plane = side wall), 4 layers, about
   76 x 40 mm: KNX block, XIAO, 5 V buck, the **isolated DALI supply (DIP24, 10.2 mm)** with
   its current limiter, the DALI interface (bridge, TX sink, RX, optocouplers). Three
   isolation domains with 6 mm bands and slots, as on v1's main board.
2. **Top shelf board**, horizontal (parallel to the top face), small, under the top shoulder,
   plugged onto the main board's DALI region with a right-angle 2-pin header: the DALI
   terminal (DA DA) only.
3. **Bottom shelf board**, horizontal, under the bottom shoulder: the KNX pin strip
   (red/black) and the AUX pin strip (yellow/white) behind it, pins pointing down, so both
   plugs go in from the bottom face. This replaces an angled KNX header: I found none in the 5.75 mm KNX family (the
   Phoenix PST 1,0 strips and the TXGA FPH57501 are vertical); a straight strip on a
   horizontal board does the same job with a part we already verified.
4. **UI board** in the front protrusion, parallel to the front face.

#### DALI supply: regulated reinforced DIP24 on the main board
The front protrusion does not help a tall converter: it has the same 18 mm width, and width
is the limit everywhere. What helps is a lower part.

| Converter | Size (mm) | Input | Output | Insulation | Verdict |
|---|---|---|---|---|---|
| RECOM REM3-2415S/A (v1/v2) | DIP24, 31.8 x 20.3 x 10.2-10.7 | 18-36 V | 15 V 200 mA | reinforced, 250 VAC | height uncertain between sources |
| **Traco THM 3-2415** | **DIP24, 31.8 x 20.3 x 10.2** | **18-36 V regulated** | **15 V 200 mA** | **reinforced, 5000 VAC, 2 x MOPP** | **proposed: fits 10.7, remote on/off** |
| RECOM REM3.5E-2415S/A/CTRL | DIP24, 31.8 x 20.3 x 10.2 (SMD 10.9) | 18-36 V regulated | 15 V, 3.5 W | reinforced, >8 mm creepage | alternative, also fits |
| RECOM REM2-1215S | SIP8, 23.0 x 8.0 x 12.2 | 12 V +-10 % unregulated | 15 V, 2 W | reinforced, >9 mm external | needs a 12 V pre-buck and a shelf board; 2 W only |
| Traco TRN 3-2415 | SIP, 11.9 x 7.7 x 11.0 | 18-36 V regulated | 15 V, 3 W | **functional only**, 1600 VDC test, not certified | **not usable**: DALI-SELV needs reinforced insulation |

- THM 3-2415 (Traco datasheet): 10.2 mm high, inside the 10.7 mm budget with 0.5 mm
  margin; takes the 20-32 V AUX range directly (no pre-regulator); 3 W keeps the v1/v2
  current limit of about 180 mA, so DALI-2 input devices still have headroom.
- Its remote on/off pin is on the input (AUX) side, so the firmware can switch the DALI
  supply off on a shorted bus through a simple level shift in the SELV-AUX domain, no extra
  optocoupler. The jumper stays for "external supply" installations.
- The limiter transistor (DPAK) sits on the main board with real copper for its up to
  ~2.7 W in a bus short (15 V x 0.18 A), until the firmware switches the supply off.

#### Power inputs on the bottom shoulder: two KNX plugs (user decision: A)
Top shoulder: only the DALI terminal (one 2-pole 5.0 mm terminal, 10 mm wide, fits). AUX
cannot sit next to it (10 + 10 mm > 13.8, and DALI-AUX needs 6 mm creepage that adjacent
3.5 mm poles do not give), whichever converter is used, so KNX and AUX both enter at the
bottom shoulder, from the bottom face, on a small horizontal shelf board. KNX-AUX needs only
2 mm creepage (both SELV). The user chose A; B stays as the alternative:

- **A, standard KNX plugs (chosen):** two 5.75 mm pin strips one behind the other:
  **red/black** for the KNX bus (powers the gateway: ESP32 and core) and **yellow/white** for
  AUX 20-32 V (feeds only the isolated DALI supply). Mating plugs: Phoenix PTS 0,5/2-PH-5,75
  BKRD (1574300) and GY35YE (1574299). The plug is about 12.45 mm wide (Phoenix listing; to
  confirm on the drawing), inside the 13.8 mm. Installers get the normal KNX behaviour: a
  4-entry plug keeps the bus line connected when the device is pulled, and KNX supplies
  (MDT STC, ekinex, ABB) deliver their 30 V auxiliary output on exactly this yellow/white
  terminal. Plugs take solid 0.6-0.8 mm wire, so a stranded 24 V PSU lead needs a short
  solid tail. Gripping the rear plug past the front one needs checking on a print.
- **B, one spring terminal (alternative, not chosen):** Kefa **KF141V-2.54**, 5 positions (KNX+, KNX-, empty, AUX+,
  AUX-), 12.7 mm wide, 13.6 mm tall, push-button release, 20-26 AWG, 150 V / 2 A, on LCSC
  (JLC can fit it). The empty middle position gives the KNX-AUX creepage. It takes stranded
  PSU leads and is one cheap part, but the KNX line ends in the device (no 4-entry plug, so
  pulling the device opens the bus wires), and 20 AWG is the upper limit for 0.8 mm KNX wire.
  The datasheet's "1 mm2" next to "20-26 AWG" needs checking.

Fallback if neither fits: a two-level 5.0 mm screw terminal on the top shelf (DALI lower deck,
AUX upper deck), with the deck-to-deck creepage checked.

#### Storage capacitor: SMD
470 uF / 35 V aluminium SMD, D10 x 10.2 mm (several on LCSC, e.g. Honor Elec
RYRVT35V470UF10*10, C48970703: 286 mA ripple at 120 Hz, 105 C 2000 h). Height about 10.5 mm
on the main board front, inside the 10.7 mm budget, and it keeps NCN5130 Table 6's 470 uF
(no need for two 220 uF). Its ripple/ESR rating against the KNX storage duty is to verify.

#### Removable XIAO
The pre-soldered XIAO (square 0.64 mm pins) on 8.5 mm sockets is 14.5 mm, far too tall.
Low-profile option: bare XIAO with turned-pin (machined) male strips soldered to it, plugged
into machined low-profile sockets such as Mill-Max 311-43 / 324 series (insulation height
2.79 mm, on LCSC). Expected height about 2.8 + ~2.5 + 3.5 = ~8.8 mm, inside the budget, still
removable. Round machined sockets do not accept the pre-soldered XIAO's square pins, so the
XIAO is the bare 113991254 plus turned-pin strips. USB-C faces the front so it is reachable
with the cover off.

#### Front face: vertical OLED
Inside the protrusion about 13.8 x 40.5 mm.
- **Winstar WEO012832F**, 0.91" 128 x 32 SSD1306 COG, 30.0 x 11.5 x 1.26 mm, mounted
  vertically (portrait). It has an FPC, so the UI board gets a small FPC connector
  (JLC-assembled) instead of solder pins. The PCB version WEA012832F (35.8 x 12.0 mm, 4 pins)
  leaves only 4.7 mm for controls, too little.
- Portrait gives 32 px across: 5 characters of a 6 x 8 font per line, 16 lines, or 2-3
  characters in double size. Status pages become short stacked lines (KNX address, DALI
  count, faults, WiFi), read top to bottom; no rotated text needed.
- Below the OLED, about 10 x 13.8 mm: two ~5 x 5 mm tact switches side by side and the three
  RGB LEDs in a row under them.

#### Shoulders
Shoulder faces at the full hidden depth (44 mm from the rail), so the DALI terminal and the
KNX/AUX plugs sit fully under them with margin; the case depth is no longer set by the
tallest connector as in v1/v2.

#### Open questions to settle before layout
1. Bottom-shoulder KNX plugs (A): confirm plug width/depth and the strip-to-strip spacing
   from the Phoenix drawings, and on a print that the rear (yellow/white) plug can be gripped
   and pulled past the front one.
2. DALI supply: THM 3-2415 (proposed) or REM3.5E; confirm the 10.2 mm height and pin rows
   from the drawing, the remote on/off logic, and the creepage on our board around it.
3. Firmware-driven DALI supply disable on a shorted bus, in addition to the jumper.
4. Machined socket and turned-pin strip part numbers, and the XIAO's retention in them.
5. SMD storage capacitor ripple/ESR against the KNX storage duty.
6. FPC connector and the WEO012832F FPC pinout from its datasheet; button size and
   protrusion through the 1.6 mm front wall.
7. Shelf-to-main headers (pin counts, DALI shelf header rated for the DALI domain), and
   which face of the main board the DIP24 sits on relative to the XIAO and storage capacitor
   (all three are ~9-10.5 mm tall, so they share the one tall side).

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
