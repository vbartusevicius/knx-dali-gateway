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
| Main board (1.6), all parts on its front face (single-sided) | -1.6 |
| **Tallest part on the front of the main board** | **<= 12.2** |

Board area is plentiful (about 76 x 40 mm on a board perpendicular to the rail); thickness
is what decides every part choice below. The main board is single-sided: parts on its back
would cost their height plus clearance from the 12.2 mm, and the THM 3 needs up to 11.0 mm.

#### Packaging study (mechanical/studies/v3_packaging.py)
Boards and the large parts as boxes at datasheet size in the 1-module envelope, checked
against the walls (0.5 mm clearance) and each other, with front/side/top views in
mechanical/build/v3/packaging_*.svg. Result: **fits**, no collisions:
- REM3-2415S/A/CTRL DIP24, 10.9 mm worst case (10.4 + 0.5 tolerance), margin 1.3 mm; long axis along y,
  input end down (AUX), output end up (DALI).
- XIAO on low-profile sockets 8.8 mm, its USB-C end reaching into the front protrusion
  behind the UI board (reachable with the cover off).
- 470 uF SMD 10.5 mm, behind the KNX plugs at the bottom.
- Main board about 77 x 40 mm plus a 34 x 10 mm extension into the protrusion, notched at
  the bottom front for the two plugs; UI board plugged straight onto its front edge with a
  right-angle header (no cable).
- Two Phoenix plugs one behind the other under the bottom shelf; DALI terminal on a small
  board parallel to the front at the top, beside the main board.
Not checked by the study: copper area per block, isolation bands, the rear plug's grip,
thermal.

#### Architecture: carrier + dedicated KNX and DALI boards (user decision)
A single main board did not fit: its three 6 mm isolation bands took about a fifth of the
area and the courtyards filled ~96 % of a single side (see the area budget below). The
boards now split by function, so isolation is mostly distance between boards:
1. **Carrier** (main), vertical along the side wall, 49.9 x 64.4 mm, 1.2 mm, single-sided:
   XIAO on 5 mm sockets (11.0 mm) with the 5 V buck, the XIAO feed (LM66100) and the I2C
   pull-ups underneath; REM3-2415S/A/CTRL with its capacitors and the supply-off optocoupler;
   the DALI current limiter and supply jumpers at the top; FFCs to the UI board, the KNX
   board, and the DALI board's logic and bus sides; a 1x5 right-angle header down into the
   inputs shelf.
2. **KNX board**, horizontal above the inputs shelf (28.4 x 12.5 mm, both sides): crystal
   and NCN5130 at the rail end, L1 beside the FFC to the carrier at the front end, the SMD
   470 uF storage capacitor underneath. Pure SELV-KNX, no isolation band.
