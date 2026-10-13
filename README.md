# KNX–DALI Gateway

Open-source DIN-rail gateway between a wired KNX TP line and one DALI line
(up to 64 control gear, DALI-2 input devices), configured with ETS and powered
from the KNX bus. WiFi dashboard/MQTT are optional extras; lighting never
depends on them.

**Status:** prototype. Nothing has run on real hardware yet; no KNX or DALI
certification is claimed.

![Gateway](hardware/releases/v1/enclosure/renders/iso.png)

## What is in the repo

| Folder | Contents |
| --- | --- |
| `gateway/` | ESP32-C6 firmware (C++17, ESP-IDF/PlatformIO), native unit tests |
| `knxprod/` | ETS product database source and generator |
| `simulation/` | DALI bus simulator and the Docker integration lab |
| `hardware/` | Circuits (SKiDL), routed KiCad boards, part libraries, `releases/` |
| `mechanical/` | Parametric 3D-printed DIN enclosure and its fit checks |
| `docker/`, `compose.yaml` | One container per tool; nothing but Docker and PlatformIO on the host |
| `.github/workflows/` | Lint, board/enclosure verification, .knxprod build, integration lab |

| Hardware | Width | Boards | Release |
| --- | --- | --- | --- |
| v1 | 3 DIN | 4-layer main (NCN5130 KNX, isolated DALI + supply, XIAO ESP32-C6), 2-layer UI | [`hardware/releases/v1/`](hardware/releases/v1/README.md) |
| v2 | 2 DIN | 4-layer main (KNX, DALI supply, terminals), interface (isolated DALI, plugged onto main), UI (XIAO, Winstar OLED) | [`hardware/releases/v2/`](hardware/releases/v2/README.md) |

Both have a 0.96" OLED, two buttons and three RGB status LEDs on the front.

## Ordering a kit

Everything is in `hardware/releases/<version>/`; its README has the part
details, assembly and first power-up steps.

1. **PCBs and SMT assembly (JLCPCB)**, per board folder (v1: `main/`, `ui/`;
   v2: `main/`, `interface/`, `ui/`):
   - Gerbers: `<board>_gerbers.zip` (1.6 mm; layer count in the release README).
   - Assembly (both sides): `<board>_bom_jlc.csv` + `<board>_cpl_jlc.csv`.
   - `<board>_assembly_check.csv` shows each part was checked against JLCPCB's
     own footprint; in JLC's placement preview glance at the bottom-side parts.
2. **Parts JLCPCB does not fit**: `<board>_hand_fit.csv` (KNX pin strip,
   RECOM REM3 converter, XIAO ESP32-C6, OLED module; v2 also the lying-down
   storage capacitor) and `assembly_parts.csv` (JST-SH cable, KNX bus plug,
   XIAO sockets, jumper caps, two 2.5 x 10 mm thread-forming screws).
3. **Enclosure**: print `enclosure/base.stl` and `enclosure/cover.stl` in
   PETG or ASA, light/translucent so the status LEDs show through the front.
4. **Front legend (optional)**: `enclosure/front_legend.pdf` is a 1:1
   print file (terminal names and polarity, buttons, LED rings) for UV/pad
   printing on the cover or an adhesive label; cyan lines are guides only.

## Working on it

| Task | Command |
| --- | --- |
| Firmware tests / build | `pio test -d gateway -e native`, `pio run -d gateway -e dev_c6` |
| Integration lab | `simulation/run-tests.sh` |
| ETS product (.knxprod, ETS 5.7 + 6) | `ETS_CV_DIR=knxprod/ETS knxprod/scripts/package.sh` ([how](knxprod/docs/knxprod-pipeline.md)) |
| Hardware and enclosure checks | see `.github/workflows/hardware.yml` (eda and cad containers) |
| New hardware release | `docker compose --profile hardware run --rm -T [-e HARDWARE_VARIANT=v2] eda "python3 hardware/scripts/release.py"` |
| Lint | `docker compose --profile lint run --rm lint check .`, `... cpp-lint gateway/scripts/check_cpp.sh` |
| Board / STL viewers | `docker compose --profile viewers up` (:8082 KiCanvas, :8081 STL) |

Sources are versioned; `hardware/build/` and `mechanical/build/` are local
build output. Routing (`layout.py <board> route`) is a developer step; CI
proves the committed boards are complete, DRC-clean and match the release.

Licences per folder: [`LICENSE.md`](LICENSE.md). Engineering rules and
decisions: [`AGENTS.md`](AGENTS.md).
