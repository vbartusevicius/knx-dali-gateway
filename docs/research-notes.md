# Research notes (2026-10-09 to 2026-10-23)

Background research, procurement notes and the original phase plan, moved out
of AGENTS.md on 2026-11-16 unchanged. They record why the project looks the way
it does; they are not current instructions. Where they disagree with AGENTS.md,
the code, or `hardware/docs/`, those win (for example: the MCU is now the XIAO
ESP32-C6, the core is AUX-powered with KNX fallback, the DALI bus layer is our
own and no longer espressif/dali, and all phases have been implemented in
software).

## Reference project and complexity

Reference: https://github.com/OpenKNX/GW-REG1-Dali

The initial review examined the upstream `v1` branch at commit `5cd7e66bf0e0aa692973e32a2acd6e941a05e8be`.

Upstream functionality includes:

- One active DALI line, with 64 individually addressed control-gear devices and 16 groups.
- Discovery and short-address commissioning.
- Switching, absolute and relative dimming, scenes, and feedback.
- DT8 colour and colour-temperature control.
- Staircase timers and human-centric lighting.
- ETS parameters, communication objects, and product-database generation.
- RP2040 dual-core operation and queued DALI commands.

The upstream `test/` directory contains only a placeholder README, not an automated test suite. Most application complexity is concentrated in `DaliModule` and `DaliChannel`, with substantial protocol functionality delegated to dependencies.

Approach assessment:

| Approach | Complexity | Assessment |
| --- | --- | --- |
| Adapt upstream firmware | Moderate | Shortest route to a working device; inherits coupling and architecture. |
| Own application with existing protocol stacks | High but manageable | Initial recommendation; offers clearer ownership and testability. |
| Rewrite KNX and DALI stacks as well | Very high | Separate protocol-engineering effort, particularly KNX management and ETS compatibility. |

The difficult parts are commissioning, timing, retries, configuration persistence, ETS integration, and bus-fault behaviour, not merely translating switch telegrams.

## Licensing and product constraints

- Upstream gateway firmware, the inspected OpenKNX KNX stack, and its Arduino DALI driver use GPL-3.0. New application code does not remove obligations arising from integrated GPL dependencies.
- Published REG1 PCB and enclosure files use CC-BY-NC-SA-4.0. They are not unrestricted commercial design assets.
- The user has confirmed personal DIY development with possible Tindie/similar sales and wants everything project-owned open source. GPL permits sales when its obligations are met; prepare the corresponding firmware source, permitted dependencies/notices, and reproducible build/install information for distributed versions. Exact firmware/hardware/documentation licences and third-party compatibility still need an audit before release.
- Tindie sales are commercial distribution even at DIY scale. Do not incorporate noncommercial-licensed PCB/enclosure designs into units for sale without separate permission. Creating original hardware/enclosure designs or obtaining suitable commercial rights avoids that restriction; publishing adaptations as open source does not cancel an upstream noncommercial condition.
- Audit binary-only SDK/radio components and their redistribution/linking licences; the user's openness requirement covers our sources but does not grant authority to relicense third-party material or claim unavailable vendor source is open.
- Before selling, identify intended markets and product form (board, kit, or assembled device) and assess applicable safety/radio/EMC/environmental requirements. Hobby scale, kits, a certified radio module, or open-source publication are not automatic exemptions for the final product; do not claim KNX/DALI certification or use certification marks without authorization.
- Audit the specific versions and licences of any replacement libraries; do not assume all KNX or DALI implementations share the same licence.
- A working prototype is not evidence of KNX/DALI certification or electrical-installation compliance.

Hardware reference: https://github.com/OpenKNX/OpenKNX/wiki/REG1-Dali

## Local verification strategy

### Deterministic core tests

Test:

- KNX datapoint encoding/decoding and routing.
- Switching and documented brightness mapping, including reserved DALI values.
- Relative dimming start and stop.
- Device, group, and scene boundaries.
- Commissioning state transitions.
- Timeouts, retries, queue saturation, restarts, and corrupted-storage recovery.

### Docker integration lab

Proposed topology:

```text
pytest / XKNX test client
          | KNXnet/IP
          v
     knxd + dummy backend
          | KNXnet/IP
          v
   host KNX test adapter
          |
     shared gateway core
          | DALI forward/backward frames
          v
   simulated DALI bus and gear
```

- `knxd` has a dummy backend requiring no physical interface; XKNX can generate KNXnet/IP traffic.
- `python-dali` contains fake gear and bus models covering addressing, groups, scenes, commissioning, and some DT8 colour-temperature functionality.
- Wrap the fake models behind a test service and add explicit fault injection and missing behaviour.
- The knxd dummy backend is a no-op bus driver, not a KNX TP electrical simulator.
- The python-dali fake gear is partial, not a standards-conformance simulator.
- Host-adapter integration tests do not automatically validate the embedded KNX stack or MCU-specific DALI driver.
- The inspected OpenKNX `v1dev` branch has a commented-out Linux platform header and CMake references to absent examples. Do not assume that particular fork builds or runs unchanged on Linux. Evaluate other versions and upstream implementations separately.

Sources:

- https://github.com/knxd/knxd/blob/master/src/backend/dummy.h
- https://github.com/sde1000/python-dali/blob/master/dali/tests/fakes.py

The initial review only inspected Docker/library capabilities. Gateway native tests and C6 firmware builds now run, as recorded in the verified implementation section. The knxd/XKNX/python-dali wire-level integration lab remains unimplemented; SDK API doubles are not that lab.

### Hardware-in-the-loop

Required bench equipment:

- MCU development board and KNX BCU/transceiver.
- KNX bus supply and KNX/IP interface.
- DALI interface, DALI bus supply, and real DT6/DT8 control gear.
- Logic analyser or oscilloscope.
- ETS access on Windows.

Validate physical timing, backward-frame response windows, bus faults, reboot behaviour, and ETS device programming. Test factory-reset commissioning and essential replacement workflows through ETS with gateway WiFi disabled/unconfigured. Verify normal operation through WiFi loss/reconnection and under dashboard/network load. KNXnet/IP in the Docker lab or a bench KNX/IP interface is test infrastructure, not a dependency on the gateway's WiFi.

On macOS, prefer network-connected interfaces and native flashing/debugging. Docker Desktop does not support direct USB passthrough; USB/IP is possible but adds complexity. ETS is a separate Windows requirement, not a Linux-container workload.

- https://docs.docker.com/desktop/features/usbip/
- https://support.knx.org/hc/en-us/articles/360018452180-ETS6-Software-requirements

## Phased implementation plan

### Phase 1: Foundation and executable test harness