3. **DALI board**, parallel to the front (12.5 x 45.7 mm, both sides, extending down behind
   the upper half of the UI board, above the KNX board's L1): WAGO 2601-1102 at the upper
   right, bridge, TX sink, RX threshold and the two optocouplers across a 6 mm band with
   slots, logic FFC at the bottom and bus FFC at the top-back. (Putting all of DALI on the
   carrier overfilled the space beside the REM3; putting the limiter on the DALI board
   overfilled that board. Splitting limiter/jumpers onto the carrier and interface onto the
   DALI board fits both.)
4. **Inputs shelf**, horizontal under the bottom shoulder (29.9 x 13.8 mm): KNX (red/black)
   and AUX (yellow/white) pin strips pointing down, both input protections and the 1x5
   socket on top, 2 mm band between them.
5. **UI board** in the front protrusion (13.8 x 40.5 mm): Winstar WEA012832F mounted
   vertically, two TS-1088 buttons at the bottom corners with the three RGB LEDs in a column
   between them, FFC and the back-side passives behind the display.
All five boards are placed and routed (0 DRC, 0 unrouted, narrowest track 0.15 mm) and pass
the supplier/pinout check on every part; the packaging study fits.

#### Board links: 8-pin 0.5 mm FFC, boards held by the case (user request)
Right-angle headers into sockets took up to half of the small boards (a 2x5 socket is
13 x 5.6 mm and fixes where the board can sit). The logic links are now flat flex:
- **Connector:** Kinghelm KH-FPC0.5-H1.2SMT-8P-QCHF (LCSC C48532769), 8 pins at 0.5 mm,
  flip-top, double-sided contacts (either cable side works), 1.2 mm high, about 8.6 x 4.4
  mm with its latch; JLC places it. Footprint gateway_fp:FPC_Kinghelm_KH-FPC0.5-H1.2SMT-8P,
  drawn from JLC's EasyEDA footprint.
- **Cable:** 8-pin 0.5 mm same-side FFC, 50 mm (e.g. SHOU HAN FFC-0.5-8P-50mm, C6936295),
  hand-fitted; 0.5 A per contact.
- **Links:** carrier-KNX board (KNX_P_PROT, GND, VFILT x2, GND, TXD, RXD, SAVEB),
  carrier-UI board (+3V3, GND, buttons, LED data, +5V, SDA, SCL, GND), carrier-DALI board
  logic (+3V3, GND, TX, GND, RX, GND).
- **Kept as pin headers:** the DALI supply (DALI domain; a 50 V-rated FFC is not the place
  for the line that sees DALI faults) and the inputs shelf (KNX and AUX in one connector
  need the 2 mm gap of the empty middle pin).
- **Mechanics:** with flex links the boards no longer hang on their connectors, so the
  case holds them: the KNX board and the inputs shelf slide into rib slots in the base's
  side walls and are retained by a cover rib; the DALI board slides into slots under the
  top shoulder; the carrier clips to the base side wall; the UI board snaps into the cover
  as on v1/v2. Each FFC gets a fold path and enough slack to open the case.

#### DALI supply: regulated reinforced DIP24 on the main board
The front protrusion does not help a tall converter: it has the same 18 mm width, and width
is the limit everywhere. What helps is a lower part.

| Converter | Size (mm) | Input | Output | Insulation | Verdict |
|---|---|---|---|---|---|
| **RECOM REM3-2415S/A/CTRL** (the v1/v2 converter with the CTRL pin) | **DIP24, 31.8 x 20.3 x 10.4** | **18-36 V regulated** | **15 V 200 mA** | **reinforced, 5 kVAC, 250 VAC** | **chosen: no minimum load, CTRL off at 2.2-12 V / <= 1 mA, same footprint as v1/v2 plus pin 1** |
| Traco THM 3-2415 | DIP24, 31.8 x 20.3 x 10.5 +-0.5 | 18-36 V regulated | 15 V 200 mA | reinforced, 5000 VAC, 2 x MOPP | fits (single-sided); remote pin only on optional models |
| RECOM REM3.5E-2415S/R8/A/CTRL | DIP24, 31.8 x 20.3 x 10.2 (SMD 10.9) | 18-36 V regulated | 15 V, 3.5 W (~230 mA) | reinforced, 250 VAC, >8 mm creepage | fits, but needs a 10 % minimum load (0.33 W wasted) and ~25 mA on CTRL |
| RECOM REM3.5E-2412S/R8/A | DIP24, 31.8 x 20.3 x 10.2 | 18-36 V regulated | 12 V, 3.5 W | reinforced | 12 V is too low: after the limiter's ~1 V the bus sits at ~11 V, below the 12-20 V target, and cable drop eats into DALI's 9.5 V receiver minimum |
| RECOM REM2-1215S | SIP8, 23.0 x 8.0 x 12.2 | 12 V +-10 % unregulated | 15 V, 2 W | reinforced, >9 mm external | needs a 12 V pre-buck and a shelf board; 2 W only |
| Traco TRN 3-2415 | SIP, 11.9 x 7.7 x 11.0 | 18-36 V regulated | 15 V, 3 W | **functional only**, 1600 VDC test, not certified | **not usable**: DALI-SELV needs reinforced insulation |

- REM3-2415S/A/CTRL (RECOM REM3 datasheet): 10.4 mm high, regulated 18-36 V input (takes
  the 20-32 V AUX range directly), 15 V 200 mA, so the v1/v2 limiter (~180 mA) keeps its
  setting; tightly regulated down to no load, so no bleeder resistor and no idle loss
  beyond the converter's own. CTRL referred to -Vin: open or 0-1.2 V = on, 2.2-12 V = off,
  -0.5..1 mA; switched off it draws 2.5 mA (12.5 mW standby). JLCPCB lists it (C6643363)
  without stock and without an EasyEDA footprint: hand-fitted, checked against the cited
  pinout in hardware/product/pinouts.yaml.
- REM3.5E (rejected after review): 3.5 W, but a 10 % minimum load (680R / 1 W, 0.33 W
  wasted all the time) and ~25 mA on CTRL (1 W feed resistor).
- THM 3-2415 (Traco datasheet drawing): 10.5 mm +-0.5 high (the web listing says 10.2),
  inside the 12.2 mm single-sided budget; takes the 20-32 V AUX range directly (no pre-regulator); 3 W keeps the v1/v2
  current limit of about 180 mA, so DALI-2 input devices still have headroom.
- Remote on/off exists only on Traco's optional-pinning models (pin 1, referred to -Vin;
  0-1.2 V or open = on, 2.2-12 V = off); the standard THM 3-2415 has no such pin. The pin is on the input (AUX) side, so the firmware can switch the DALI
  supply off on a shorted bus through a simple level shift in the SELV-AUX domain, no extra
  optocoupler. The jumper stays for "external supply" installations.
