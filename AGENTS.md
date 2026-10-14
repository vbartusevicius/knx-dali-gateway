# KNX-DALI Gateway

## Project intent and current scope

Build a KNX-DALI gateway with our own maintainable, testable application, followed by electrical schematics, PCB layout, and a printable DIN-rail enclosure. Start with code and reproducible local testing before custom hardware.

The first gateway code slice now exists under gateway: a portable C++17 lighting/recovery service, validated DALI values, ESP-IDF DALI/NVS adapters, USB development console, native tests, and C6 development/release compile profiles. PlatformIO Core 6.2.0, Espressif32 6.12.0/ESP-IDF 5.5.0, and component/tool versions are pinned. Later slices added the thelsing/knx TP device stack, the ETS product source (knxprod, OpenKNXproducer in Docker), 64 channels + 16 groups + broadcast with switching, brightness, relative dimming, scenes, faults, ETS parameters, ETS-operated DALI commissioning, startup recovery, retries, button/reset/watchdog handling, REUSE licensing, and a Docker integration lab. 171 native tests (plain and sanitized) and 22 lab scenarios pass; C6 firmware compile checks pass. Still pending: full DT8, DALI-2 input devices/arbitration, network services, KiCad schematics, CI, and every hardware/ETS validation. Nothing has run on real hardware; no upload/order/publication occurred. Production MCU/power, licensing, tooling, and compliance gates remain open. See the verified implementation section below for commands, limitations, and the next slice.

## Engineering rules

### Modularity and simplicity

- Organize the repository as separate applications/components with their own source, tests, and eventual build/dependency configuration. Keep firmware in gateway, ETS product definitions/tooling in knxprod, and test-side bus models/services in simulation. Do not put all application code and tests into root-level src/core/firmware/test folders.
- Keep each component independently buildable and testable. Root CI/orchestration coordinates components and validates exported contracts; it must not create hidden dependencies on another application's implementation details.
- Apply SOLID where it improves actual cohesion, substitution, and testing, together with KISS and YAGNI. Prefer composition and explicit collaborators over deep inheritance. Do not invent interfaces, factories, generic frameworks, service locators, or dependency-injection infrastructure for hypothetical future needs.
- Give classes and files focused responsibilities and small, understandable APIs. Split nontrivial implementations into appropriate source files; group small cohesive value types when that is clearer than mechanically creating one class per file. Use free functions for stateless operations instead of forcing everything into a class.
- Keep entry points, interrupt handlers, and transport callbacks thin. Business rules belong in focused modules, not giant main files, callbacks, or application-manager classes. Add feature folders only as real implementations require them.
- Keep reusable firmware/domain logic independent of GPIO, ESP-IDF, networking, and storage details through the few meaningful adapter/test boundaries. Reuse the same core in firmware and host tests; do not maintain a simulator-only copy of the gateway logic.
- Extract repository-wide packages only when genuine reuse across components exists. Do not create a generic common/utils/shared dumping ground or duplicate source files between apps. Treat generated ETS/firmware metadata as an explicit, versioned contract, not separately maintained constants.

### C++ practices and dependencies

- Use idiomatic modern C++ supported by the selected toolchain, a consistent naming/file convention, project namespaces, const-correctness, scoped enums, named constants, and explicit units. Prefer standard-library facilities where appropriate; do not assume language features beyond the configured C++ standard.
- Use RAII and explicit ownership/lifetimes. Avoid manual owning new/delete and unnecessary shared ownership. Non-owning references/pointers must have clear lifetime and nullability semantics. Keep allocation, blocking, and synchronization compatible with the embedded timing/power budget; use bounded buffers/queues and do not allocate or perform networking in critical interrupt paths.
- Public headers should expose only the necessary API and dependencies. Put nontrivial non-template definitions in source files; keep inline/header implementation for genuinely small accessors, constexpr logic, templates, or a measured requirement. Avoid large inline implementations and implementation code dumps in Markdown responses; code changes belong in files.
- Use clear result/state representations instead of magic sentinel values, implicit conversions, unchecked casts, or duplicated mutable state. Handle errors at appropriate boundaries with the project's embedded-compatible policy; do not force exceptions/RTTI or verbose catch-all handling into every operation.
- Before writing infrastructure or protocol support, look for a maintained existing package/tool and verify its API against the selected version. Prefer suitable SDK/standard/community implementations over homegrown HTTP/MQTT/serialization, stacks, queues, logging, crypto, or test frameworks.
- A package is suitable only after checking required feature/correctness coverage, MCU/SDK compatibility, maintenance, licence/redistribution terms, resource/timing costs, and testability. Online availability or marketing claims are not enough. Pin approved versions/commits and preserve notices; do not weaken security/release-age policies to make a dependency work.
- Write custom code only for the project's actual domain behaviour, necessary integration, or demonstrated gaps that available packages do not adequately solve. Cover such gaps with independent tests; avoid blanket rewrites and unnecessary wrapper layers around good libraries.

### Comments and tests

- Code should explain itself through names, types, invariants, and small functions. Do not add routine inline comments, explanatory blocks, or comments that narrate obvious code. Comment only a critically non-obvious reason, such as a hardware erratum, protocol timing requirement, or unavoidable workaround; keep it concise. Preserve required licence notices and do not strip third-party attribution.
- Design every change with its verification path in mind. Prefer deterministic native tests for pure logic, injected time and narrow bus/storage seams, simulator integration tests for protocol interactions, and dedicated hardware checks for timing/electrical behaviour.
- Use established test frameworks/runners rather than building our own. Tests live with the component they exercise; cross-component scenarios belong in the integration layer and consume real exported contracts.
- Write failing regression tests before fixing bugs where practical. Test boundary values, malformed traffic, timeouts, concurrency, readback/fallback, and failure recovery, not just happy paths. Fakes must exercise production code, not duplicate its implementation and confirm the same mistakes.
- Review for code smells before finishing: god classes/functions, deep nesting, duplicated behaviour, unbounded work, hidden global/singleton state, inappropriate abstractions, ambiguous ownership, magic values, and dependencies leaking across layers. Refactor only where it improves the current design; do not add speculative capabilities.

## Confirmed requirements