1. Create the repository structure and build entry points.
2. Pin dependencies and toolchains; preserve upstream attribution.
3. Reproduce an upstream firmware build as a reference.
4. Implement portable core interfaces, virtual time, and a bounded command queue.
5. Build the Docker KNX/DALI lab.

Acceptance gate: a KNX switch telegram traverses the lab, changes one simulated DALI device, and produces the expected KNX status response.

Verify ARM64 toolchain support. Use a pinned AMD64 build profile only if required.

### Phase 2: Minimum useful gateway

Implement:

- One DALI line, initially using pre-addressed DT6 gear.
- Individual, group, and broadcast control.
- Switching, absolute dimming, and relative dimming.
- Status queries, missing-device detection, and bounded retries.
- Persistent configuration and defined startup behaviour.

Acceptance gate: automated tests cover normal operation, address boundaries, traffic bursts, disconnected gear, and restart recovery.

### Phase 3: Commissioning and ETS

Add:

- Discovery and automatic short-address assignment.
- Add-new-device and replacement workflows.
- Group membership and DALI scene storage/recall.
- ETS application parameters and communication objects.
- Product-database generation and configuration/version consistency checks.

Acceptance gate: commission a simulated 64-device line without duplicate assignments; verify product import, programming, DALI commissioning, and replacement through ETS on physical hardware with gateway WiFi disabled. Replacement must retain existing logical KNX channels and group-address associations.

DALI supports 16 stored scenes per gear. Any larger KNX scene space needs an explicit mapping. ETS-native commissioning and optional DCA functionality require further architectural decisions.

### Phase 4: Embedded validation and advanced features

Connect the shared core to the chosen MCU adapters and run hardware-in-the-loop tests. The original proposal selected RP2040; ESP32 with WiFi is now under consideration.

After single-colour DT6 operation is proven, implement and validate full DT8 Part 209 against a versioned command/capability matrix, including Tc, xy, RGBWAF, legacy primary-N where supported, queries, configuration, and colour scenes. Colour-temperature-first is an implementation sequence, not permission to omit other DT8 coverage. Validate startup/recovery readback-first with persisted-target fallback and continuously available dashboard/MQTT under bus-power constraints. Include DALI-2 input-device coexistence and multi-master-capable event-driven bus handling in staged milestones; the first host scaffold only recognizes control-device frame widths and is not a complete event/arbitration implementation. Defer HCL, emergency-lighting-specific functions, and KNX Secure unless explicitly included in scope.

Acceptance gate: physical bus captures and real-device behaviour agree with the tested software contracts.

### Phase 5: Schematics, PCB, and printable DIN enclosure

After interfaces and power requirements are proven:

1. KiCad schematics: MCU, KNX interface, DALI interface, power, protection, isolation boundaries, debug, LEDs/buttons, and terminals.
2. Enclosure envelope: DIN width, terminal access, PCB mounting, clearances, and service access.
3. PCB layout: placement/routing, ERC/DRC, isolation review, test points, BOM, fabrication and assembly exports.
4. Printable enclosure: parametric body, cover, DIN clip, light guides, and label area; export STL/STEP and verify PCB fit.
5. Prototype review: electrical, thermal, mechanical, and EMC validation before installation-ready claims.

The current power architecture separates the KNX-powered gateway core from an optional-use integrated DALI bus supply powered solely by the auxiliary 24/30 V DC screws. With an externally powered DALI bus, the device must operate from red/black alone without auxiliary DC. No onboard mains supply is required. DALI wiring must not simply be assumed to be SELV; review isolation requirements. A printed prototype enclosure is not automatically suitable for permanent electrical installation.

## Open decisions

- Exact open-source firmware/hardware/documentation licences, third-party compatibility audit, intended sales markets, and kit/assembled product form; small-scale sales and project-owned openness are confirmed.
- Adapt upstream firmware versus own application over existing stacks.
- Exact ESP32 variant and supported C++ standard/toolchain/SDK combination. C++ is the selected gateway implementation language; Python is test-side tooling, and the earlier Rust/RP2040 comparisons are research history rather than active implementation choices.
- Exact KNX TP transceiver/BCU and validated bus-powered electrical design. Wired TP control plus continuously available configured dashboard/MQTT are confirmed; networking must remain independent of essential bus operation.
- Local dashboard scope and WiFi credential provisioning.
- ETS-native commissioning, DCA requirements, and configuration ownership/synchronization between ETS, dashboard, gateway storage, and DALI gear.

## Follow-up research: ESP32, languages, and ETS

These are research findings and recommendations, not approved implementation choices. Sources were inspected on 2026-10-09; candidate combinations have not been built or bench-tested here.

### Compiled-language candidates

- C++: `thelsing/knx` has an ETS-configurable System B device stack, an `Esp32IdfPlatform`, and a native ESP-IDF 5.x demo without an Arduino layer. Unlike the inspected OpenKNX fork, its Linux examples are present, offering a candidate for testing the actual device stack in Docker. The library is GPL-3.0.
- C/C++ DALI: Espressif publishes `espressif/dali` 1.1.0, an Apache-2.0 ESP-IDF component using RMT. It documents control-gear commissioning, send-twice commands, backward-frame reception, and DT8 colour APIs. Component metadata requires ESP-IDF >=5.5.0; do not rely on the README's broader 5.0+ compatibility wording. It is relatively new and needs bus-timing, concurrency, and interoperability validation.
- Rust: `metaneutrons/knx-rs` includes a no_std-capable ETS device stack but explicitly marks its TP-UART crate WIP. Its IP crate uses Tokio/std and is not automatically an embedded ESP32 networking adapter. The repository was created in April 2026 and uses GPL-3.0.
- Rust: `ntrnnr/zweidraehte` provides a no_std/no-allocation device core, TP1 links, ETS product-definition tooling, and an in-repository conformance suite. Its documented reference MCUs are STM32 and RP2040, not ESP32. The repository was created in July 2026; its own documentation identifies unverified features and explicitly disclaims KNX certification. Licensing is AGPL-3.0-only with a separately offered commercial licence.
- Rust ESP32 services exist through the `esp-rs/esp-idf` monorepo (`esp-idf-svc`, `esp-idf-hal`, `esp-idf-sys`), including WiFi, HTTP, NVS, and OTA. Reusing Espressif's C DALI component would require an FFI wrapper; no comparably complete native-Rust ESP32 DALI library was established in this review.
- Current recommendation for lowest integration risk: portable C++ core on native ESP-IDF, evaluate thelsing KNX plus Espressif DALI first. ESP32-S3 is a candidate when dashboard, OTA, and memory headroom are important; final module, power budget, and antenna placement remain open.

Sources:

- https://github.com/thelsing/knx
- https://github.com/thelsing/knx/tree/master/examples/knx-demo-esp-idf
- https://components.espressif.com/components/espressif/dali/versions/1.1.0/readme
- https://github.com/metaneutrons/knx-rs
- https://github.com/ntrnnr/zweidraehte
- https://github.com/esp-rs/esp-idf

### Upstream development branch

`GW-REG1-Dali/v1dev` already includes ESP32 networking, web UI/web console configuration, and dependencies on OpenKNX DALI and a DALI IoT gateway. A merged scheduling fix addresses DALI backward-frame truncation during network/ETS activity. Inspect this branch as a reference rather than assuming the original `v1` branch describes all current work. RMT offload does not eliminate scheduler and response-window risks.

- https://github.com/OpenKNX/GW-REG1-Dali/tree/v1dev
- https://github.com/OpenKNX/GW-REG1-Dali/pull/20

### ETS-native provisioning versus DCA

The upstream `DALI-GW.ae-manu` JSON was inspected directly. Its parameter-button JavaScript uses `online.connect()`, `online.invokeFunctionProperty(160, 1, ...)`, and `online.readFunctionProperty(160, 1, ...)`. Scripts scan/address gear, read types/settings, and update ETS parameter values using `device.getParameterByName(...).value`. This is concrete evidence that basic DALI commissioning can live in ETS's native parameter UI without a separate DCA. Do not copy its unbounded polling/busy-wait patterns; design bounded operations, progress, cancellation, and versioned payloads. Confirm the required ETS/schema version through product import and real ETS testing.

A DCA is a separately installed `.etsapp`, associated with product applications, not a webpage embedded by `.knxprod`. ETS6 SDK documentation specifies .NET Framework 4.8-compatible DLLs; C# is the practical candidate irrespective of firmware language. Official development/distribution involves KNX membership, App developer registration/agreement, SDK access, and validation. KNX lists DCAs as free/no separate end-user App licence; this does not remove ETS or developer requirements. Use ETS SDK device communication rather than opening a separate KNX bus connection from the DCA.

Initial recommendation: ETS-native parameters and commissioning buttons first; optional DCA later for a richer inventory/group/scene editor. Expose one commissioning service to both ETS function-property and authenticated dashboard adapters, with only one commissioning operation owning the DALI bus at a time.

Separate three provisioning layers:

1. Network credentials: local secure SoftAP/BLE provisioning, independently of ETS. Espressif's current separate component is `network_provisioning`; older ESP-IDF versions also used `wifi_provisioning`.
2. KNX configuration: physical address, application download, communication objects, group-address associations, and parameters through ETS.
3. DALI commissioning: discover, identify, assign addresses, groups, scenes, and readback through the gateway.

The user has confirmed KNX TP as the control transport and ETS as the primary provisioning tool, with WiFi optional for management. KNX/IP-only over WiFi is out of scope. Treat WiFi credentials separately from ordinary ETS parameters; do not expose them in exports or logs. ETS-managed configuration is authoritative; dashboard maintenance must use explicit synchronization and revision checks to avoid silent overwrites on the next ETS download. Essential DALI commissioning and replacement must remain possible through ETS without gateway WiFi.

Sources:

- https://github.com/OpenKNX/GW-REG1-Dali/blob/v1/DALI-GW.ae-manu
- https://support.knx.org/hc/en-us/articles/7588671243410-Requirements
- https://support.knx.org/hc/en-us/articles/360000229110-Requirements-to-become-an-ETS-App-developer
- https://support.knx.org/hc/en-us/articles/360018452500-ETS-Apps-overview
- https://components.espressif.com/components/espressif/network_provisioning

## Hardware features and power discussion

### User-selected direction

- RESET button is required; a separate KNX PRG button, power/heartbeat/status LEDs, and MQTT observability were proposed by the user and are described below as design recommendations.
- KNX red/black powers the gateway core, including the ESP32. One line supports 64 control-gear addresses with an optional-use integrated DALI bus supply powered only from auxiliary DC.
- One two-pole auxiliary DC screw terminal accepts either the already powered white/yellow pair or an external 24 V DC supply for the internal DALI supply. Both alternatives use the same two screws, one source at a time. If DALI is externally powered, leave these screws unused and disable the internal DALI supply; core/ETS operation still runs from red/black.
- Reserve an optional display rather than requiring it in revision 1.
- Avoid onboard mains in the baseline; evaluate mains only as a separate future alternative if needed.

### Power constraints and sizing

- KNX red/black carries TP communication and powers the gateway core. Extract power through a suitable bus coupler with compliant current limiting, load-change filtering, and inrush behaviour; do not attach an arbitrary converter directly to the communication pair.
- There is no universal 20 mA per-device rule to assume here. For example, onsemi documents 5–40 mA fan-in configurations for NCN5130. A 40 mA example provides 1.2 W at 30 V or 0.84 W at 21 V before conversion losses and transceiver consumption. The chosen current class, converter capability, and minimum bus voltage need validation.
- ESP32 WiFi creates a separate feasibility gate: Espressif's ESP32-S3-WROOM-2 table gives 355 mA at 3.3 V for one high-power TX condition, about 1.17 W. This is an example for that module/RF condition, not a measured gateway budget. Average current and short-duration bursts must both fit the bus extraction/storage design. Buffer capacitance cannot fix a sustained power deficit, and converter peak-current capability is separate from input-power allowance.
- Do not assume a stock DevKit or small BCU's onboard regulators can supply the selected ESP32. Reevaluate the module/MCU choice, clocking, radio power/duty cycle, efficient conversion, energy buffering, LEDs/display, and USB bridge idle consumption before fixing the PCB. ESP32-S3 remains a candidate, not a mandatory selection; C3/C6 merit comparison under the bus-power constraint.
- A published bus-powered ESP32-C6 example exists in `tostmann/ip4knx` (TULX32). It caps WiFi TX power and disables brownout detection. Treat it only as evidence that such architectures exist, not as a validated design to copy. Keep brownout protection enabled and prove supply stability for this gateway.
- The white/yellow cable pair is not inherently powered. This user's installation supplies it from MDT STC-1280.01 U2 over approximately 0.5–1 m. MDT's manual describes U2 as an unchoked 30 V output for components needing auxiliary voltage. Other PSU models may have different restrictions.
- MDT STC-1280.01 datasheet: 30 V DC SELV, 1280 mA rated current, 1600 mA continuous current, and 1600 mA continuous total for KNX + U2. These are shared-output figures, not independent per-output allocations. Preserve normal-load headroom and do not size against the 1900 mA peak rating. Existing KNX and auxiliary loads remain unknown. A large PSU rating does not remove the gateway's own bus-coupler/current-class limits.
- DALI bus power feeds communication interfaces and bus-powered control devices, not the lamp's lighting power. LED drivers/luminaires still require their own supply.
- A preliminary worst-case allowance for 64 standard gear interfaces is 64 × 2 mA = 128 mA, around 2.05 W at 16 V, before any additional bus loads. This power comes from the internal auxiliary-powered DALI supply or an external DALI supply, not from KNX red/black. Use actual device ratings for the final budget.
- Evaluate an integrated DALI supply around 16 V nominal, approximately 200 mA guaranteed current, with a maximum supply current not exceeding 250 mA for the entire line. Guaranteed operating current and maximum/short-circuit current are different specifications. These are proposed supply targets, not a finalized circuit.
- A DALI supply must meet the electrical/timing requirements for a bus that transmitters pull low; it is not an ordinary fixed-voltage 16 V supply. Include short-circuit/thermal behaviour and recovery in verification.
- Discard the earlier combined 6–8 W auxiliary-input estimate: it incorrectly allocated MCU power to auxiliary DC. Budget core power against KNX extraction separately. At 16 V, 200 mA and 250 mA correspond to 3.2 W and 4 W of DALI output respectively; auxiliary input sizing must include the actual supply losses, fault behaviour, and margin.
- The same two auxiliary screws feed one protected DALI-supply input stage for either white/yellow DC or external 24 V DC. They do not supply the ESP32/core. No source selection/ORing is required between these alternatives. Review isolation, supply returns, reverse polarity, transients, and power backfeeding between all domains.
- The DALI TX/RX interface must work with a powered external DALI bus and KNX-powered core while auxiliary DC is absent. Do not make its receiver, isolation circuit, or enable logic dependent on an auxiliary-only rail. Keep DALI bus-current consumption within its declared allowance.
- Keep a reviewed isolation boundary between DALI and KNX/accessible logic, including the optional internal DALI supply and TX/RX signal paths. DALI installations may be FELV/non-SELV even if our board has no mains input. Review USB service isolation and power backfeeding too.
- Disable the internal supply for an externally powered DALI bus; do not indiscriminately parallel supplies. Auxiliary absence is not a fault in external-supply mode. If DALI itself has no power, core startup, ETS programming, and actionable diagnostics must still work rather than hang. Total maximum DALI supply current must remain within the line limit.