- The limiter transistor (DPAK) sits on the main board with real copper for its up to
  ~2.7 W in a bus short (15 V x 0.18 A), until the firmware switches the supply off.

#### DALI terminal: wires from the top, release from the front
In a distribution board the neighbouring devices cover both side faces, so every DIN
device takes its wires through the top/bottom faces and is operated (screw, lever, push
button) from the front of its shoulder. V3 keeps that: the DALI wires enter through the top
face, the clamp is released from the top shoulder's front face, on a small board parallel
to the front (the terminal's pins point backwards, wire entry up, actuator forward), as the
screw terminals do on v1/v2.

Screwless is fine, but not the KF141 family: KF141V/R are 2.54 mm pitch, 150 V, 20-26 AWG
(Kefa/LCSC data). DALI runs next to mains in the same installation and is not SELV, and DALI
pairs are often 1.5 mm2 cores of the mains cable, so the terminal needs a 250-300 V rating and
1.5 mm2. Candidates, actuated from the front:
- WAGO 2601-3102 (lever, Push-in CAGE CLAMP, 3.5 mm pitch, 1.5 mm2): smallest, to check.
- WAGO 2604-3102 (lever, 5.0 mm pitch, 4 mm2, 320 V III/3, 300 V UL; length 12.4 mm): fits
  the 13.8 mm width only with the terminal board spanning the full width (main board
  notched at the top front, as at the bottom).
- Keep the MX126 screw terminal (verified part, already in v1/v2).

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

#### Decisions taken while drawing the V3 circuits (hardware/circuits/variants/v3)
- **OLED: Winstar WEA012832F (0.91" with its own PCB and 4 pins: 1 GND, 2 VIN, 3 SCL,
  4 SDA; 35.8 x 12.0 x 2.41 mm, glass 30 x 11.5 starting 5 mm from the pin end).** The
  bare-glass WEO012832F comes with an I2C FPC only as a 14-pin 0.62 mm hot-bar tail, which
  JLC cannot fit and is hard to hand-solder. With the 35.8 mm module, about 4.7 mm of the UI
  board stays free for controls.
- **Buttons: XUNPU TS-1088-AR02016 (3.9 x 3.0 x 2.0 mm, LCSC C720477)**, pressed through
  printed plungers in the front wall; two of them and the three LEDs share that 4.7 mm row.
- **DALI terminal: WAGO 2601-1102** (lever, horizontal wire entry, 8.5 x 12.75 x 16.58 mm,
  1.5 mm2) on the terminal board parallel to the front: wire entry up through the top face,
  lever pressed through the top shoulder's front. (The 2601-3102 is the vertical-entry
  version, its lever would face the top face instead.)
- **DALI supply: REM3-2415S/A/CTRL** (no minimum load). CTRL is driven through an
  optocoupler (TLP383) from XIAO D1: AUX_P -> 10k -> opto -> CTRL with a 5.6 V zener to -Vin
  (1.4-2.6 mA over 20-32 V), so CTRL sits at ~5.6 V when the opto conducts (= off) and stays
  open (= on) when the gateway is unpowered or the GPIO idles. The opto only bridges two
  SELV grounds (KNX- and AUX-), which must stay separate; the DALI safety barrier is the
  converter itself.
