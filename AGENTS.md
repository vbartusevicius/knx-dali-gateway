# KNX-DALI Gateway: agent instructions

DIN-rail gateway between a wired KNX TP line and one DALI / DALI-2 line, commissioned through ETS. The repository holds the ESP32-C6 firmware, the ETS product, a Docker KNX/DALI lab, SKiDL/KiCad boards for two hardware variants and CadQuery enclosures. User-facing overview: `README.md`. Background research and the original plan: `docs/research-notes.md` (history, not instructions).

Nothing has run on physical hardware yet. Never claim bench, ETS-import, electrical, EMC or certification validation from host, container or DRC results; say what was actually verified.

## Repository map

| Path | What lives there |
| --- | --- |
| `gateway/` | C++17 firmware (ESP-IDF via PlatformIO), native Unity tests, web UI, scripts |
| `knxprod/` | ETS application XML (source of truth), commissioning script, `.knxprod` pipeline (`knxprod/docs/knxprod-pipeline.md`) |
| `simulation/` | Docker integration lab: python-dali simulator, xknx ETS-like programmer, pytest scenarios |
| `hardware/` | SKiDL circuits (`circuits/blocks`, `circuits/variants/<v>`), committed routed boards (`layout/<v>/<board>`), scripts, tests, frozen `releases/<v>/`, docs (`docs/variants.md`, reviews, rework guides) |
| `mechanical/` | CadQuery enclosure, fit drawings, legend, 3D renders, tests |
| `docker/` | One folder per image; all built by the root `compose.yaml` |
| `scripts/` | Shared GitHub Release packager (`package_release.py`) and unit tests |
| `.claude/skills/hardware-review/` | Project skill: re-check the hardware before any order |

Component rules: each component owns its source, tests and build config; no root-level `src/`/`test/`, no shared `common`/`utils` dumping ground. `hardware/build/`, `mechanical/build/`, `knxprod/build/`, `dist/` are generated and ignored.

## Commands

All non-PlatformIO tools run in Docker through `compose.yaml` profiles; never install KiCad, .NET, Wine, CadQuery etc. on the host. The `eda`/`cad` images use `bash -c`, so pass one quoted string: `docker compose --profile hardware run --rm -T eda "<cmd>"`.

| Task | Command (repo root) |
| --- | --- |
| Firmware unit tests | `pio test -d gateway -e native` and `-e native_sanitized` |
| Firmware build (`firmware.bin`, `firmware-full-usb.bin`) | `pio run -d gateway -e dev_c6 -e release_c6` |
| clangd compile databases | `pio run -d gateway -e dev_c6 -e native -t compiledb`, then restart clangd |
| Integration lab | `simulation/run-tests.sh` |
| ETS header regenerate / staleness check | `knxprod/scripts/generate.sh` / `knxprod/scripts/check.sh` |
| ETS package | `ETS_CV_DIR=knxprod/ETS knxprod/scripts/package.sh` |
| Python lint (ruff) | `docker compose --profile lint run --rm lint check .` |
| C++ format + clang-tidy | `docker compose --profile lint run --rm cpp-lint gateway/scripts/check_cpp.sh [--fix]` |
| Whole hardware + mechanical pipeline | `python3 hardware/scripts/build.py [v2\|v3] [--eda\|--cad\|--release\|--test] [--boards main ui] [--fresh]` |
| GitHub release assets + `SHA256SUMS.txt` | `python3 scripts/package_release.py --clean --notes-out dist/release-notes.md` |
| Viewers (KiCanvas :8082, STL :8081) | `docker compose --profile viewers up` |

PlatformIO lives in the IDE-managed `~/.platformio/penv`; it may not be on the shell `PATH`.

### Hardware pipeline steps (inside `eda`; `HARDWARE_VARIANT=v2|v3`, default v3)