### Front-panel and service recommendations

- RESET: proposed baseline is a recessed hardware ESP32 EN/CHIP_PU reset, preserving ETS/DALI configuration and working when the application hangs. This restarts the MCU but does not remove power from all board peripherals or the DALI bus. A local full power-cycle circuit is a separate, unselected option; it must not cut the shared KNX line and could affect lights via DALI system-failure behaviour if it interrupts DALI power. Reinitialize peripherals and keep bus TX inactive during reset/boot.
- KNX PRG: separate accessible programming button with a dedicated red indicator reflecting actual programming mode. Do not conflate it with MCU bootloader recovery or normal reset.
- BOOT/service access: USB-only flashing and console, with an internal BOOT control plus RESET for USB bootloader recovery when normal firmware is broken. No dedicated UART/JTAG service connection. ESP32-S3 supports native USB flashing/CDC console; the built-in peripheral's official name includes Serial/JTAG, but separate JTAG tools/connectors are not part of this product requirement. Exact module, USB connector, power/isolation scheme, and pin allocation are not selected yet.
- Factory reset: a separate deliberate, confirmed procedure; do not erase configuration or readdress lights on an ordinary reset or casual button press.
- PWR: low-current indication that the relevant logic rail is present, ideally independent of application firmware.
- SYS: dim RGB or multicolour status indication for healthy heartbeat, commissioning, and faults; derive health from the actual application/bus loop, not a timer that could continue blinking during a hang.
- KNX/DALI activity indicators are useful optional additions. Distinguish no activity from bus failure; healthy idle buses need not be busy. If power/space is tight, use one documented status pattern rather than many bright LEDs.
- WiFi intentionally disabled or unavailable is not a primary gateway fault. Display/network tasks must not block control or ETS operations.
- Low-current discrete LEDs are preferred over a decorative/high-brightness addressable RGB LED for the baseline. Indicator mapping and exact LED count remain proposals.
- Optional display: reserve I2C/SPI plus appropriate power/reset provisions and an enclosure/front-board variant. Its useful offline content is KNX physical address, DALI inventory/status, commissioning progress, and errors; show IP only when networking is available. LCD/OLED type is not selected.
- MQTT can publish retained IP/hostname/device identity and health, with availability/Last Will and updates on address changes/reconnection. It remains optional and requires functioning WiFi and a broker; retained information can be stale. Never include credentials in telemetry.
- Also expose IP/network status through an ETS diagnostic read operation and local discovery where available. MQTT is not the only way to locate the management interface.
- Consider auxiliary/logic/DALI voltage monitoring, DALI current/fault reporting, brownout handling, and watchdog recovery. Confirm sensor accuracy, isolation, and BOM trade-offs before making them mandatory.

### Open hardware decisions and acceptance tests

The PSU model and auxiliary distance are now supplied: MDT STC-1280.01, approximately 0.5–1 m. Verify remaining shared KNX/U2 current budget and both domains' actual input-voltage conditions. Prove KNX-powered controller operation before selecting MCU, transceiver, converter/buffering topology, current class, and USB implementation. Then select auxiliary DALI-supply voltage range/protection, guaranteed current, isolation topology, enclosure width, and optional display connector. No removable MCU/socket requirement remains.

Test KNX-powered operation with auxiliary DC disconnected and DALI externally powered, including ETS provisioning, control, status, USB service, and the proposed WiFi workload. Test radio-off and radio-load cases at minimum KNX voltage; verify current/load-change limits, inrush, brownouts, and no disruption of other bus devices. Also test auxiliary-only DALI supply operation, internal/external supply modes, auxiliary loss, DALI shorts/recovery, and reset/boot recovery. A DALI supply fault must not collapse the independently KNX-powered core. A RESET must not disturb other KNX devices or erase DALI addressing.

Sources inspected during this discussion:

- https://knx.org/devices/bus-supply-unit-640-ma-auxiliary-output-0
- https://www.onsemi.com/download/application-notes/pdf/and90055-d.pdf
- https://www.knx.org/devices/113/M-0071_H-ZDIIBD16-1_HP-3381-30-367F_CI-ZDIIBD16-1/en-US/download
- https://www.dali-alliance.org/products/8459/inbox-dali-16
- https://www.dali-alliance.org/products/390/dali-ps
- https://www.lunatone.com/wp-content/uploads/2018/04/24033444-24VDO_DALI_PS24_100mA_EN_D0080.pdf
- https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html

### USB and ESP32 implementation notes