- The product is a wired KNX TP-to-DALI gateway. KNX/IP-only over WiFi is not the product architecture.
- All normal lighting control must work with WiFi disabled, unconfigured, or unavailable. No cloud, dashboard, DHCP, or network connection may be required for gateway startup or continued operation.
- ETS is the primary provisioning and configuration tool. Physical-address programming, application download, KNX associations, DALI discovery/addressing, groups, scenes, and essential replacement workflows must be available through ETS without gateway WiFi.
- Provide continuously available WiFi dashboard and MQTT connectivity when configured; do not reduce networking to temporary maintenance windows. WiFi remains optional to the control/provisioning path, not optional to the requested feature set: KNX/DALI must continue if WiFi/broker connectivity fails or the radio is disabled for testing. Prove sustained and burst network power consumption from KNX red/black without borrowing core power from auxiliary DC.
- ETS owns the authoritative application configuration. Dashboard operations must use the shared commissioning service and explicit synchronization/revision checks rather than silently overriding ETS-managed settings.
- Use one serialized DALI commissioning service for ETS and optional WiFi clients. Network load or failures must not starve bus control, and concurrent commissioning operations must be rejected or coordinated.
- A replacement workflow must preserve logical KNX channels and group-address associations, restore the intended DALI settings, and report any configuration mismatch for explicit reconciliation.
- For startup/recovery, prefer valid ballast state readback (A) over persisted command state (B). Where a reachable device/field cannot provide reliable state, restore the valid persisted target and explicitly report fallback-only capability/source. Do not treat transient timeout, startup, or an offline bus as proof that readback is unsupported; do not replay relative commands blindly.
- Provide a physical RESET button. Distinguish a configuration-preserving device reboot from factory reset, KNX line reset, and DALI address reset.
- Power the ESP32 and all gateway-core electronics from KNX red/black through a compliant bus-coupling/power-extraction circuit. Normal gateway operation and ETS provisioning must not require the auxiliary DC input. Validate the KNX power budget for controller, interfaces, indicators, optional WiFi, and optional display; do not assume a generic DevKit or BCU regulator is sufficient.
- Target one DALI line with up to 64 control-gear devices and an optional-use integrated DALI bus supply. The auxiliary DC input powers only that internal DALI supply, not the ESP32/core. With an externally powered DALI bus, the gateway must operate from KNX red/black alone, with auxiliary screws unused and internal DALI supply disabled.
- One gateway/application controller is expected, but standards-compatible DALI-2 input devices (buttons, presence and light sensors) can share the line. They transmit events and are multi-master control devices, so do not assume the gateway is the only transmitter. Target multi-master-capable application-controller bus behaviour for event-driven inputs: 8-bit replies, 16-bit gear frames, 24-bit control-device frames, collision/arbitration/backoff, separate addressing/instances, discovery, and event handling. The first lamp-control milestone is incremental, not permission to exclude these devices from the eventual architecture.
- Begin with single-colour brightness control for incremental implementation/testing (ordinary DT6 gear or the base brightness commands of DT8 gear), but full IEC 62386-209 DT8 is the eventual protocol target, not tunable-white-only support. Build a versioned command/capability matrix covering Tc, xy, RGBWAF, and legacy primary-N where supported, including queries, configuration, activation, limits, and colour scenes. Do not imply that every ballast supports every colour type or that DT8 means all other DALI device types/standard parts.
- Provide exactly one two-pole auxiliary DC screw terminal (+/-), accepting either DC carried on the user's KNX white/yellow pair or an external 24 V DC supply on the same two screws. Do not add a separate white/yellow connector or a second auxiliary DC input.
- Connect one auxiliary source at a time; dual-input selection/ORing is not required. The DALI supply's protected input must accommodate both 24 V nominal and the 30 V auxiliary output of the user's MDT STC-1280.01. Input range, existing-load/current headroom, and reverse-polarity/transient protection still require verification. Auxiliary cable distance is approximately 0.5–1 m.
- USB is the only requested service connection for firmware flashing, console/diagnostics, and recovery. It may be inside the case; opening the case for service is acceptable. Normal USB service has KNX, DALI, and auxiliary wiring disconnected and requires a USB-powered core/recovery path. Live-system USB debugging with the case open is an exceptional use case requiring a reviewed isolation/power method, not permission to assume direct simultaneous connection is safe. Do not add dedicated UART/JTAG service connectors or require those tools. Internal UART communication with the KNX transceiver is a separate functional interface, not a user service port.
- ESP32 removability is not required. The user's earlier daughterboard suggestion was about development-board-style USB convenience. A fixed soldered ESP32 module with native USB or an onboard USB-to-serial bridge is acceptable; a DevKit is only a prototyping option, not a required socketed production footprint.
- Reserve a connector and mechanical provision for an optional small display; a populated display is not required for revision 1.
- Target a classic distribution-board DIN enclosure in the 1M or 2M width range (approximately 17.5–18 mm or 35–36 mm respectively), mounting on a 35 mm DIN rail. The user delegates mechanical selection to the agent; use a 2M working envelope for the first revision, subject to placement/isolation/thermal validation, and treat 1M as a later optimization. Do not silently exceed the user's 1–2 module expectation.
- Start physical experiments with off-the-shelf development/interface boards. Prefer custom 3D-printed enclosure prototypes after careful PCB layout and 3D review, rather than purchasing a stock molded case as the default. Establish the DIN/mechanical envelope and mounting/terminal/isolation constraints before PCB routing; finalize and pay for printed case iterations after the PCB geometry is reviewed.
- The project is personal DIY with possible small-scale Tindie/similar sales. Open-source all project-owned firmware, ETS definitions, hardware/PCB design, parametric enclosure source, BOM, and build/test tooling. Small-scale sales are commercial distribution; audit licences and applicable market requirements rather than treating DIY status as an exemption.
- Onboard 230 V is not required for the baseline. The user would consider it as an alternative, but prefers avoiding it if it adds substantial isolation/complexity. Low-voltage supply does not remove the DALI isolation requirements.

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

## Architecture principles

- Keep the gateway core independent of Arduino, ESP-IDF, GPIO, and network implementations.
- Define adapter interfaces for KNX telegrams, DALI transactions, time, and storage.
- Run the same application core in host tests and embedded firmware; do not build an unrelated simulator-only implementation.
- Use deterministic virtual time in core tests rather than real sleeps.
- Use bounded command queues, explicit timeout and retry policies, and defined startup behaviour.
- Pin dependencies and toolchains, preserve upstream attribution, and keep third-party modifications minimal.
- Choose hardware adapters after proving the core and validating candidate stacks.

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

## Multi-app repository layout

The user requested component-owned source/tests rather than a single root-level firmware/core/test application. The directory scaffold now contains:

- gateway/src: gateway firmware and its portable domain/application code. Introduce focused KNX, DALI, commissioning, configuration, network, and platform modules as their implementations appear, not as speculative class/folder scaffolding.
- gateway/src layout (user-approved): headers live next to their sources, one folder per module, no include/ directory. Includes are relative to src (for example "dali/ArcLevel.hpp"); namespaces mirror folders (gateway::dali, gateway::storage, gateway::lighting, gateway::espidf, gateway::firmware). src/espidf holds SDK adapters that must also compile natively against the test SDK fakes; src/firmware holds boot/wiring/console code that needs the real chip and is the only folder excluded from native builds (build_src_filter = +<*> -<firmware/>). Keep this rule instead of growing per-file filters.
- gateway/tests/unit: deterministic native tests of gateway behaviour and protocol contracts.
- gateway/tests/integration: host-adapter, mocked-bus, and gateway integration scenarios. Add hardware-test fixtures here when a real bench exists.
- knxprod/src: editable ETS product/application XML, native commissioning scripts, and minimal generation tooling around approved packages.
- knxprod/tests: schema, metadata, generated-header, script, package, and firmware/product compatibility checks.
- simulation/src: test-side KNX/DALI models, services, scenario drivers, and host orchestration; this is not a second production gateway implementation.
- simulation/tests: tests of simulator behaviour, fault injection, and protocol integration involving the actual gateway core.
- hardware/kicad: eventual editable electrical schematics/layout; no hardware design files exist yet.
- mechanical/enclosure: eventual parametric case source and mechanical validation; no case design files exist yet.

Build/dependency files belong with their component when implemented: gateway owns its PlatformIO configuration and ESP-IDF component/CMake files; simulation owns its Python configuration/lock; knxprod owns its generator input/configuration. Root CI and optional Compose orchestration coordinate those components. Do not create dummy build files, placeholder source classes, or generic shared packages just to fill the layout.

For local development, open the gateway as the PlatformIO project or use the IDE's standard multi-project workspace support. CI uses the same component configuration and correct working directory, not a separately maintained root firmware toolchain. Any generated firmware metadata consumed from knxprod must have an explicit reproducible export/validation path without manually copying or independently editing constants.

Keep editable sources and dependency locks in version control. Generated binaries, Gerbers, and STL/STEP exports should normally be build/release artifacts. Avoid caches and duplicated generated files; if a small checked-in generated contract is needed for a simple local build, require regeneration/consistency validation in CI. Gateway source/build/tests now exist and have verified commands below; knxprod, simulation, hardware, and mechanical directories remain scaffolding only.

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

## Verified gateway implementation

### Implemented scope