1. `python3 hardware/scripts/gen_circuits.py`: SKiDL → `build/<v>/<board>/{<board>.net, parts.json}` and the generated `assembly_parts.yaml`.
2. `python3 hardware/scripts/layout.py <board> sync | place <refs> | autoplace [--write] [--all] [<refs>] | unroute | clean-tracks | tidy-silk | components`. The committed `hardware/layout/<v>/<board>/<board>.kicad_pcb` is the source of truth; `sync` keeps positions and copper by ref/net and rebuilds the frame (outline, slots, keep-outs, pours, rules, net classes) from `circuits/variants/<v>/layout_<board>.yaml`.
3. `python3 hardware/scripts/layout.py <board> route [attempts]`: DRC baseline of the unrouted board → DSN export → fastroute → SES import and clean-up → report (routed/total from pcbnew connectivity, tracks, vias, DRC violations not in the baseline). fastroute is not repeatable run to run, so `route` imports up to `ROUTE_ATTEMPTS` (default 4) sessions (`FASTROUTE_MAX_TIME` s each, default 600) and keeps the board with the fewest DRC errors + open connections, stopping at a clean one. `kicad-cli pcb drc` on the imported board is the only pass/fail (0 errors, 0 unrouted; silkscreen/library warnings are reported, not failing). DRC's `unconnected_items` list is capped, so count unrouted from connectivity.
4. `python3 hardware/scripts/render_board.py <board> <label> [--placement]`: DRC + SVG/PNG views in `build/<v>/<board>/render/<label>/` (red = violations/unrouted).
5. `python3 hardware/scripts/release.py <version> [<board>...]`: refuses boards with DRC errors, unrouted items or failed part checks. Name boards to re-release only those.
6. `cad`: `python3 mechanical/scripts/build_enclosure.py && python3 mechanical/scripts/render_assembly3d.py && (cd mechanical/scripts && python3 fit_drawing.py)`, then rerun `release.py`. After a release that changes renders, refresh README images with `python3 mechanical/scripts/readme_images.py`.

Tests: `eda` → `python3 -m pytest hardware/tests mechanical/tests/test_fit.py mechanical/tests/test_ui_stackup.py mechanical/tests/test_board_stack.py --ignore=hardware/tests/test_step_geometry.py -q -p no:cacheprovider`; `cad` → `python3 -m pytest mechanical/tests/test_enclosure_probe.py hardware/tests/test_step_geometry.py`.

### CI (`.github/workflows`)

`lint` (ruff, C++), `hardware` (regenerates circuits and fails on `git diff`, `layout.py components`, hardware + fit tests including `test_boards_routed`: DRC 0 errors / 0 unrouted on committed boards, enclosure build, renders as artifact; routing itself is a developer step), `lab`, `knxprod`, `release` (builds firmware + signed `.knxprod`, stages all release files through `scripts/package_release.py`, verifies `SHA256SUMS.txt` and publishes atomically with a `| File | Use |` table). Keep every change green there.

### Release assets

GitHub Releases are packaged solely by `scripts/package_release.py` (used by `.github/workflows/release.yml` and manual runs):
- `knx-dali-gateway-<variant>-hardware.zip` (`v2`, `v3`): per-variant archive with `boards/<board>/`, `enclosure/`, root BOM/order CSVs and `README.md` (plus `rework/` on `v3`), strictly scoped to that variant's boards and excluding `.DS_Store`/scripts/clutter.
- `firmware-full-usb.bin` and `firmware.bin`: the two direct-flash ESP32-C6 images shared by V2 and V3 (no loose `.elf`/`.map`/`.json`/`.svg` files).
- `KnxDaliGateway.knxprod`: ETS 5.7 / 6 product database.
- `SHA256SUMS.txt`: SHA-256 checksums covering every release file above.
- Release body starts with the generated `| File | Use |` Markdown table; never attach helper scripts, simulator binaries or ungrouped discipline archives.

## Engineering rules

### Modularity and simplicity

- SOLID where it improves real cohesion, substitution or testing, together with KISS and YAGNI. Prefer composition and explicit collaborators; no interfaces, factories, frameworks, service locators or DI containers for hypothetical needs. Dependency injection is manual constructor injection with composition roots in `gateway/src/firmware/Application.cpp` and `gateway/src/lab/main.cpp`.
- Focused classes and files with small APIs; free functions for stateless operations. Entry points, ISRs and transport callbacks stay thin; business rules live in focused modules, never in giant mains or manager classes.
- Domain logic stays independent of GPIO, ESP-IDF, networking and storage behind the few meaningful seams. Firmware, native tests and the lab run the same core; never maintain a simulator-only copy of gateway logic.
- Extract shared packages only for genuine cross-component reuse. Generated ETS/firmware metadata is a versioned contract, never hand-copied constants.