- The user does not require a replaceable/socketed MCU. Bare WROOM/MINI modules can be soldered to our PCB. Reproduce the USB convenience of a development board rather than assuming a development board or custom daughterboard must be plugged in.
- Native USB-capable ESP32 variants can provide flashing and a serial console directly. A classic ESP32-WROOM can instead use an onboard USB-to-UART bridge with an auto-download/reset circuit. Both expose only USB to the user; the exact chip/module, USB connector, and bridge choice remain open and must account for KNX power consumption.
- A DevKit can be used for software/bench prototyping, but its regulators, always-on LEDs, bridge consumption, pin allocation, and USB/header supply paths do not constitute a validated bus-powered product design.
- Ensure recovery works through BOOT + RESET even if firmware disables/reconfigures normal USB. Keep native USB pins reserved if used, and make console logging nonblocking so an absent PC cannot stall KNX/DALI control.
- In normal operation, regulated core rails come from KNX red/black power extraction. Auxiliary 24/30 V feeds only the internal DALI supply. Never feed either raw source voltage directly to ESP32 power pins.
- USB VBUS and KNX-derived core power must not fight or backfeed each other or auxiliary/DALI domains. Support USB-powered bench flashing/diagnostics with all external wiring disconnected; this is a service requirement, never a runtime dependency. Do not energize exposed KNX/DALI terminals from USB. For exceptional live-system debugging, define a reviewed isolated service arrangement (onboard or an appropriate external USB isolator) and verify grounding, power selection, and unpowered-interface behaviour before allowing connection. If a prototype DevKit is used, review its documented mutually exclusive USB/5 V-header/3.3 V-header power options.
- A bridge chip's power gating or USB-side supply may reduce idle bus load, but that is a design option requiring correct logic-level and unpowered-interface behaviour, not permission to sacrifice recovery.

### Reset, brownout, and watchdog terminology

- Hardware MCU reset: assert ESP32 EN/CHIP_PU to restart the chip and firmware without switching off the entire PCB supply. It is not a factory reset or a complete peripheral power cycle.
- Full local power cycle: intentionally switch selected local rails off and back on, with defined discharge/timing. Extra circuitry is needed; USB or other interfaces must not keep the supposedly switched-off domain powered. If DALI supply is interrupted, connected gear can enter its configured system-failure state. Do not interrupt the shared KNX line.
- Brownout handling: detect supply voltage below a safe operating threshold and reset/hold the controller until a stable supply returns, instead of operating unpredictably. ESP32-S3 has a built-in brownout detector; an external supervisor may be appropriate for slow/unstable supply ramps. Keep detection enabled, design sufficient regulator/transient headroom, and use corruption-tolerant persistent storage. It does not make flash writes during a failing supply safe or fix an undersized supply.
- Watchdog recovery: a hardware timer requires periodic evidence of healthy execution. If monitored control tasks stop progressing, configure the watchdog to trigger recovery/reset. Do not feed it from an unrelated timer that continues running while the gateway is hung. ESP-IDF task-watchdog defaults can report rather than reboot, so production reset behaviour must be configured and tested.
- Automatic resets preserve application configuration; they do not erase KNX associations or DALI addresses. Report reset causes through USB/ETS diagnostics and address repeated reset loops. Optional WiFi/MQTT failure must not cause healthy KNX/DALI operation to be rebooted.

Additional sources inspected:

- https://cdn.mdt-group.com/image/upload/v1753521465/STC-1280-01_MDT_DS_GB.PDF
- https://www.mdt-group.com/fileadmin/user_upload/STC-xxx0-01_MDT_TM_V10_EN.pdf
- https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/wdts.html
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/fatal-errors.html
- https://www.onsemi.com/products/interfaces/wired-transceivers-modems/ncn5130
- https://documentation.espressif.com/esp32-s3-wroom-2_datasheet_en.html
- https://github.com/tostmann/ip4knx

## DIN enclosure and prototype procurement

### Size and mechanical approach

- The user expects a classic 1M or 2M distribution-board enclosure. Typical widths are approximately 17.5–18 mm and 35–36 mm; both mount on a 35 mm DIN rail. Rail width is not the enclosure/module width.
- The user delegates mechanical selection to the agent. Use 2M as the working first-revision envelope because the KNX power extraction, isolated DALI supply/interface, USB, terminals, indicators, and antenna require space. A 1M version is an optimization candidate, not a proven fit; 2M fit still requires validation.
- Establish a parametric mechanical envelope and PCB mounting, terminal access, distribution-board cover fit, ventilation, isolation, antenna, and rail-clip constraints before routing. Carefully review PCB placement/layout and its 3D model before finalizing and ordering printed case prototypes. PCB and enclosure are co-designed, not routed without a mechanical envelope.
- The chosen prototype direction is off-the-shelf development/interface boards first, then our PCB and a custom 3D-printed enclosure. A stock molded body is not the default. Standard printed material or a stock case's material rating is not evidence that the finished device is suitable/certified for permanent installation.
- Molded case candidates retained only as references/alternatives: Italtronic Modulbox 2M; CamdenBoss CNMB/2/KIT (listed external dimensions 36 × 90 × 58 mm, UL94-V0 polycarbonate); Phoenix Contact BC 17.8/35.6 families. If that route is reconsidered, check the complete kit and actual drawings before adopting its PCB geometry.
- Some manufacturer CAD/sample downloads require registration. Do not assume drawings imply current stock or a complete enclosure kit.

### Small-quantity PCB and assembly options

Vendor capabilities were researched on 2026-10-09. These are published service descriptions, not project-specific quotes, stock checks, or procurement approvals.

| Supplier | Useful role | Published small-quantity capability |
| --- | --- | --- |
| JLCPCB | Bare PCB fabrication and assembly with its parts library | Prototype assembly advertised from 2 boards; confirm process and board-size constraints at quote time. |
| AISLER | European fabrication/assembly and KiCad-oriented workflow | Assembly advertised from 1 populated board; bare boards in sets of 3. |
| PCBWay | Fabrication, assembly, and flexible sourcing | Turnkey, customer-supplied/kitted, and hybrid sourcing; verify the applicable minimum through the quote rather than assuming a universal MOQ. |

- Distinguish bare PCBs from assembled PCBAs. Prefer outsourced fine-pitch SMT assembly for the KNX/power circuitry; hand assembly of larger connectors/buttons is an option, not a finalized manufacturing split.
- Prepare Gerbers/drills, exact-MPN BOM, pick-and-place data, assembly drawings, explicit unpopulated options, and any factory programming/test instructions.
- Factory DFM/AOI/electrical checks do not replace KNX/DALI functional, bus-power, isolation, or thermal validation.
- Compare complete landed cost: PCB, components, setup/assembly, special-part sourcing, shipping, taxes, and any inspection/testing. Do not budget from promotional bare-PCB prices or promise turnaround before a real quote.
- Check specialized KNX, isolation, transformer/DC-DC, and connector availability before freezing footprints. Where an assembler lacks a part, evaluate authorized sourcing/consignment; do not silently substitute safety-critical components.