- **Board-to-board:** Kinghelm KH-2.54PH90-1X5P right-angle headers on the main board
  (J5 down into the inputs shelf: KNX+, KNX-, empty, AUX+, AUX-; J6 forward into the
  terminal board: DA1, DA2) and BOOMELE 2.54-1*5P sockets on the shelves. UI on the v1
  10-pin JST cable.
- **Storage capacitor:** Honor Elec RYRVT35V470UF10*10 (C48970703), SMD 10 x 10.2 mm.
- All new JLC parts are in the supplier reference (footprints and symbols); REM3/CTRL,
  WEA012832F and WAGO 2601-1102 have cited pinouts in hardware/product/pinouts.yaml.

#### Area budget of the main board (checked before layout)
Courtyard areas of the V3 main board's parts (from the v1/v2 boards with the same
footprints): 2073 mm2 for everything that is not a 0603/0805/1206 passive, plus 40 such
passives (221 mm2), against 2563 mm2 per side of a 39.8 x 64.4 mm board. Add the two 6 mm
isolation bands (~480 mm2) and the keep-outs of the two shelf headers and a single-sided board
would be ~96 % full even with small parts tucked under the raised XIAO: not routable.
Changes:
- **Done:** the KNX and AUX input protection (SMAJ40CA, SS16, PTC, SS16, SMAJ33A) moved
  to the inputs shelf next to their pin strips; the header carries KNX_P_PROT and AUX_P.
- **Planned:** the main board grows into the front protrusion (+34-41 x 10 mm, about
  +400 mm2) and plugs straight into the UI board with a right-angle header (the JST cable
  pair goes away); passives no taller than 1.0 mm may sit on the back. With a 1.2 mm board
  the tallest front part may then be 13.8 - 1.2 - 1.0 = 11.6 mm: REM3 10.4 (+0.5) keeps
  0.7 mm.

#### Open questions to settle before layout
1. Bottom-shoulder KNX plugs (A): confirm plug width/depth and the strip-to-strip spacing
   from the Phoenix drawings, and on a print that the rear (yellow/white) plug can be gripped
   and pulled past the front one.
2. DALI supply: REM3-2415S/A/CTRL availability and price from a distributor (JLCPCB: no
   stock), and the creepage on our board around it.
3. Firmware-driven DALI supply disable on a shorted bus, in addition to the jumper.
4. Machined socket and turned-pin strip part numbers, and the XIAO's retention in them.
5. SMD storage capacitor ripple/ESR against the KNX storage duty.
6. DALI terminal: WAGO 2601-3102 vs 2604-3102 vs MX126 (dimensions, lever swing through the
   shoulder face, availability).
7. FPC connector and the WEO012832F FPC pinout from its datasheet; button size and
   protrusion through the 1.6 mm front wall.
8. Shelf-to-main headers (pin counts, DALI shelf header rated for the DALI domain), and
   which face of the main board the DIP24 sits on relative to the XIAO and storage capacitor
   (all three are ~9-10.5 mm tall, so they share the one tall side).

## Process
1. Each block's placement is designed deliberately (reference layouts, decoupling at the pins,
   escape routes kept free) and checked with top / bottom / overlay previews before routing.
2. GND and power first, Freerouting for the rest, KiCad DRC including creepage rules.
3. A connection that cannot be routed after one pass is reported, not iterated on blindly;
   the last few may be finished by hand in KiCad.
4. Sub-boards are routed once and reused by the variants that use them.

## Planned v1/v2 upgrade (not done yet)
- Replace REM3-2415S/A with **REM3-2415S/A/CTRL** (same DIP24 footprint plus pin 1) and add
  V3's supply-off circuit (TLP383 + 330R + 10k + 5.6 V zener, XIAO D1 = DALI_SUPPLY_OFF),
  so the firmware can switch the DALI supply off on a shorted bus instead of the limiter
  carrying the short indefinitely. Needs a re-layout around U3 and new releases; the jumper
  JP1 stays for external-supply installations.

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