### C++

- Idiomatic C++17 for the pinned toolchain, project namespaces mirroring folders (`gateway::dali`, `gateway::lighting`, ...), const-correctness, scoped enums, named constants with units.
- RAII and explicit ownership; no owning `new`/`delete`, no needless `shared_ptr`. Bounded buffers and queues; no allocation, blocking or networking in interrupt or timing-critical paths. No exceptions/RTTI.
- Headers expose only the needed API; nontrivial non-template code goes in `.cpp`. Clear result/state types instead of sentinels, implicit conversions or unchecked casts.
- `gateway/src` layout: headers next to sources, one folder per module, includes relative to `src` (`"dali/ArcLevel.hpp"`). `src/espidf` holds SDK adapters that must also compile natively against `tests/support/sdk_fakes`; `src/firmware` (boot, wiring, console, KNX device, networking) is the only folder excluded from native builds. `src/lab` is the Linux lab entry point.
- Board pins are constexpr facts in `gateway/src/firmware/Board.hpp` (one XIAO pin map for v2 and v3), not settings.
- `src/CMakeLists.txt` globs `*.cpp`; after adding a source file touch it or clean. `sdkconfig.defaults` is the committed SDK config (menuconfig edits are lost on clean). Project sources build with `-Werror`.
- Style is enforced by `gateway/.clang-format` and `gateway/.clang-tidy` through `cpp-lint`.
- clangd: `gateway/scripts/compiledb.py` writes `.pio/build/<env>/clangd/compile_commands.json` (a separate folder because ESP-IDF overwrites the default one); `gateway/.clangd` uses `dev_c6` for `src/` and `native` for `tests/`. Changing `sdkconfig.defaults` wipes the build folder, so rerun `compiledb` when every header shows as unresolved.
- Lab (`simulation/`): PlatformIO env `lab` builds the same application on thelsing's Linux platform (KNX IP mask 0x57B0 over routing multicast) with `SocketDaliTransport` to the python-dali simulator; xknx plays ETS. It does not exercise TP-UART timing or the RMT driver. Never point lab or commissioning tests at a live KNX installation.

### Dependencies

- Look for a maintained package before writing infrastructure or protocol code; check features, MCU/SDK compatibility, maintenance, licence, resource cost and testability against the exact version. Write custom code only for domain behaviour or demonstrated gaps, and test it independently.
- Pin versions/commits and hashes; prefer releases at least 7 days old; never weaken security or release-age policies to make a dependency work. Preserve licence notices.

### Comments and tests

- Code explains itself. Comment only critically non-obvious reasons (hardware erratum, protocol timing, unavoidable workaround), concisely. Never strip licence notices.
- Use established frameworks (Unity, pytest). Tests live with their component; cross-component scenarios go in `simulation/`.
- Write a failing regression test before a fix where practical. Cover boundaries, malformed traffic, timeouts, concurrency, readback/fallback and recovery. Fakes exercise production code, never re-implement it.
- Before finishing, review for god classes, deep nesting, duplication, unbounded work, hidden globals, ambiguous ownership, magic values and layer leaks. Refactor only where it improves the current design.
- Do not weaken a guard test to make a change pass. Strict-xfail markers in `test_circuit_physics.py` pin known open findings; remove a marker only when its fix lands.

## Product requirements (user-confirmed)