### Case printing and components

- JLC3DP and PCBWay offer small-batch mechanical/3D-printing services. Consider SLS/MJF nylon for functional clips/fit tests and SLA for detailed cosmetic prototypes, subject to actual material, temperature, flammability, strength, and tolerance requirements. None is automatically approved as an installed electrical enclosure.
- DigiKey and Mouser are candidate sources for exact MCU modules, semiconductors, isolation components, and connectors. TME is a useful source for terminals and DIN housings. Use JLCPCB's assembly library/LCSC where appropriate for a JLC build; verify exact manufacturer identity, ratings, and availability.
- The BOM should retain manufacturer part numbers independently of distributor SKUs, with approved alternates where appropriate. Avoid tying the engineering design solely to one supplier's catalog identifiers.

### Remaining decisions and suggested route

Resolved user choices:

1. Agent selects the mechanics within 1M/2M; use a 2M working envelope. Off-the-shelf development/interface boards precede custom PCB and carefully reviewed printed enclosure prototypes.
2. Case-open USB service is acceptable, normally with all external wiring disconnected; live-system debugging is exceptional and needs a reviewed isolated method.
3. WiFi dashboard and MQTT are continuously available when configured, while control remains independent of network health.
4. Start with single-colour lamp control and retain full DT8 as the target. One application controller is expected, but DALI-2 inputs can share the bus; include standards-aware input/event and multi-master bus behaviour in the architecture rather than assuming a lights-only line.
5. Prefer ballast state readback on recovery; use a valid persisted target only for otherwise reachable devices/fields lacking reliable readback, and report A-versus-B capability/source.
6. Personal DIY with possible Tindie/similar sales; open-source all project-owned design/software/tooling. Licensing and market obligations still apply to distributed units.

Remaining engineering selections: exact MCU/dev-board/interface evaluation setup, measured always-connected network power budget, bus coupler/converter, Part 209 edition/coverage and library audit, persistent-target storage/field readback model, ETS power-on/system-failure defaults, USB isolation/power paths, printed-case material/geometry, exact licences, and intended sales markets/product form.

Suggested route: code/test harness and off-the-shelf bench setup, bus-power proof under sustained dashboard/MQTT load, reviewed PCB within the mechanical envelope, small prototype PCB batch (initial recommendation 3–5 boards with a small assembled subset), and custom printed case iterations after PCB/3D review. This is not permission to order boards/components, upload design files, or start firmware/hardware scaffolding before the user approves the next implementation step. No project-specific manufacturing quote or order has been placed.

### Resources

- https://jlcpcb.com/solutions/pcb-prototype-assembly
- https://aisler.net/en/products/assembly
- https://www.pcbway.com/pcb-assembly.html
- https://eng.italtronic.com/products/modulbox_one_en/2m_modulbox_one_en/
- https://www.camdenboss.com/camden-boss/cnmb2kit-din-rail-module-box-kit%2c-industrial-enclosure%2c-polycarbonate-enclosure/c-23/p-18578
- https://www.phoenixcontact.com/en-de/products/upper-housing-part-bc-356-ot-u11-bk-2200777
- https://www.tme.eu/html/EN/modulbox-enclosures-for-din-rail-mounting/ramka_557_EN_pelny.html
- https://jlc3dp.com/capabilities/3d-printing-service
- https://www.pcbway.com/rapid-prototyping/3d-printing/
- https://www.digikey.com/en/supplier-centers/espressif-systems
- https://www.mouser.com/new/espressif/

## Follow-up scope: continuous networking, full DT8, and recovery

### Recovery: readback A before persisted target B

- The user's preference is A (discover/read actual ballast state) wherever available; otherwise B (restore saved command state), with explicit indication when only B is possible. Apply this per device and state field, not a blanket assumption that every field on every device is readable.
- Normal control gear provides QUERY ACTUAL LEVEL for brightness. DT8 provides colour-type/status/value queries for supported fields. A valid actual result wins over a stale persisted target; synchronize KNX/dashboard feedback without issuing a blanket startup on/off command.
- Query responses can be unavailable during gear startup or due to defects; a lost reply can also indicate wiring, timing, power, or reachability problems. Use bounded retries and classify errors. Do not mark a device readback-incapable simply because it timed out, and do not immediately overwrite it during a transient fault.
- For an otherwise reachable device/field lacking valid reliable readback, restore a valid persisted resolved target and label the state source as restored/fallback. If no valid saved target exists, report unknown rather than inventing a brightness/colour value.
- Save resolved target state rather than replaying raw relative-dimming packets or every historic command. Preserve address/configuration consistency and avoid flash writes at every dimming step. Storage format, commit/wear policy, and fallback eligibility are implementation decisions to test.
- Query individual short addresses. A group can contain mixed levels/colours, and group/broadcast queries can produce multiple replies; derive group status from members rather than assuming one response describes all lamps.
- Physical mains power-on and DALI bus-power loss are gear-controlled behaviours, separate from a gateway firmware restart. Expose applicable power-on/system-failure settings via ETS and validate the gear's supported semantics; concrete initial defaults remain to be selected. A powerless gateway cannot guarantee delivery of a final command.
- Startup recovery must not perform discovery that randomizes/readdresses installed gear, erase configuration, or silently change ETS associations. State readback is distinct from an explicitly requested commissioning/replacement routine.

### Full DT8 coverage and candidate-library audit

- Full DT8 means a deliberate IEC 62386-209 command/capability matrix, not only a colour-set helper. Current DALI Alliance colour types are Tc, xy, and RGBWAF; the standard also contains legacy primary-N, which must be explicitly accounted for in the requested full-protocol scope. Capability detection determines what can be used on each gear.
- Test DTR sequencing, ENABLE DEVICE TYPE, activation/auto-activation, supported queries and limits, masks/reserved values, colour fades/status, and scene storage/recall. Unsupported features must be surfaced rather than silently treated as implemented.
- The inspected Espressif `master` DT8 helper API exposes CCT, RGB, and RGBWAF but no xy/primary-N helper modes. Its lower-level transactions may support implementing additional sequences, but a marketing claim of DT8 support is not proof of full coverage.
- The earlier current-master inspection identified DALI_DAPC_MASK as 0xFE instead of the independently documented MASK 0xFF. Inspection of downloaded espressif/dali 1.1.0 (repository commit 5f9cb98ae4d0e8153c4b4d1accf471214e5b6fe8) now confirms that same definition in its released header. This is static source evidence, not a reproduced physical failure. The gateway does not use that macro or the colour/commissioning helpers: ArcLevel accepts 0..254, excludes MASK 255, and adapter tests independently require raw DAPC data 254 with is_cmd=false. Further driver/helper timing and standards auditing is still required.
- python-dali command definitions are a useful independent reference/test aid, but its fake gear is incomplete, especially for full DT8. Extend the simulated state models and compare with standard/manufacturer vectors and real gear instead of treating passing self-consistent mocks as conformance.
- The user clarified that DALI-2 sensors/buttons must be considered even with one gateway. Input devices are multi-master and normally event-driven; a single-master application controller may poll inputs but does not establish event-driven coexistence. Target multi-master-capable controller behaviour and staged input support, distinct from adding a second gateway. DALI-2 has up to 64 control gear and 64 control devices in separate address spaces; the gateway's own control-device address and all input-device bus loads count toward their respective limits. Address capacity is not a guarantee that the 200 mA supply can power every possible combination.

