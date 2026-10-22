---
name: hardware-review
description: Re-check the v2/v3 hardware before ordering - circuit physics, PCB rules, part heights, exact enclosure fit, stock and prices, datasheet cross-checks - and write a dated findings file
argument-hint: "[v2|v3|all]"
allowed-tools:
  - read
  - grep
  - glob
---
Review the KNX-DALI gateway hardware for manufacturing readiness. Target variant: $ARGUMENTS (default: all, i.e. v2 and v3).

Never tell the user a design is "guaranteed to work". Report what was verified, by which check, and what only a bench test can prove. Past reviews: `hardware/docs/review-*.md`; rework for ordered hardware: `hardware/docs/rework-*.md`.

## 1. Regenerate and run every automated check

The tools run only in Docker (project rule). For each variant `<v>`:

```sh
python3 hardware/scripts/build.py <v> --eda      # netlists, layout sync, routing check, renders
python3 hardware/scripts/build.py <v> --cad      # enclosure, fit drawings, legend, 3D renders
python3 hardware/scripts/build.py <v> --test     # EDA + CAD pytest suites
```

What the suites cover, so you know what not to redo by hand:
- `hardware/tests/test_circuit_physics.py`: values from the netlist against datasheet limits. DALI TX sinks 250 mA below 4.5 V, its clamp stays above the internal supply limit, gate drive holds through a 1.3 ms collision break, gate turn-off fits the 400-433 us half-bit window, quiescent bus current, RX pull-up vs opto CTR, SCT2400 EN start/stop vs KNX bus minimum and NCN5130 reset, KNX power budget.
- `test_power_logic.py`, `test_consistency.py`, `test_isolation.py`, `test_drc_*.py`, `test_boards_routed.py`: topology, pin maps, creepage, DRC = 0, unrouted = 0.
- `test_u1_routing.py`: NCN5130 datasheet layout rules (strict xfail on v3 rev C, see the review file).
- `test_jlc_assembly.py`: every footprint pin-for-pin against JLCPCB/EasyEDA or `hardware/product/pinouts.yaml`.
- `mechanical/tests/test_fit.py`: layout heights >= datasheet maxima in `hardware/product/package_heights.yaml`, part air, openings, cable reach.
- `mechanical/tests/test_enclosure_solids.py`: exact boolean intersections of case vs boards, part bodies (Fab outlines), solder tails, vertical board backing, button plungers.
- `mechanical/tests/test_enclosure_probe.py`, `test_ui_stackup.py`, `test_board_stack.py`, `test_front_legend.py`.

Any failure is a finding. Do not weaken a test to pass. If a test is wrong, show why with the datasheet, then fix it.

## 2. Live stock and price

```sh
docker compose --profile hardware run --rm -T -e HARDWARE_VARIANT=<v> eda "python3 hardware/scripts/jlc_parts.py stock --boards 5"
```

Report parts short for the planned quantity, low-stock parts (under 100), the per-unit cost of the NCN5130, REC3, XIAO and connectors, and the count of `expand` (extended) parts: each adds JLCPCB's feeder fee per order. Hand-fitted parts (REC3, XIAO, PST strips, OLED, FFC) are not on JLCPCB: check Mouser/Digi-Key/TME stock with a web search against `hardware/circuits/variants/<v>/assembly_parts.yaml`.

## 3. Datasheet cross-check (what tests cannot know)

For every part that was added or changed since the last review (`git log -p hardware/circuits hardware/product`), and for anything a test depends on as a constant:
- Fetch the PDF. Manufacturer sites often block curl; try Farnell/LCSC/Mouser mirrors. Extract the text in a throwaway container:
  `docker run --rm -v /tmp/ds:/ds python:3.12-slim sh -c "pip install -q pypdf==5.1.0 && python -c '...'"`
- Check pinout vs symbol pin names, absolute maximum ratings vs the worst case (KNX 33 V + TVS clamp, AUX 30 V MDT STC-1280.01, DALI 22.5 V), package maximum height, THT lead length, and isolation class (DALI vs SELV).
- Add new figures to `hardware/product/package_heights.yaml` (heights, tails) or as named constants with their source in `test_circuit_physics.py`, never only in prose.

## 4. Physics questions to answer explicitly

- DALI: who limits the bus current in each mode (internal supply via JP1 / external 250 mA supply)? Does every transmitter path pull below 4.5 V? Edge delays through each optocoupler vs firmware timing (`gateway/src/dali/bus/Timing.hpp`)? Dissipation of the pass transistor during a sustained short, and what switches it off when the MCU is unpowered?
- KNX: VFILT at a 21 V bus with 40 mA, buck UVLO thresholds, energy for WiFi bursts on 470 uF, SAVEB warning time.
- Sensors and inputs: ADC ladder voltages and ESP32-C6 ADC range, NCN_SAVEB level when KNX is off, DALI_RX idle level with and without bus power, I2C pull-ups vs OLED, LED data level (3.3 V into the 5 V AHCT buffer).
- Thermal: hottest parts in the closed case (Q1, REC3, NCN5130, buck) at worst-case load.

## 5. Mechanics by hand

Open `mechanical/build/<v>/fit_front.svg` / `fit_side.svg` and the renders. Check what the solid tests model only as boxes:
- Connector mating: plug bodies, insertion direction, access with neighbours on the rail, wire release.
- Cable paths: FFC Type A/B and pin 1 mapping through the folds, cable lengths.
- Assembly order: can every part be inserted, with snaps, ribs and channels swept by the cover's sliding direction?
- Print orientation and thin features: flexure tabs, snap tongues, skins under 0.8 mm.

## 6. Write it down

Write `hardware/docs/review-<YYYY-MM-DD>.md` in the format of the previous review: tables of findings with evidence (test name or datasheet page) and a status (fixed / rework / open / accepted), plus the bench items. If boards or prints were already ordered, write or extend `hardware/docs/rework-<revision>.md` with exact reference designators, values, LCSC numbers, grinding coordinates and the expected bring-up measurements. Add a short pointer under "Pre-manufacturing review findings" in AGENTS.md.