- Wired KNX TP to DALI gateway. KNX/IP over WiFi is not the architecture.
- All lighting control, startup and ETS provisioning work with WiFi disabled, unconfigured or failing. No cloud, DHCP or broker dependency.
- ETS is the primary provisioning tool and owns the configuration: physical address, application download, associations, DALI discovery/addressing, groups, scenes and replacement work through ETS without WiFi. Dashboard changes go through the shared commissioning service with explicit revision/sync checks, never silently overriding ETS.
- WiFi dashboard and MQTT are continuously available when configured (not maintenance windows), but their failure or load must never starve or reboot KNX/DALI. KNX-only power must still sustain modem-sleep WiFi; continuous WiFi is carried by AUX.
- One serialized DALI commissioning service for ETS and network clients; concurrent operations are rejected ("busy") or coordinated.
- Replacement preserves logical KNX channels and associations, restores intended DALI settings and reports mismatches.
- Recovery: valid ballast readback (A) wins over persisted target (B). Fall back to B only for a reachable device/field without reliable readback, and report the source. Timeouts, startup or an offline bus are not proof that readback is unsupported. Never replay relative commands. Startup recovery never readdresses gear or changes associations.
- Physical RESET distinguishes reboot (keeps configuration), factory reset, KNX reset and DALI address reset. DALI address reset only on explicit request (ETS function property or confirmed console command).
- Power: AUX input is the primary core supply via an isolated converter (SELV-AUX → SELV-KNX) and a priority path, with the KNX bus buck as seamless fallback. The gateway must work and be programmable from KNX red/black alone. The NCN5130 stays bus-powered.
- One DALI line: up to 64 control gear plus DALI-2 input devices (multi-master: 8-bit replies, 16-bit gear, 24-bit device frames, collisions/backoff, separate address spaces, events). Do not assume the gateway is the only transmitter.
- Full IEC 62386-209 DT8 is the target (Tc, xy, RGBWAF, primary-N, queries, limits, colour scenes), driven by per-gear capability detection. Never imply every gear supports every colour type.
- Exactly one two-pole AUX DC input (24 V PSU or the KNX white/yellow pair, e.g. MDT STC-1280.01 at 30 V, one source at a time). It feeds the optional internal DALI supply (switchable off, `JP1` disconnects it for an external DALI PSU) and the core converter.
- USB (on the XIAO, may be inside the case) is the only service interface: flashing, console, recovery, with external wiring disconnected. Live debugging needs a reviewed isolated method. No dedicated UART/JTAG connectors.
- DIN enclosure within 1–2 modules (v3 = 1 DIN, v2 = 2 DIN), custom 3D-printed. Never silently exceed 2 modules.
- DALI is not assumed SELV: keep the 5.5 mm reinforced barrier between DALI and KNX/AUX/accessible logic; SELV-AUX | SELV-KNX is 1 mm functional. No onboard 230 V.
- Parts must be hand-solderable with an iron: passives 0805+ (only exception: v3's three 0603 NCN5130 bus caps), leaded parts wherever one exists (the NCN5130 QFN and LM66100 SC-70 are accepted).
- Personal DIY with possible small-scale sales: open-source everything project-owned; audit licences and market requirements before selling; never claim KNX/DALI certification.

## Firmware behaviour to preserve

- Concurrency: DALI transactions block for tens of ms and never run in the KNX loop. The KNX task (1 ms loop, `CONFIG_FREERTOS_HZ=1000`) turns group-object writes into commands on a bounded queue; one DALI worker is the only `LightingService` caller and returns status updates on a second bounded queue. Console, web and MQTT submit through the same queue. Task watchdog (30 s, panic) covers the KNX task and DALI worker. Network code must not block either.
- DALI bus layer is our own (`src/dali/bus`, `src/espidf/RmtDaliBus`): Manchester coding, priority settling, echo collision detection, backward-frame window, unsolicited frames as events. Timing constants in `src/dali/bus/Timing.hpp`; collision retries live in the link layer and `RetryingTransport` retries only timeouts/invalid replies, so retries never multiply. RMT timing is unverified without an oscilloscope.
- Persistence: NVS `targets_v1` (last target per short address = fallback B; last non-zero level per channel/group/broadcast), one commit per operation, failed commits retried. Factory reset erases `eeprom` (KNX stack), `targets_v1`, `net_cfg`, `diag_v1`; it never touches DALI gear addresses.
- Mapping: DPT 5.001 0 = off, 1..255 linear to arc 1..254 (MASK 255 is never a level). Group status derives from member readback, never from one group query.
- Buttons (ADC ladder on D2): left SCROLL tap = next OLED page, hold 5 s = WiFi setup portal; right RESET tap = reboot, hold 5 s = KNX programming mode; both 10 s = factory reset.
- Images: 4 MB flash, two OTA slots, no `storage` partition; the web UI is compiled into the app (`pack_webui.py` → `WebAssets.hpp`), so `firmware.bin` OTA updates both. `merge_image.py` writes `firmware-full-usb.bin` for `0x0`.
- The `thelsing/knx` stack makes distributed firmware GPL-3.0-or-later.

## ETS product (`knxprod/`)

- `knxprod/src/*.xml` is authoritative; the firmware header `gateway/src/knx/generated/KnxDaliGateway.h` is generated (`generate.sh`) and `src/knx/ObjectMap` / `src/config/Configuration` static_assert against it. Never hand-edit generated output.
- Bump `ApplicationVersion` in `knxprod/src/KnxDaliGateway.xml` whenever the parameter/object layout changes or the user re-imports for testing (ETS ignores same-version re-imports), then regenerate.
- Never use "OpenKNX" in a product, catalog, hardware or application name; `prepare_xml.py` renames and `verify_knxprod.py` rejects it.
- Product IDs (M-00FA, application 90) are provisional and a release gate.
- ETS DLLs, signing keys and the decrypted CV bundle never enter git, images or logs; signing runs under Wine with the user's ETS mounted read-only. `verify_knxprod.py` is structural only; real ETS 5.7/6 import is still outstanding.
- ETS online-button API names in `DaliGateway.script.js` are unverified until tested in real ETS.

## Hardware rules (`hardware/`)

- Variants `v2` (2 DIN, boards main/interface/ui) and `v3` (1 DIN, boards main/ui) share circuit blocks, the XIAO pin map and firmware. Design facts: `hardware/docs/variants.md`; open findings: the latest `hardware/docs/review-*.md`. Invoke the `hardware-review` skill before anything is ordered.
- Generated files: `hardware/circuits/variants/<v>/assembly_parts.yaml` (change `gen_circuits.py`, never the YAML); `jlc_reference.json` is refreshed with `jlc_parts.py snapshot` and reviewed. Release CSVs use CRLF: edit byte-preserving.
- New parts get fixed new references so existing designators never renumber.
- Every part is verified: `jlc_parts.check_board` fits our footprint against JLCPCB/EasyEDA's (or `footprint_ref`) and compares pin names; parts without a JLCPCB record need a cited pinout in `hardware/product/pinouts.yaml`; differing pad numbering only via cited entries in `jlc_pad_maps.yaml`. SKiDL pin names must map to footprint pads (generic `Q_*_BCE`, TVS/LED pin orders have been wrong before).
- Release part checks run live and fail closed; CI uses the offline reference. JLCPCB/EasyEDA APIs return 403 when hit fast: keep the throttling, retries and cache.
- Isolation lives in the spec `isolation`, layout `copper_keepout`/`domain_regions` and `test_isolation.py`. KiCad DRC rules on `Reference`/`memberOfFootprint` never match: where an isolator's own pins are too close, add a board slot (frame `slots`).
- One invalid rule makes KiCad silently ignore the whole `.kicad_dru`; `test_drc_rules` plants a violation to prove the rules run.
- Independent verification gates (zero imports from `hardware/circuits/`, operating strictly on the final routed `.kicad_pcb` and exported `<board>_bom.csv`):
  - `test_independent_pinout.py`: mounting-face shoelace chirality on `F.Cu`/`B.Cu` (catches footprints mirrored in `.kicad_mod` or by `Flip()`), standalone datasheet-audited MPN pin-role table, non-swapped UART/I2C/LED chains, zero open/shorted 2-pin passives, DC diode/cap bias and physical silkscreen cathode/`+` mark proximity.
  - `test_copper_spice.py`: reconstructs nodes via spatial `STRtree` Union-Find over physical pads, tracks, vias and filled zone islands (with copper `R`/`C` parasitics and 3D `(X, Y)` mating-pad stitching for stacked B2B headers), then drives `libngspice` (`ctypes`, `.options rshunt=1e11`, smooth `C^inf` `tanh`/`spos` models) across `.op`, `.dc`, `.ac`, `.noise` (`noise1`/`noise2`) and `.tran`.
- Track ends must sit on a track end/centreline, inside a pad/via, or inside a filled zone of their net; grazing ends, zero-length segments, collinear overlaps, unsplit interior crossings and same-net planar loops fail `test_track_joints.py` (KiCad DRC ignores same-net shorted loops). `import_ses` and `layout.py <board> clean-tracks` run `polish_tracks` (`clean_planar_tracks` + `remove_dead_ends` + `seat_track_ends`).
- Layout/routing lessons (do not undo):
  - KiCad 10's DSN export puts all nets in the default class; `layout.with_net_classes` rewrites them from the `.kicad_pro` with a `+3 um` clearance rounding margin (and overrides `wire_smd`/`smd_smd` back to `base + 3 um` for any class with extra wire-to-wire spacing so traces still escape fine-pitch pads). If `.tune` length-matching is ever used, restrict it strictly to 2-pin point-to-point branches, never 3+-pin nets.
  - DSN input (`export_dsn`, audited 2026-11-16 by importing raw sessions; do not undo): pours are left out (a router counts anything inside a plane outline as connected); rule areas that forbid neither tracks nor vias (case contacts) are left out (the DSN would make them full keep-outs); plane layers go in as `(type power)` with their shape removed from THT pin padstacks (fastroute refuses pin connections on a power layer without their plane: "some of their layers are disabled") and kept in the via padstack, so sessions import through vias, not buried ones; back-side images get `<padstack>_back` copies without the *mirrored* layer instead (the router mirrors layers of back-side parts; dropping In1 there removed their In2 copper and let In2 tracks short through C4 and the XIAO); board cutouts (isolation slots) are grown by the edge clearance like the outline. Passing the In1 pours as DSN planes left GND/GNDA/GNDD connections blocked; a board-wide In1 `wire_keepout` stopped all vias.
  - KiCad 10 SWIG: a `PCB_TRACK` removed from a board must stay referenced for the whole process (`layout._DETACHED` / `track_joints._DETACHED`), or later proxies degrade and the process segfaults. `ZONE.SetDoNotAllowCopperPour()` no longer exists; use `SetDoNotAllowZoneFills()`.
  - `track_joints` compares tracks by `m_Uuid` (pcbnew wrappers are new per call).
  - Placement & routing: `resolve_overlaps.py <board> [--write] [--all] [<ref-or-block>...]` (`layout.py <board> autoplace`) runs a deterministic 2-stage Google OR-Tools CP-SAT placer on a `0.05 mm` grid (`AddNoOverlap2D` per side + 3D height + cross-face THT keep-outs; Stage 1 solves unlocked ICs/inductors/transistors, Stage 2 solves batched passives with `0°/90°/180°/270°` pad anchoring and down-weighted plane nets). Check `test_u1_routing.py` (NCN5130 CCP loop, decoupling distance, VSW1 ≤ 10 mm) and DRC before re-running the router blindly.
- v3 USB: the XIAO's USB-C faces the rail-side edge; `test_usb_access.py` guards the plug path.
- v3 main U1 bus caps (2026-11-16): C3 sits at (4.8, 20.3), fanned out from (5.6, 20.8) so CEQ1/CEQ2 have a corridor past C1 (CCP loop 4.2 mm of 5 mm, so C1 stays). J4 pins 1/5 (+3V3/+5V) have full-width `pad_escapes` stubs.

## Mechanical rules (`mechanical/`)

- Enclosures are built from the board exports (`components_<board>.json`, STEP) and checked with exact solids: `test_fit.py`, `test_enclosure_solids.py`, `test_board_stack.py`, `test_print_rules.py`, `test_front_legend.py`. Do not weaken them; heights come from `hardware/product/package_heights.yaml`.
- Print rules learnt from the first printed case: gaps ≥ `MIN_PRINT_GAP` (0.6 mm), latch strain ≤ 2.5 %, legend letters ≥ 3 mm and 0.6 mm deep, every printed part one solid, ≥ 2 mm air between parts and case (spec `checks.component_air_gap`).
- Fastening never requires PCB changes. v3 has no front text; v3 connector names are engraved on the base's right side.
- Board views and fit drawings are SVG only; 3D renders come from `render_assembly3d.py`.

## Licensing

One root `LICENSE.md` maps folders to SPDX licences (gateway GPL-3.0-or-later; knxprod, simulation, docker MIT; hardware, mechanical CERN-OHL-P-2.0; docs CC-BY-4.0) and lists third-party notices. No per-file SPDX headers, no per-folder licence files, no REUSE tooling. Never incorporate the upstream OpenKNX REG1 hardware (CC-BY-NC-SA) into sold units.

## Docker and tooling

- `eda`: KiCad 10.0.6 (`kicad/kicad:10.0`, amd64 under emulation on arm64), SKiDL, kinet2pcb (quoted `fp-lib-table` workaround in `docker/eda/fix_fp_lib_table.py`), OR-Tools (`cp_model`), `libngspice0`, fastroute (release binary, SHA-256 pinned). `mcp` builds FROM `eda` and adds Java 25 + Freerouting 2.5.0 for KiCAD-MCP-Server's autoroute tool. Pin every stage to `linux/amd64` (an unpinned stage on arm64 copies arm64 binaries into the amd64 image).
- `knx-dali-gateway-eda-native:kicad10.0.8` (`docker/eda-native/Dockerfile`, built with `docker build --platform linux/arm64 -f docker/eda-native/Dockerfile -t knx-dali-gateway-eda-native:kicad10.0.8 .`, run with `docker run --rm -v "$PWD":/work <tag> "<cmd>"`): native arm64 Ubuntu 24.04 + KiCad 10.0.8 PPA + fastroute linux-arm64; not in compose. On v3 main pcbnew/Gerbers run about 3x and fastroute 1.4x faster than the emulated `eda`, but `kicad-cli pcb drc` ran slower (2.5 s vs 1.7 s). No kicad-packages3d: stock 3D models are missing for STEP exports. Releases still come from `eda`.
- Router: fastroute v0.1.14 (`parisxmas/fastroute`, GPL-3.0 Rust port of Freerouting, build tool only, not distributed). Freerouting's CLI (`-de X.dsn -do X.ses`) plus `--max-time`, `--multi-start`, `--report=FILE --diagnose` (classifies each unrouted connection as congestion or blocked). Exit 0 even with unrouted connections; the session always holds the best board so far. `route` passes `--router.fanout.enabled=false` (explicit locked ties/bridges/escapes handle fine-pitch pads), `--router.min_trace_width_um=<frame min_track>` (the DSN has no board minimum; otherwise it necks to 0.1 mm) and `--router.neck_width_um=220` (a connection that fails at class width is retried whole at that width; at 0.12 mm an 8 mm +5V run came out). Do not set the minimum to 220 (fewer connections routed) or put the power classes in `--no-neckdown-classes` (J4/U9 power pins blocked). traxide (`h3ll5ur7er/traxide`) was tried on 2026-11-16 and left three U1 nets of v3 main unrouted in every profile.
- KiCad MCP servers (`docker compose --profile mcp build mcp`, registered in the gitignored `.claude/mcp_config.local.json`, start the agent from the repo root): `kicad` (mcp-server-kicad) and `kicad-pcb-tools` (KiCAD-MCP-Server, needs about a minute before its worker answers).
- Project agent config lives in `.claude/` for this repo (user choice).

## Open issues

- Post-import fixes kept (measured on raw imports, reasons in their docstrings): `widen_thin_tracks` (power tracks necked below 0.22 mm outside fine-pitch courtyards; widens only while the clearance holds), `prune_ground_tracks` (v3; v2 has it off), `seat_track_ends` (pruning leaves grazing ends) and `remove_dead_ends`. Removed as dead (never seen in raw fastroute output): `widen_neckdown_stubs`, `shift_near_miss_tracks`; carry_over no longer forces vias through. The fine-pitch neck-down DRC rule for power (min 0.15 mm) must come after the generic one (min 0.1 mm): KiCad applies the last matching rule.
- Review 2026-10-23 findings E12–E15 and R1 (`hardware/docs/review-2026-10-23.md`); DALI short protection while the MCU is unpowered.
- Bench work: ETS 5.7/6 import and programming on TP hardware, DALI timing on an oscilloscope, bus-power and AUX hot-unplug measurements, first prints of the current case.