### Continuous networking and sales

- Continuous availability permits appropriate WiFi modem power-saving while maintaining connection and responsive services; it does not permit making dashboard/MQTT maintenance-window-only. Measure realistic steady-state, association/reconnect, dashboard traffic, MQTT updates, and firmware-update workloads at minimum KNX voltage.
- Network failures must not starve control, cause healthy bus tasks to reboot, or create a dependency on auxiliary DC. If continuous networking cannot meet the proven KNX budget, report the conflict and seek an explicit revised requirement rather than silently dropping networking or changing the power architecture.
- Publish all project-owned preferred-form sources: firmware, ETS/application scripts, KiCad schematics/layout/libraries, BOM and manufacturing tooling, parametric enclosure CAD and print profiles, simulator/tests, and build instructions. Exact licences and notices remain to be selected consistently with dependencies; third-party materials and binary-only vendor components retain their own licences and need disclosure/audit.
- DIY/Tindie intent does not change the commercial nature of paid distribution or authorize upstream noncommercial hardware adaptations. Identify release markets and product form before a sales listing; assess final-product obligations separately from voluntary KNX/DALI certification and component approvals.

Sources for these clarifications:

- https://www.dali-alliance.org/dali/colour.html
- https://infosys.beckhoff.com/content/1033/tcplclib_tc3_dali/5627810571.html
- https://infosys.beckhoff.com/content/1033/tcplclib_tc3_dali/6432727435.html
- https://infosys.beckhoff.com/content/1033/tcplclib_tc3_dali/5627763851.html
- https://raw.githubusercontent.com/espressif/esp-iot-solution/master/components/dali/include/dali_command.h
- https://raw.githubusercontent.com/espressif/esp-iot-solution/master/components/dali/include/dali_color_control_dt8.h
- https://raw.githubusercontent.com/sde1000/python-dali/master/dali/gear/colour.py
- https://www.gnu.org/philosophy/selling.html
- https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode
- https://europa.eu/youreurope/business/product-requirements/labels-markings/ce-marking/index_en.htm

## Implementation foundation and bench shortlist

The user approved gateway coding after the native PlatformIO/multi-app workflow discussion. The portable lighting/recovery core, SDK adapters, native tests, and C6 development image now exist. The next implementation milestone is actual KNX device-stack integration and an isolated KNXnet/IP-to-simulated-DALI vertical slice, using Docker for Linux/test services where useful. Do not create a second independently maintained firmware build system or require a dev container for USB upload. No runnable wire-level lab, ETS product database, physical multi-master implementation, full DT8 coverage, network services, or electrical design exists yet. Do not confuse host/API-double tests with physical timing, ETS acceptance, or DALI-2 certification.

### Development hardware candidates

| Item | Initial candidate / role | Qualification still needed |
| --- | --- | --- |
| MCU | ESP32-C6-DevKitC-1 for first evaluation; ESP32-S3-DevKitC-1 as a comparison if needed | C6 guide documents 8 MB flash, native USB plus bridge, and J5 current-measurement access. Neither variant is proven to meet our bus-power/RMT/network requirements yet. |
| KNX interface | NCN5130 evaluation board or purchased NanoBCU V2/2.10 bench module | NanoBCU documents 40 mA bus limit and 100 mA converter outputs; do not assume its regulator powers WiFi bursts. Its schematic is NC-licensed and CAD is not published, so do not copy it into our sold hardware. |
| DALI interface | MikroE DALI 2 Click / MIKROE-2672 candidate | Public documentation describes optocoupler isolation and selectable 3.3/5 V GPIO interface. It is a physical interface, not proof of controller firmware compliance or a DALI bus supply. Check exact hardware revision and electrical timing. |
| DALI bench supply | Existing compliant DALI supply, otherwise a DC-input supply such as Lunatone DALI PS24 100 mA | Adequate for a few test devices, not our final 64-gear supply target. Never connect ordinary 24/30 V DC directly to DALI. |
| Gear | One enclosed/safely powered dimmable LED fixture/driver, then DT8 gear covering Tc, RGBWAF and xy | Exact fixture/LED electrical compatibility and available colour capabilities must be checked before buying. |
| Input device | DALI-2 certified Part 301 push-button and/or Part 303 occupancy input for event tests | Choose a genuine input device, not a second application controller. Input-device commissioning and events need real-bus validation. |
| Tools | Suitable isolated USB service setup, DMM and oscilloscope/logic analyser; KNX/IP or USB interface and Windows ETS access | Review isolation and power before live bench wiring. No dedicated JTAG/UART service equipment is required. |

These are a shortlist, not purchase orders or proof of availability. Begin functional prototyping with reviewed development-board power; prove the final KNX-derived power circuit separately under sustained networking and input-event traffic. The DALI-2 control-device address pool includes an addressed multi-master gateway; separate address capacity does not increase the line's current budget.

Sources:

- https://www.dali-alliance.org/dali/systems.html
- https://www.dali-alliance.org/dali/control-devices.html
- https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html
- https://github.com/Ing-Dom/NanoBCU
- https://media.digikey.com/pdf/Data%20Sheets/MikroElektronika%20PDFs/MIKROE-2672_Web.pdf
- https://download.mikroe.com/documents/add-on-boards/click/dali-2/dali-2-click-schematic-v100.pdf

## Developer workflow and release validation

### Local development and community practice