- gateway/src/dali contains bounded ShortAddress (0..63), ArcLevel (0..254, not MASK 255), and a narrow transport interface. They do not reimplement a KNX or DALI protocol stack.
- gateway/src/lighting implements individual short-address level control, cached state provenance, successful-command target persistence, read-only refresh, and readback-first recovery. Saved-target restoration occurs only on explicit Unsupported readback, never timeout/unconfigured/invalid response. Commanded/restored targets are not labelled measured. Failed transmission does not overwrite the saved target.
- gateway/src/espidf contains the Espressif DALI 1.1.0 transaction adapter and RAII NVS target store; gateway/src/firmware contains the SDK USB console, composition root, and thin app_main entry point. Hardware pins are facts of the board, not user/ETS settings: they live as constexpr values in gateway/src/firmware/Board.hpp (user decision; the earlier Kconfig options and GATEWAY_DALI_* build flags were removed). The DALI bus itself is polarity-insensitive (bridge rectifier in the interface); the invert values describe the interface circuit's logic, e.g. the MikroE DALI 2 Click optocouplers invert both directions. The store uses the targets_v1 namespace and commits accepted target changes; failed commits are retried even when the staged value is unchanged. Storage initialization failures are reported without automatic partition erasure.
- The USB development console exposes status, level, read, and recover using ESP-IDF's console implementation and standard C++ numeric parsing. This is bench/debug control, not a substitute for required ETS provisioning. Boot does not scan, commission, readdress, restore, or switch any gear automatically.
- Physical DALI defaults to disabled. Enable it only after explicitly reviewing GPIOs, polarity, interface/power/isolation, and bench wiring. C6 USB GPIO12/13 and overlapping/invalid GPIOs are rejected. No hardware was uploaded or exercised. SDK idle/interrupt watchdog and brownout settings are enabled; actual control-worker progress monitoring still needs implementation.
- LightingService is non-copyable and assumes serialized calls; the current console is the only caller. Its synchronous transport/storage operations must not be called from interrupts or independently from future network/KNX callbacks. A bounded controller worker/queue, deterministic retry/time scheduling, and coordination with input events are the next concurrency milestone, not implemented features.

### Dependencies and verification