- Native PlatformIO is the primary local build/upload/test entry point, with its IDE extension in VS Code/Windsurf/Devin Desktop. Keep the CLI usable too. Local USB upload/monitor must not depend on Docker Desktop USB/IP or a privileged container.
- PlatformIO is not synonymous with Arduino: the official ESP32-C6-DevKitC-1 board definition lists `espidf`. ESP-IDF is the recommended SDK for the current timing/peripheral requirements, subject to a dependency compile check.
- The inspected released Espressif32 7.1.3 manifest uses ESP-IDF 6.1 packages; the 6.12.0 manifest uses ESP-IDF 5.5 packages. These observations are not a release choice. Pick and pin a platform/SDK/compiler combination that actually builds the chosen KNX/DALI components; do not use `master`/`latest` or blindly upgrade SDK major versions.
- OpenKNX projects use PlatformIO; OpenKNX OAM-StateEngine has a GitHub workflow that generates/checks product data and builds releases. WLED is another public example of PlatformIO builds and artifacts in GitHub Actions. These establish a community pattern, not proof their test coverage or reproducibility meets our goals.
- Keep fast core tests native (PlatformIO test environment using a standard supported framework such as Unity) and compile the same core into firmware and the host adapter. ESP-IDF's internal CMake component files are expected; a separate user-facing firmware build stack is not.
- Docker is useful for Linux knxd, isolated protocol test services, and a canonical CI build environment. Developers need it only for the integration suite, not every edit/build/upload cycle. Do not add a mandatory dev container as the baseline.
- Pin PlatformIO Core, platform and resolved toolchain/framework packages, firmware/test libraries, product generator, and configuration. Record hashes/version manifests for release artifacts. ESP-IDF `CONFIG_APP_REPRODUCIBLE_BUILD` removes build-time/path variation; verify repeatability with artifact hashes rather than assuming Docker alone makes builds identical.

### Mandatory mock/test layers

1. Native core tests: independent command/frame vectors, address and instance bounds, queues/timeouts, readback-first recovery/fallback, persistent-target semantics, DALI masks, and firmware/ETS protocol contracts.
2. KNX integration: knxd with a dummy backend and XKNX clients, connected to the real shared core through a host adapter. Exercise group writes/reads/status and malformed/lost/repeated traffic. The dummy backend does not simulate TP electrical behaviour or implement our device's System B management; those need separate actual-stack management/virtual-platform tests where supported, then ETS/hardware checks.
3. DALI integration: simulated gear and control devices using python-dali as a partial reference/model, extended for required DT8 and DALI-2 behaviour. Exercise 8/16/24-bit traffic, events interleaved with replies, discovery/addressing, colour/scenes, faults, and deterministic arbitration/backoff scenarios. Model the application/scheduler boundaries; real Manchester timing, collisions, RMT re-arm, electrical levels and RF-induced power/timing effects remain hardware-in-the-loop gates.
4. Run the integration lab in a private Docker network with no privileged USB or automatic discovery of the house KNX installation. Never run CI commissioning/address-changing tests on the user's live installation. Pending/unimplemented conformance features are not passing checks.

### ETS product data and optional DCA tests

- `.knxprod` is product data imported into ETS, not an ETS plugin. A DCA is a separate `.etsapp`; native ETS parameter-button scripts can remain the first commissioning UI.
- Keep editable product/application XML as the authoritative parameter/object definition and generate firmware constants/header and product data from it. Check identifiers/references, DPTs/object sizes, parameter ranges/defaults, memory offsets/overlap, table bounds, mask/profile/manufacturer metadata, and version compatibility against the compiled firmware contract. Do not require firmware and application version numbers to be numerically identical; require an explicit compatible pairing.
- OpenKNXproducer provides `check` and `create`, and generates a C/C++ parameter/object header. Its own README warns that sanity-check success does not guarantee ETS import or adding a device to a project. Pin the chosen generator and investigate its converter/signing/runtime dependencies and licences; use legally available supported tooling, never key extraction or signature-check bypasses.
- Add schema validation for the selected XML namespace when the appropriate schema/tooling is available, generated-file consistency checks, and package/signature/tool validation as supported. Test native ETS scripts against mocks of the ETS online/parameter APIs and shared firmware request/response fixtures. These are headless checks, not the real ETS runtime.
- Actual ETS import, device creation/parameter UI, address/application download and commissioning need a licensed Windows ETS installation with the intended supported version(s). Start with an explicit manual compatibility gate; a private self-hosted Windows/bench runner can automate it later. Do not assume a generic GitHub-hosted runner contains or can license ETS.
- If a DCA is added later, use separate Windows/.NET SDK build and unit tests, plus real ETS UI/device tests. Do not make DCA infrastructure a dependency before it is required.

### Pipeline and production artifacts

- GitHub Actions is the preferred pipeline target. No remote repository is configured and no workflow files exist yet.
- Pull-request checks run native tests, generated-contract/product checks, isolated mock integration tests and firmware compile checks using pinned versions. A periodic macOS native-build check can detect developer-workflow regressions; it need not duplicate every integration job.
- Produce release candidates from the pipeline, validate their firmware/product pairing and packaging, then perform the required ETS/bench acceptance on those exact artifacts. Publish only after all required automated checks and declared compatibility/hardware gates pass. Some tests necessarily need candidate builds first; the requirement is tested artifact promotion, not an impossible rule that every artifact test runs before the artifact is built.
- Promote the tested binaries/product data rather than rebuilding after testing. Package the appropriate firmware/bootloader/partition images, `.knxprod`, hashes/version/dependency manifest and corresponding project sources/build instructions. Keep development/debug artifacts distinct from production releases.
- Use a pinned Linux build environment for canonical release output (a container is appropriate); native local PlatformIO is still the standard developer route. Do not maintain divergent source/dependency configurations for local and CI builds.
- Keep release permissions/licensed tooling on trusted release jobs. Do not expose secrets, live hardware, or licensed self-hosted runners to arbitrary pull-request code. Never suppress failing tests or weaken security policies to publish a release.

Workflow resources inspected:

- https://docs.platformio.org/en/latest/frameworks/espidf.html
- https://docs.platformio.org/en/latest/integration/ide/vscode.html
- https://docs.platformio.org/en/latest/advanced/unit-testing/index.html
- https://docs.platformio.org/en/latest/integration/ci/github-actions.html
- https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/reproducible-builds.html
- https://docs.docker.com/desktop/features/usbip/
- https://github.com/OpenKNX/OpenKNXproducer
- https://github.com/OpenKNX/OAM-StateEngine/blob/v1/.github/workflows/openknx-oam-check.yml
- https://github.com/wled/WLED/blob/main/.github/workflows/build.yml
- https://support.knx.org/hc/en-us/articles/360022205039-Import-Add-products

Research candidates for the future lab are XKNX 3.20.0 and python-dali 0.11; those releases predate this work by more than seven days. A Python 3.12.14 Docker base digest published 2026-09-19 was inspected, but no image was built and no lab dependencies were installed. These are not configured or verified project dependencies. Pin the eventual toolchain/packages and record working commands only after the agreed native/CI workflow is implemented and tested.