- Local PlatformIO Core 6.2.0 and intelhex 2.3.0 are declared in gateway/requirements-dev.txt. The existing IDE-managed environment lacked intelhex, and the user explicitly approved installing only that missing dependency there; system Python was not changed.
- gateway/platformio.ini pins native platform 1.2.1, Unity 2.6.1, Espressif32 6.12.0, ESP-IDF package 3.50500.0 (IDF 5.5.0), RISC-V compiler 14.2.0+20241119, esptool package 2.40900.250804, CMake 3.30.2, and Ninja 1.13.2. dev_c6 adds lib_deps thelsing/knx at commit 980c047ad7fc5e27bf2fae95e48acde5d5e0b4fd with -DMASK_VERSION=0x07B0 -DKNX_NO_AUTOMATIC_GLOBAL_INSTANCE -DKNX_FLASH_SIZE=8192 (-Wno-error=volatile only because the pinned library uses deprecated volatile increments; our sources keep -Werror via CMake). The IDF manifest pins DALI 1.1.0, cmake_utilities 1.1.1, and led_strip 3.0.3; retain the generated gateway/dependencies.lock with component hashes. Framework-managed Python transitive packages and fully hermetic canonical CI remain additional reproducibility work.
- Fifty Unity cases pass on both native and native_sanitized (suites unit/test_lighting with 37 and unit/test_knx with 13). KNX coverage: brightness scale bounds/monotonicity/round trip, channel object numbering, group-object writes to commands, status objects. Lighting coverage adds switch on/off last-level behaviour and command dispatch, plus value bounds/MASK, independent channel state, transport and storage faults, A-before-B recovery, no fallback on missing/malformed replies, off-state persistence, NVS failed-commit retry, repeated-target write avoidance, resource ownership, and SDK DAPC/query request/error translation.
- Native tests compile the actual gateway service and adapter implementations. Test-only C API doubles live in the local library gateway/tests/support/sdk_fakes, linked into every native suite via lib_deps symlink://. The thelsing KNX adapter (src/firmware/KnxDevice) is build-verified only; it guards group-object access by the ETS-loaded table size. They do not execute Espressif's RMT engine, model TP wiring, prove flash durability under real power loss, or replace the pending knxd/XKNX/python-dali integration lab.
- dev_c6 and release_c6 compile checks pass for ESP32-C6-DevKitC-1. The enabled DALI initialization branch also compiled under temporary verification-only definitions; normal builds were restored to the disabled configuration afterward. No pin assignment from that compile check is an approved hardware design.
- SDK defaults select native USB console, 8 MB flash to match the evaluation board definition, brownout/watchdog protection, no C++ exceptions/RTTI, and CONFIG_APP_REPRODUCIBLE_BUILD. Generated full sdkconfig files are written to .pio/build/ENV/sdkconfig (board_build.esp-idf.sdkconfig_path) and managed_components/ is the component-manager download cache; both are ignored. sdkconfig.defaults is the committed source of truth; menuconfig edits are lost on clean unless copied there. src/CMakeLists.txt globs *.cpp; PlatformIO re-runs CMake only when CMakeLists/sdkconfig/lock files change, so after adding a source file touch src/CMakeLists.txt or clean. CONFIGURE_DEPENDS is deliberately not used (it breaks ESP-IDF's script-mode component scan and PlatformIO does not honour it). Artifact hash repeatability across clean builds/paths and CI has not yet been established. Local release-profile output is a compile check, not a publishable production release.

Verified commands from the repository root, using a PlatformIO terminal (or the installed environment's pio executable):

- pio test -d gateway -e native
- pio test -d gateway -e native_sanitized
- pio run -d gateway -e dev_c6
- pio run -d gateway -e release_c6
- pio run -d gateway -e dev_c6 -t menuconfig opens the standard SDK configuration UI. Keep source defaults synchronized with intentional local settings; this command does not upload firmware.

### IDE code intelligence (clangd)

- The editor uses the clangd extension at the repository root; Microsoft cpptools is listed as unwanted. clangd reads per-environment compile databases generated by PlatformIO, not hand-written include paths.
- gateway/scripts/compiledb.py (post extra script for every env) writes .pio/build/ENV/clangd/compile_commands.json and rewrites bare compiler names to absolute paths so clangd can query the RISC-V toolchain for system headers. The separate clangd/ folder is deliberate: ESP-IDF's CMake export overwrites .pio/build/ENV/compile_commands.json on every build.
- gateway/.clangd selects the dev_c6 database for src/ and the native database for tests/, removes GCC-only flags Apple clangd rejects, drops -DUNITY_INCLUDE_CONFIG_H (that header only exists after pio test), and adds the Unity include path for tests/ because test files are not in the database and clangd infers their flags from the nearest file, which may be an sdk_fakes source without Unity.
- .vscode/settings.json passes --query-driver for riscv32-esp-elf compilers. .vscode/tasks.json task "Gateway: refresh code index" runs pio run -d gateway -e dev_c6 -e native -t compiledb on folder open; rerun it (then restart clangd) after changing build flags, adding source files, or switching dependencies.
- Verify with: from gateway/, clangd --query-driver='**/riscv32-esp-elf-*' --check=FILE reports no diagnostics. The DefineOutline/SwapBinaryOperands FAIL lines are clangd self-test artifacts, not code errors.

### KNX slice decisions (user-approved, implemented and build/test-verified; not bench-tested)

- KNX stack: thelsing/knx (System B, ETS-programmable, native Esp32IdfPlatform, TP via TP-UART), pinned by commit in platformio.ini lib_deps for the ESP-IDF envs only. OpenKNX/knx was rejected because its ESP32 support requires the Arduino framework. Consequence: distributed gateway firmware is GPL-3.0-or-later; record this in licence files before any release.
- Prototype hardware for the board definition: ESP32-C6-DevKitC-1 + MikroE DALI 2 Click (3.3 V jumper, external DALI bus supply for now) + NCN5121/NCN5120 TP-UART module at 19200 baud on UART1. Board pins: DALI TX 18 / RX 19 (both inverted), KNX TX 22 / RX 23, prog button GPIO9 (BOOT), prog LED GPIO8 (onboard WS2812 via espressif/led_strip). Polarity and wiring still need bench confirmation.
- ETS application (knxprod/src/*.xml is the source of truth; OpenKNXproducer generates gateway/src/knx/generated/KnxDaliGateway.h, and src/knx/ObjectMap + src/config/Configuration static_assert against it). Objects: 64 channels (channel n = DALI short address n) × 6 = 1..384 (Switch DPT 1.001, Switch status, Brightness DPT 5.001, Brightness status, Relative dimming DPT 3.007, Fault DPT 1.005); 16 DALI groups × 5 = 385..464 (no Fault); broadcast 465 Switch, 466 Brightness, 467 Relative dimming, 468 Scene DPT 18.001. Parameters (292 bytes): StartupDelay, broadcast/group/channel switch-on mode (last level or fixed %) + fixed level, per-channel 16-bit DALI group membership (little-endian). Product IDs are provisional: manufacturer M-00FA (OpenKNX community), OpenKnxId 0xFF, application number 90 — must be checked against the OpenKNX registry or replaced by an own manufacturer ID before any distribution.
- Group membership configured in ETS is authoritative: after each configuration the worker reconciles every gear's DALI groups (query, then add/remove only differences). Group/broadcast commands update member channel states and publish their status objects.
- Persistence (NVS namespace targets_v1): arc_NN = last commanded target per short address (fallback B for recovery), aNN/gNN/b = last non-zero level per channel/group/broadcast for "switch on = last level" across reboots; one nvs_commit per operation (refresh/finish_dimming currently commit per answered channel).
- Relative dimming: any non-zero DPT 3.007 step code starts continuous dimming (deliberate simplification of step sizes): first Up step is ON AND STEP UP, then UP/DOWN every 200 ms (src/lighting/RelativeDimmer, virtual time), 10 s safety limit if the stop telegram is lost; stop/expiry reads back affected channels and derives group status from the lowest answering member. Scenes: broadcast scene object only; KNX scenes 1..16 map to DALI scenes 0..15 (higher ignored); learn = STORE ACTUAL LEVEL IN DTR0 + STORE DTR AS SCENE (each gear stores its own level); recall reads back all channels.
- Commissioning is operated from ETS: parameter page "DALI commissioning" buttons run knxprod/src/DaliGateway.script.js, which calls online.invokeFunctionProperty / invokeFunctionPropertyState (API names from public OpenKNX docs; unverified until tested in ETS). Function-property object index 160: command 1 scan, 2 assign (0 new only / 1 re-address all), 3 replace (target short address must be absent; exactly one unaddressed gear gets it, then group reconcile + recover), 4 identify (RECALL MAX; the pinned driver has no IDENTIFY DEVICE), 5 reset DALI addresses; state 1 returns phase, progress, found, last error, presence and fault bitmaps. Jobs run serialized in the DALI worker; requests while busy are answered "busy". After commissioning the adapter sends STOP QUIESCENT MODE because the driver leaves input devices quiet.
- Startup: wait ≤3 s for ETS parameters, scan presence, recover every present gear (readback first), publish statuses. Fault detection: idle worker polls one previously present gear per second with QUERY STATUS; no reply or lamp failure sets the channel's Fault object. src/dali/RetryingTransport retries reads on timeout/collision/invalid reply and writes on collision (3 attempts; commission/identify not retried).
- Buttons and resets: hardware EN/RESET reboots and preserves configuration. PROG button (sampled, 50 ms debounce): release < 2 s toggles KNX programming mode; held ≥ 5 s the LED fast-blinks; release ≥ 10 s factory-resets (erases NVS namespaces "eeprom" of the KNX stack and "targets_v1"; DALI gear addresses untouched). DALI address reset only via ETS function property 5 or console `reset-addresses confirm`. Task watchdog (30 s, panic) covers the KNX task and DALI worker.
- Mapping: switch ON restores the last non-zero level (in-memory, so after reboot with lights off ON goes to max 254); OFF sends level 0. DPT 5.001 raw 0 maps to off and raw 1..255 maps linearly to DALI arc 1..254 so the gear's logarithmic curve gives perceptually even dimming; status maps back with the inverse.
- Concurrency: DALI transactions block for tens of milliseconds and must never run inside the KNX loop (TP-UART acknowledgement timing). The KNX task (higher priority, 1 ms loop, CONFIG_FREERTOS_HZ=1000) converts group-object writes into control::Command values on a bounded FreeRTOS queue; a single DALI worker task is the only LightingService caller and returns StatusUpdate values on a second bounded queue that the KNX task publishes. The USB console also submits through the command queue.

### Pending-slice decisions (user)

- DALI-2 input devices are deferred until after DT8 and networking. They will need a bus layer that receives unsolicited 24-bit event frames and handles collisions/backoff, which espressif/dali 1.1.0 does not provide.
- Wi-Fi credentials are provisioned through a temporary SoftAP setup portal (unconfigured state or a button gesture); KNX/DALI keep running while it is active.
- Schematic authoring tool (KiCad files vs SKiDL vs atopile) is still being discussed with the user.

### DT8 and own DALI bus layer (implemented, not bench-tested)

- DT8 (src/dali/ColourControl): colour temperature (mirek), xy, primary N, RGBWAF, Tc step cooler/warmer, Tc limits, colour status/type features/value queries (MSB in reply, LSB in DTR0), COPY REPORT TO TEMPORARY before scene store so scenes capture colour. Opcodes/selectors verified against IEC 62386-209 tables and python-dali; KNX DPT 7.600/232.600/251.600/242.600 layouts verified against xknx 3.20.0. Capabilities come from QUERY COLOUR TYPE FEATURES at scan; colour commands to a short address lacking the type return Unsupported without sending. RGB/RGBW: brightness = max component (DAPC), colour channels normalised to 254; validity bits honoured. Colour is not persisted (gear keeps it; recovery reads back). KNX: per channel 15 objects, per group 14, broadcast 9 → 1193 objects (before input channels); ETS "Colour control" parameter only drives object visibility.
- espressif/dali was replaced by our own multi-master bus layer: portable src/dali/bus (Manchester encoder/decoder, LinkLayer with priority settling times, echo-based collision detection, break/retry, backward-frame window, reply collisions reported separately so COMPARE counts them as "yes", unsolicited frames delivered as bus events) + src/dali/GearAddressing (Part 102 random-address commissioning) + src/espidf/RmtDaliBus (RMT TX/RX continuous capture, dedicated bus task) + BusTransport. Timing constants in src/dali/bus/Timing.hpp from IEC 62386-101 ed2 figures and Microchip AN1465; the collision break duration is an implementation choice. Collision retries live in the link layer (and the lab transport for forward collisions only); RetryingTransport retries timeouts/invalid replies, so retries do not multiply. Echo comparison happens per frame (not a bit-level abort), which is a known gap versus full DALI-2 collision behaviour; all RMT timing is unverified without an oscilloscope.
- Unassociated status objects are not published (previously they went to GA 0/0/0).
- DALI-2 input devices (src/dali/devices): Part 103 24-bit frames and special commands, DeviceAddressing (own 0..63 address space), DeviceInventory (instances; every instance gets event scheme "device/instance" and a type-default event filter, only ETS-bound instances are enabled), EventDecoder for 301 push buttons, 303 occupancy, 304 light (10-bit value × QUERY RESOLUTION step). Verified against python-dali dali/device/*.py and IEC 62386-103/301/303/304 material. KNX: 32 input channels (parameters: device short address, instance number, type, button function toggle/on/off/toggle+dim/scene, scene number, light threshold) with 4 objects each (Switch/Occupancy DPT 1.001 with toggle feedback, Dimming DPT 3.007, Illuminance DPT 9.004, Scene DPT 17.001) → 1321 objects total. Flow: bus task → unsolicited event queue → worker (decode + InputMapper) → KNX task publishes. Function properties 6/7 scan/assign input devices; state property 2 reports input devices. Quiescent mode during gear commissioning suppresses events. Unverified on real devices.

### Integration lab (implemented)

- Entry point: ./simulation/run-tests.sh (docker compose -f simulation/compose.yaml; fresh stack per run, torn down afterwards). 22 pytest scenarios, ~26 s: switching/brightness, group and broadcast fan-out, parameters and group reconciliation, relative dimming, scenes, commissioning via function properties, faults, retries with injected timeouts/collisions.
- gateway-lab container builds PlatformIO env `lab` (Linux only): the same application code (src/knxstack/KnxApplication shared with firmware/KnxDevice, shared worker logic, LightingService, Commissioning) on thelsing LinuxPlatform with KNX IP mask 0x57B0 over routing multicast, and SocketDaliTransport encoding frames with src/dali/Frame (vectors checked against IEC 62386-102 addressing). knxd is not used; xknx talks routing directly. TP-UART timing and the ESP-IDF DALI driver are not exercised by the lab.
- dali-sim: python-dali 0.11 fakes behind a JSON-over-TCP protocol plus a control API for fault injection (SimGear adds the missing STORE ACTUAL LEVEL IN DTR0 handling). knx_programmer performs an ETS-like download with xknx (individual address in prog mode, load-state machine per table object, relative allocation, memory writes, restart); table contents are synthesized from the generated header and tests/scenario.py. Group-object descriptor layout for the pinned stack: size code in the low byte, priority in bits 9:8 (0x0300 = low).
- Native env applies -Werror only to project sources (build_src_flags). .clangd maps src/lab/ to the native database with lab defines.

### Tooling, licensing, and product data

- User rule: tools that are not part of the PlatformIO toolchain (.NET/OpenKNXproducer, reuse, future KiCad CLI, Wine experiments) run through Docker/compose, never installed on the host.
- knxprod: knxprod/compose.yaml service `producer` (debian:trixie-slim + sha256-pinned OpenKNXproducer 4.3.12 native Linux binary). knxprod/scripts/generate.sh regenerates the gateway header (filtering the tool's self-referential getGroupObject macros); knxprod/scripts/check.sh fails when the committed header is stale. Without ETS the tool runs checks and header generation and skips .knxprod packaging; packaging/signing needs ETS DLLs (Windows ETS, possibly Wine later with a user-supplied ETS installation).
- Licensing (user decision): one root LICENSE.md maps each folder to its licence by SPDX identifier with links, no full texts and no per-folder LICENSE files (gateway GPL-3.0-or-later because of thelsing/knx; knxprod, simulation, docker MIT; hardware, mechanical CERN-OHL-P-2.0; docs CC-BY-4.0). No per-file SPDX headers, no REUSE tooling. Third-party notices (firmware components, KiCad libraries, tools) are a section of the same LICENSE.md; no separate notices file.
- Docker (user decision): one folder per image under docker/ (eda, cad, knxprod, lab-gateway, lab-python, lint, mcp, stl-viewer, kicanvas), each Dockerfile built from the repo root by the one root compose.yaml with profiles (knxprod, lab, hardware, mcp, lint, viewers). eda carries KiCad 10, SKiDL and Freerouting 2.5.0 (its JRE stage is pinned to linux/amd64: on arm64 hosts an unpinned stage copies an aarch64 java into the amd64 KiCad image); there is no separate router image. mcp builds FROM the eda image through a compose additional build context. Viewers download pinned third-party code at image build (three.js npm tarball, KiCanvas from a pinned commit, both SHA-256 checked); nothing third-party is vendored. `docker compose --profile viewers up` serves KiCanvas on :8082 (hardware/layout/v1) and the STL viewer on :8081 (mechanical/build).
- Hardware variants (user decision, supersedes the entries below; design in hardware/docs/variants.md, envelope drawing in hardware/docs/din-envelope.svg): POC 3 DIN / 2 boards / 2 layers; second 2 DIN / carrier + KNX and DALI sub-boards / 2 layers; golden 2 DIN / 2 boards / 4 layers with a populated LCD/OLED (1 DIN is a stretch goal). UI board always separate in the front protrusion. MCU: Seeed XIAO ESP32C6 on headers, powered from KNX. DALI supply 12-20 V, optimised for size. Envelope (user measurement): per module 18 wide, hidden 81 tall x 44 deep (rail excluded), front 45 tall x 17 deep.
- Hardware v1 (released in hardware/releases/v1, user decision 2026-10-11): 3 DIN POC, main board 4 layers (In1 solid ground split into SELV-KNX / DALI / SELV-AUX planes) + UI board 2 layers with a populated 0.96" SSD1306 OLED. DALI supply = RECOM REM3-2415S/A + two-transistor limiter (~180 mA), enabled by two jumper caps on JP1 (no adequately rated slide switch in stock). Buttons (user): PRG/RST tap = reboot, hold 5 s = KNX programming mode; FUNC tap = display page/wake, hold 5 s = WiFi setup; both 10 s = factory reset (gateway/src/device/FrontPanel, ladder 20k/10k so both-pressed decodes). LEDs (user): three SK6805-EC15 RGB in one chain on LED_DATA (XIAO D0) via a 5 V SN74AHCT1G125 on the UI board, always lit (device/StatusLeds); +5V reaches the UI on the former LED_KNX cable wire (the only main-board change; XIAO D1 now spare); they shine through a 0.6 mm skin of the front wall inside one baffle tube each; LED driver parts sit under the OLED. C&K PTS645SL70 (7.0 mm), 1.4 mm proud of the front; OLED glass flush with the front (UI board z 53.8, mechanical/scripts/ui_stackup.py). Main board top at z 21.2; shoulders 1.3 mm above the tallest connector. Connectors flush with the faces the wires come from (user): DALI/AUX terminal wire-entry faces in openings in the top face (J2/J3 pins at y 74.75, 0.2 mm short because pads need 0.3 mm edge clearance), KNX plug underside flush with the bottom face (J1 at y 7.25); terminal screws reached through holes in the shoulder front; JP1 stays covered (user). Keep >= 2 mm air between other parts and the case (spec checks.component_air_gap). Case = base (5 mm deep back with a TS35 channel, fixed top hook, sprung sliding latch on a parallelogram flexure whose tab sticks out below the case with a front-facing screwdriver hole (user); KNX plug in a recess open to front and bottom; 5 legs) + cover (shoulder plates with 3 pegs, never under the protrusion where no plate exists; every printed part must be one solid, two screw tabs behind the top and bottom walls, protrusion with 4 UI standoffs and 4 sprung snap tongues cut into the side walls, the UI board pushed in from behind), joined by 2 thread-forming 2.5 x 10 screws entering along y from the wire sides (user: no PCB changes for fastening). Contact points live in layout_<board>.yaml `case:` and become KiCad rule areas; off-board parts (JST A10SR10SR30K102A 1:1 cable, sockets, jumper caps, screws) in hardware/product/assembly_parts.yaml. Renders use VTK offscreen (libosmesa6 in the cad image). KNX pin strip J1 in the bottom-right corner; UI connector J4 next to C4. Main J4 and UI J1 are top-entry JST BM10B-SRSS-TB (C160396), so the cable runs straight between the boards (the right-angle version on the UI board opened towards its edge, 0.65 mm from the case wall). Board renders with labelled parts: mechanical/build/v1/renders/{main,ui}_{top,bottom}.png.
- Hardware pipeline (Docker, `docker compose --profile hardware run --rm -T eda "<cmd>"`; the eda/cad images use `bash -c`, so pass one quoted string):
  1. `python3 hardware/scripts/gen_circuits.py` — SKiDL circuits → build/v1/<board>/{<board>.net, parts.json}. Every part carries LCSC + Assembly (jlc/hand); R/C get LCSC numbers from common.PASSIVES (verified against the JLCPCB API); an unknown passive stops the build.
  2. `cd hardware/scripts && python3 layout.py <board> sync|place <refs>` — the stored layout hardware/layout/v1/<board>/<board>.kicad_pcb is the source of truth; sync re-imports the netlist and keeps positions, tracks and reference visibility by ref/net; frame (outline, slot, keep-outs, pours, rules, net classes by domain region) comes from circuits/variants/v1/layout_<board>.yaml.
  3. `layout.py <board> route [attempts]` — export DSN (pours and plane layer left out, net classes rewritten), Freerouting until a run reports 0 unrouted (not repeatable run to run), import SES, prune ground tracks the pours cover. Routing is a developer step; the routed .kicad_pcb (+ .kicad_pro/.kicad_dru written next to it) is committed. `layout.py <board> components` re-exports part data from the committed board without changing it (CI uses it).
  4. `python3 hardware/scripts/render_board.py <board> <label> [--placement]` — KiCad DRC + realistic SVG/PNG views (top, bottom, in1, in2, overlay, zooms) in build/v1/<board>/render/<label>/; red = DRC errors/unrouted.
  5. `layout.py <board> tidy-silk`, then `python3 hardware/scripts/release.py <version>` (refuses boards with DRC errors, unrouted items or unsourced parts).
  6. cad container: `python3 mechanical/scripts/build_enclosure.py && python3 mechanical/scripts/render_assembly3d.py && (cd mechanical/scripts && python3 fit_drawing.py)` (reads components_<board>.json and the board STEP files written by sync/release; fit_front/fit_side.png are the drawings to review: openings over real part outlines, side section with heights and posts). Then rerun release.py to copy the enclosure into the release.
  Tests: eda `python3 -m pytest hardware/tests mechanical/tests/test_fit.py mechanical/tests/test_ui_stackup.py --ignore=hardware/tests/test_step_geometry.py` (145 pass), cad `python3 -m pytest mechanical/tests/test_enclosure_probe.py hardware/tests/test_step_geometry.py`.
  Variants: every step above takes `HARDWARE_VARIANT=<name>` (`docker compose ... run -e HARDWARE_VARIANT=v2 eda ...`; default v1). The variant spec hardware/circuits/variants/<name>/spec.yaml is merged over hardware/product/spec.yaml (circuits/product_spec.py); boards, layouts and build output live under hardware/{layout,build}/<name>/, the enclosure under mechanical/build/<name>/. hardware/tests/board_variant.py gives tests the active variant's boards; DRC/unrouted, layout↔netlist, board-link pin maps (spec `interconnects[].refs/signals`), isolation (every board with >1 domain, isolators from the layout frame) and DRC-rule probes run per variant. mechanical/tests/test_board_stack.py checks stacked boards against each other's parts and that cable corridors cross no third board. `layout.py <board> unroute` drops all tracks before rerouting a moved placement.
- V2 (2 DIN, revision B, release hardware/releases/v2, draft-unvalidated): three 4-layer boards. Main 31.5×76.5 (KNX, 5 V buck, RECOM DALI supply, terminals, JP1, J4 cable to UI, sockets J6 logic / J5 DALI). Interface 31.5×27.5 (isolated DALI TX/RX + bridge) is a daughterboard plugged onto main via its back-side male headers J1/J2 (8.5 mm Kinghelm sockets + 2.5 mm header plastic = 11.0 mm, interface top z 30.6), no case retention; isolation slots under both optocouplers. DALI_TX/RX travel main↔UI on cable pins 7/8 (only one cable). UI 31.5×40.5: XIAO on the back, Winstar WEA012864DWPP3N00003 OLED (VCC,GND,SCL,SDA; drawing WEA012864D-03 p.3), placed 1.3 mm higher than before; the OLED rests face down in the cover pocket and is soldered after the UI board snaps in (spec front_ui.oled mounting: pocket, header_gap 3.13). C4 is hand-fit lying down (gateway_fp:CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent + generated STEP; leads bent 90° 3 mm from the body, axis 7 mm under the board) in two base cradles. Tests: B2B pins coincide pin-for-pin in enclosure coordinates and the stack seats (test_board_stack), cable nets match spec signals, isolation main DALI-KNX 6.30/DALI-AUX 6.12, interface 6.76 mm. KNX bus plug for the Phoenix PST 1,0 pin strip: Phoenix PTS 0,5/2-PH-5,75 BKRD (WAGO 243-211 is for 0.8 mm pins). CI runs the hardware workflow as a v1/v2 matrix.
- XIAO footprint (fixed, both variants): pins anticlockwise from the USB end (1-7 = D0-D6 down the left, 8-14 = D7, D8, D9, D10, 3V3, GND, 5V up the right); the earlier draft had the right column reversed (5 V on D7). Use the Pre-Soldered XIAO (Seeed 102010636; the bare 113991254 has no headers) in two 8.5 mm sockets; module sits 11.0 mm above the board (3D offset 11.0, height 14.5); its courtyard is the pad field only, layout.py body_box() uses the Fab outline (RAISED_BODIES) for the enclosure/stack boxes. Both variants use the Winstar OLED (spec front_ui.oled in the base spec).
- Every part is verified, hand-fitted ones included (user requirement after the XIAO and SK6805 errors): jlc_parts.check_board fits each footprint against its JLCPCB/EasyEDA footprint (part LCSC, or `footprint_ref` in src() for parts bought elsewhere, e.g. XIAO C9900124963) and compares our symbol's pin names with the EasyEDA symbol's (name_problems, aliases in PIN_ALIASES); parts with no JLCPCB record (REM3, Phoenix 1574200, Winstar OLED) need a cited pinout in hardware/product/pinouts.yaml (pads + names or nets). parts.json carries symbol pin names; jlc_reference.json stores pin_names; snapshot covers every variant's parts (`jlc_parts.py snapshot [--cached]`). No per-component test files: checks live in test_jlc_assembly.py. The SK6805-EC15 used KiCad's LED:SK6805 symbol (other package numbering) on a renumbered footprint: copper right, but the fitted CPL rotated it 180 deg (reverse supply); now gateway:SK6805-EC15 + datasheet pad numbers.
- Variant names match release names (user): v1 (formerly "poc") and v2; hardware/circuits/variants/v1, hardware/layout/v1, build/v1. `release.py <version> [<board>...]` re-releases only the named boards (user: do not re-render everything for a local change).
- Front legend top shoulder (user choice; AUX input printed 20-32 V: REM3 UVLO 18 V max + SS16 drop below, 33 V PTC/TVS above): pin row (DA DA / - +), "PSU 20-32V" and "BUS" centred under their pairs, one bracket around all of it (short legs up to the name row) with "DALI" set into its bar so the supply reads as the DALI bus supply, not device power; one type size, fixed rows (front_legend.dali_terminals). legend_color_model.py writes cover_color.3mf + cover_color_obj.zip for WJP full-colour resin (JLC3DP): legend inlaid 0.3 mm as a separate black body sharing faces with the cover (test checks volume removed == inlay volume). WJP caveats: walls < 1 mm (LED skins, baffles, snap tongues) are below JLC3DP's guideline.
- V3 (1 DIN, `HARDWARE_VARIANT=v3`, `hardware/releases/v3/`): consolidated to **two boards** (`main` + `ui`), placed, routed (0 DRC, 0 unrouted) and released, with the 1-DIN enclosure (`mechanical/build/v3/` and `hardware/releases/v3/enclosure/`). `main` (39.8 x 78.0 mm, 1.2 mm, two-sided vertical PCB along the left wall) carries all three push-pin connectors (`J1` KNX red/black at bottom-front flush with y=0, `J2` AUX yellow/white at bottom-rear flush with y=0, `J3` DALI push-in at top-front flush with y=81, all with crafted right-side pockets and 30-deg half-butterfly dovetail keyways), `C4` (470 uF SMD cap), `L1` (220 uH inductor), `U8` (XIAO on 5 mm sockets with the 5 V buck underneath, directly under `U3` REM3-2415S/A/CTRL), the three optocouplers (with milled slots under `U5/U6`), and `J4` (the single 8-pin 0.5 mm FFC to `ui`), while the back side (-x, inside the THT solder-tail shadow) holds the low SMD parts (`NCN5130` + crystal + passives, and the DALI TX/RX passives & transistors). `ui` (13.8 x 40.5 mm) uses the Crystalfontz `CFAL12832D-B` 30.0 x 11.5 mm 0.91in 128x32 OLED plugged into a JLC-assembled 12-pin 0.5 mm ZIF FPC connector (`C2856799`) on the back (zero hand-soldering), with generous spacing to the two protruding buttons (`y = 6.0`) and the three RGB LEDs below them (`y = 2.0`).
- Planned v1/v2 upgrade (user, not done): REM3-2415S/A -> REM3-2415S/A/CTRL plus v3's supply-off circuit on XIAO D1; see hardware/docs/variants.md.
- V3 (1 DIN, `HARDWARE_VARIANT=v3`, `hardware/releases/v3/`): consolidated to **two boards** (`main` + `ui`), placed, routed (0 DRC, 0 unrouted) and released, with the 1-DIN enclosure (`mechanical/build/v3/` and `hardware/releases/v3/enclosure/`). `main` (39.8 x 78.0 mm, 1.2 mm, two-sided vertical PCB along the left wall) carries all three push-pin connectors (`J1` KNX red/black at bottom-front flush with y=0, `J2` AUX yellow/white at bottom-rear flush with y=0, `J3` DALI push-in at top-front flush with y=81, all with crafted right-side pockets and 30-deg half-butterfly dovetail keyways), `C4` (470 uF SMD cap), `L1` (220 uH inductor), `U8` (XIAO on 5 mm sockets with the 5 V buck underneath, directly under `U3` REM3-2415S/A/CTRL), the three optocouplers (with milled slots under `U5/U6`), and `J4` (the single 8-pin 0.5 mm FFC to `ui`), while the back side (-x, inside the THT solder-tail shadow) holds the low SMD parts (`NCN5130` + crystal + passives, and the DALI TX/RX passives & transistors). `ui` (13.8 x 40.5 mm) uses the Crystalfontz `CFAL12832D-B` 30.0 x 11.5 mm 0.91in 128x32 OLED plugged into a JLC-assembled 12-pin 0.5 mm ZIF FPC connector (`C2856799`) on the back (zero hand-soldering), with generous spacing to the two protruding buttons (`y = 6.0`) and the three RGB LEDs below them (`y = 2.0`).
- Renders: board views are SVG only, opening at 2.25x (render_board.SVG_ZOOM); fit drawings SVG only; the front legend is a 1:1 print file (SVG + PDF). Enclosure renders stay PNG (VTK raster).
- KiCad 10 SWIG lesson (do not undo): a PCB_TRACK removed from a board must stay referenced for the whole process (layout.py _DETACHED); once Python frees one, every later proxy (GetTracks, Footprints, LoadBoard) degrades to SwigPyObject and the process segfaults.
- Front legend: mechanical/scripts/front_legend.py writes mechanical/build/<variant>/front_legend.svg/.pdf/.png (1:1 front view, text as outlines, groups print-*/guides-*), placed from the same opening data as the cover; test_front_legend.py; release copies it into enclosure/. User chose artwork only (no engraving). jlc_parts snapshot references every variant's parts (generate both first); live release checks fetch each LCSC once per run with 15 s-step back-off. Remaining: bench validation, first prints.
- Fabrication checks (user: "pay for PCBs, enclosure and parts with confidence"): hardware/scripts/jlc_parts.py fetches JLCPCB's part record and the EasyEDA footprint for every JLC part (cart.jlcpcb.com getComponentDetail; easyeda.com/api/products/<C>/components needs a browser User-Agent and Accept: application/json, and both answer 403 when hit too fast, so requests are throttled, retried and cached in hardware/build/jlc/), checks MPN, value, voltage rating and stock, and fits JLCPCB's footprint onto ours in quarter turns so every pad centre lands inside a pad of the same pin and every pin of ours is covered; bottom parts mirror JLCPCB's footprint about its x axis. The fitted rotation/centre feed the CPL (KiCad's raw orientation was wrong for 19 parts incl. NCN5130, SOT-23s, LEDs, connectors). Different pad numbering only through cited entries in hardware/product/jlc_pad_maps.yaml (DF06S bridge, PTS645 switch). release.py refuses a board on any failed check and writes <board>_assembly_check.csv; test_jlc_assembly and test_release_current (committed release == current boards) run in CI.
- Supplier CI correction: JLCPCB/EasyEDA 403 responses caused nondeterministic CI failures despite retries. check_board defaults to offline, using hardware/product/jlc_reference.json (48 LCSC records: supplier MPN, electrical attributes, numbered footprint pads, source URLs/notices; no stock). Refresh explicitly in eda with `python3 hardware/scripts/jlc_parts.py snapshot`; `snapshot --cached` captures all existing downloads without network and fails if any are missing. Review/commit reference changes when adding or changing parts. CI test_release_current passes live=False; release.assembly_checks defaults live=True, bypasses stale cache, requests fresh records/footprints/stock, and fails closed on API errors or stock shortages. Regression tests cover blocked APIs, missing references and stale caches. Full 145-test EDA/fit suite passes with Docker --network none and an empty tmpfs hiding hardware/build/jlc; fabrication files unchanged.
- C++ style (user): gateway/.clang-format (110 columns, Google-based, includes regrouped) and gateway/.clang-tidy (misc-include-cleaner = unused/missing includes, bugprone, performance, modernize/readability subsets; unchecked-optional-access and exception-escape off with reasons) gate through `docker compose --profile lint run --rm cpp-lint gateway/scripts/check_cpp.sh [--fix]` and the lint workflow; covers the native and lab compile databases (src/firmware, src/espidf get clang-format plus GCC -Werror). clangd errors in the IDE mean a missing compile database: `pio run -d gateway -e dev_c6 -e native -t compiledb` (the VS Code folder-open task).
- Versioning and pipeline (user decision): versioned = sources (circuits, libraries incl. the 3D placeholder STEPs generated by hardware/circuits/lib/gateway_3d/generate.py, layout YAML, routed boards with their KiCad project files, scripts, tests, docs) and hardware/releases/<version>/ (frozen snapshots, user choice). Built locally and ignored: hardware/build/, mechanical/build/. GitHub Actions (.github/workflows): lint (ruff), hardware (gen_circuits, `git diff --exit-code` on generated sections, `layout.py components`, hardware + fit tests including test_boards_routed = DRC 0 errors / 0 unrouted on the committed boards, enclosure build + probes, renders uploaded as an artifact), lab (simulation/run-tests.sh). Python tool config lives per component (hardware/, mechanical/, simulation/ pyproject.toml; gateway/scripts/ruff.toml), never in the root.
- Freerouting/KiCad lessons (do not undo): KiCad 10's DSN export puts all nets in the default class (layout.py rewrites the class section from the .kicad_pro); with plane polygons in the DSN Freerouting 2.5.0 treats any via/THT pin inside a plane as connected and reports unfinished nets as routed (pours are left out and the plane layer is removed from the DSN; KiCad still clears through vias in In1); a single invalid rule (e.g. silk_edge_clearance) makes KiCad ignore the whole .kicad_dru silently (test_drc_rules plants a violation to prove the rules run); SKiDL symbol pin names must map to footprint pad numbers (test_consistency checks every netlist pin; generic Q_*_BCE symbols were wrong for SOT-23 BC847/857, the project SCT2400 symbol had the wrong pinout, D_TVS/LED pin order reversed polarity).
- KiCad MCP servers (local, no AI services): image `knx-dali-gateway-mcp` from docker/mcp/Dockerfile (FROM the eda image) (`docker compose --profile mcp build mcp`), registered in .devin/mcp_config.local.json (gitignored, relative `-v .:/work` mount; start Devin from the repo root) as stdio `docker run -i` servers: `kicad` = mcp-server-kicad 0.20.1 (ProductOfAmerica, MIT, 109 tools, file-based edits + kicad-cli DRC/exports, update_pcb_from_schematic, autoroute_pcb) and `kicad-pcb-tools` = KiCAD-MCP-Server v2.8.2 (mixelpixx, MIT, 244 tools, pcbnew/SWIG worker, Freerouting 2.5.0 + Java 25 inside the image, check_clearance/courtyard tools). The second needs ~1 min after start before its Python worker answers (amd64 emulation). Its npm dependency tree reports audit warnings; it runs only inside the container with the repo mounted.
- Tool versions (M1 upgrade, pinned): Freerouting v2.5.0 (build-date 2026-10-05, SHA-256 f6f51bb02245e8e717f9359bd260cc9c5c0b1bc0acc8b7cb2cd5b8ffeb5de3c7, JRE 25 eclipse-temurin:25.0.4.1_1-jre). KiCad 10.0.6 via kicad/kicad:10.0 Docker image (amd64 emulation on arm64 — no official arm64 tag). kinet2pcb 1.1.4 has a fp-lib-table quoting bug (KiCad 10 uses quoted attribute values; kinet2pcb compares type before stripping quotes → all stock libraries skipped). Workaround: docker/fix_fp_lib_table.py strips quotes from the fp-lib-table at image build time. KiCad 10 API change: ZONE.SetDoNotAllowCopperPour() removed → use SetDoNotAllowZoneFills().
- POC electrical design (supersedes earlier power notes; details and sources in hardware/docs/bom-design.md):
  - NCN5130 strictly per datasheet Figure 12 / Table 6: 16 MHz crystal (mandatory), R1 27 Ω 1 W TX shunt, C1 47 nF from the protected bus node to CCP, C2 220 nF CEQ1–CEQ2, C3 100 nF CAV, SS16 reverse-polarity + 40 V TVS, FANIN 10 kΩ (40 mA bus load), 9-bit UART 19200 straps (UC1/UC2/MODE/TREQ to GND), DC2 unused (VDD2MV to VDD1), V20V tied to VFILT.
  - Energy storage is on VFILT (470 µF/35 V); SAVEB at VFILT < 14 V, RESETB < 12 V. A ≥ 40 V-input buck (SCT2400) makes 5 V from VFILT, enabled only above ≈ 17 V so it cannot stall NCN start-up; LM66100 ideal diode into the XIAO 5V pin.
  - XIAO header numbering is counter-clockwise (1–7 = D0–D6, 8–11 = D7–D10, 12 = 3V3, 13 = GND, 14 = 5V). EN is not reachable through headers: UI RESET is a firmware reboot via an ADC button ladder on D2 (PROG + RESET); hard reset = XIAO button or power cycle.
  - DALI TX is fail-safe (MCU off/reset → bus released; GPIO high = bus low) with a bootstrap gate supply; DALI RX: bus high → DALI_RX low (inverted). Internal DALI supply switched by a DPDT (both poles) so it has a return path; terminals become DA+/DA− when it is on.
- Firmware follow-ups from the POC hardware: XIAO pin map D0 LED_PROG, D1 LED_KNX, D2 button ladder (ADC), D3 LED_DALI, D4/D5 I2C, D6/D7 KNX UART (19200 8E1), D8 DALI_RX (inverted), D9 NCN_SAVEB, D10 DALI_TX (non-inverted); buttons become ADC-ladder decoding; XIAO pin map (KNX UART on GPIO16/17, DALI TX 18 / RX 19, LEDs 0/1/2, PROG 21, I2C 22/23) must replace the DevKit Board.hpp; WiFi must use modem sleep (DTIM) — continuous RX leaves only ~13 % KNX power margin at a 21 V bus.
- Order (user decision): finish hardware, then lint + architecture refactor (gateway/docs/architecture-review.md), then DALI user options/gear tuning, then networking.
- .knxprod (user: from the pipeline, ETS5 + ETS6): knxprod/scripts/package.sh = OpenKNXproducer `create -d` (expanded XML + header) -> insert empty ApplicationProgram Hash (OpenKNXproducer writes none, knx-rs-prod only fills an existing one) -> knx-rs-prod 0.10.1 (metaneutrons/knx-rs, GPL-3.0, pure Rust, built from the SHA-256-pinned release in docker/knxprod) hashes, patches the fingerprint into all ids, splits, signs, embeds knx_master.xml (project-20, SHA-256-pinned from update.knx.org) -> verify_knxprod.py (tests in knxprod/tests). One project/20 file serves ETS 5.7 and ETS 6. Signing key = the RSA key inside ETS's Knx.Ets.XmlSigning.dll (same in every ETS), taken from the user's own ETS, never committed/baked; local ETS_SIGNING_KEY_FILE, CI secret ETS_SIGNING_KEY (push: signed, PR: unsigned, v* tag: required + attached to the release). Details: knxprod/docs/knxprod-pipeline.md. The OpenKNXproducer+ETS-DLL route (needs MBs of proprietary DLLs and .NET-Framework assemblies on Linux) was rejected for CI.
- ETS signing update (supersedes the previous entry): tested on this Apple Silicon host using Windows OpenKNXproducer 4.3.12 under Wine 10 in an amd64 Docker container, with the user's ETS 5.7.298.38537 libraries mounted read-only; no private-key extraction. Native Linux signing reaches DirectorySigner but fails on shlwapi.dll. Wine must keep built-in mscoree enabled (WINEDLLOVERRIDES="mscoree=b;mshtml="); disabling it causes a misleading XmlSigning.dll FileNotFoundException. `docker compose --profile knxprod build producer producer-wine`, then `ETS_CV_DIR=knxprod/ETS knxprod/scripts/package.sh` generates build/KnxDaliGateway.knxprod with Catalog, Hardware, application hash/fingerprint B812, project/20 master, and manufacturer signature. prepare_xml.py removes only upstream generated help/icon placeholders and adds an empty Hash if absent; 11 tests + header freshness + ruff pass. Verifier is structural, not cryptographic verification or an ETS import test. XSD absent: Wine uses -N; actual ETS 5.7/6 import/download still outstanding. CI accepts knxprod/ets-cv.zip.gpg with secret ETS_CV_PASSPHRASE, decrypts only on push, cleans temporary DLLs; encrypted bundle/secret not provisioned and workflow not run. Raw DLLs, plaintext bundle and credentials never enter images/git. Existing supplied-key route remains optional.
- Architecture review and refactoring plan: gateway/docs/architecture-review.md (variant-per-domain commands + visitor dispatch, LightingService split, value types, interfaces instead of void* callbacks, shared worker, table-driven KNX layout, folder/namespace restructure, lint gates, user tuning options). Arduino analysis: gateway/docs/arduino-feasibility.md (recommendation: stay on ESP-IDF; Arduino does not change the GPL/blob situation).
- User directions (latest): remove per-file SPDX headers; centralize all Dockerfiles in one docker/ folder and one root compose file (including KiCad in a browser and an STL viewer); prefer extended-actuator buttons and LEDs visible directly through the enclosure over light pipes or custom button extensions; add PCB power/logic tests and PCB–enclosure integration tests; leave web server/MQTT/Home Assistant for the final part.
- Dependency injection: manual constructor injection with composition roots in firmware/Application.cpp and lab/main.cpp; no DI container. Collaborators are constructor dependencies, not per-call parameters.

Still pending: .knxprod packaging with ETS, ETS programming on real TP hardware, full DT8 matrix, 24-bit input events and multi-master arbitration, dashboard/MQTT/Home Assistant (final part), bus-power and physical timing validation, and tested CI release publication. Product IDs remain a release gate. Do not claim these are covered by the current tests or development image.

## Known local environment

Initial checks on 2026-10-09:

- Host: macOS, ARM64.
- Workspace was empty before this file was created.
- Git is installed.
- Docker and Docker Compose are available; the Docker engine reports Linux/aarch64.
- Python 3 is available on PATH.
- `cmake`, `pio`, `kicad-cli`, and `openscad` were not found on PATH during the initial check.
- PlatformIO Core 6.2.0 was subsequently found in the IDE-managed ~/.platformio/penv environment. Its CLI is not on the initial shell PATH; use a PlatformIO IDE terminal or that environment's pio executable. Native tests and C6 builds are verified below. Compiler warnings are errors for project C++ code; no standalone lint command has been established.

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
