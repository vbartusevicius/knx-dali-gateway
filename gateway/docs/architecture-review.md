# Gateway architecture review and refactoring plan

Status: review of the current code (after slices C1–C5b) and a proposed plan. Not implemented yet.

## 1. Findings

| # | Finding | Where | Why it matters |
| --- | --- | --- | --- |
| 1 | One 24-alternative `Command` variant and a central `execute()` with a 24-branch `if constexpr` chain; `address_of()` repeats the same type list | control/Command.* | Every new command touches the central dispatcher; unrelated domains (lighting, dimming, commissioning, inputs) share one function and one result type; no compile-time guarantee that each command has exactly one handler |
| 2 | `LightingService` is a god class (~1050 lines incl. header): brightness, groups, scenes, colour, dimming finish, persistence, last-active, switch-on policies, reconciliation | lighting/LightingService.* | Hard to read, test and extend (the upcoming per-channel tuning would make it worse) |
| 3 | Missing value types: colour values travel as loose integers and bit masks (`red, green, blue, white, validity`, `x, y, xy_valid, brightness, brightness_valid`, raw Kelvin) | control, lighting, knx | Easy to swap arguments; validation repeated in several places |
| 4 | Missing "target resolution" abstraction: `as_short() ? … : as_group() ? … : broadcast` repeated; member fan-out computed in several functions | lighting | Duplicated logic for "which channels/groups does this address affect" |
| 5 | 47 C-style callbacks (`void (*)(…, void* context)`) | dali/devices, commissioning, firmware, lab | Type-unsafe, noisy, inconsistent with the rest of the C++ code |
| 6 | Worker loop implemented twice (firmware/Application.cpp and lab/main.cpp); only the wait calculation is shared | firmware, lab | Lab tests do not exercise the firmware's exact worker code |
| 7 | ObjectMap: walls of `static_assert` and `switch` statements per function and block; KnxApplication routes objects with chains | knx/ObjectMap.cpp, knxstack | The layout is data, but it is written as control flow |
| 8 | Console parses commands with `strcmp` chains | firmware/UsbConsole.cpp | Repetitive; argument parsing duplicated |
| 9 | Results built with repeated literals (`{ControlError::None, TransportStatus::Ok, {}, {}, {}}`) | control, lighting | Noise; easy to get wrong |
| 10 | Folder layout grew by accretion (dali/ holds types, frames, gear transport, DT8; knx/ mixes DPT codecs, object layout and the ETS protocol; device/, knxstack/, firmware/, espidf/ and lab/ are siblings of domain folders) | src/ | Layering is not visible from the tree |
| 11 | Large files: LightingService.cpp 851, test files up to 928 lines; one test suite (test_lighting) holds bus, colour, commissioning and input tests | src, tests | Navigation and review cost; slow incremental test builds |
| 12 | No automatic lint: unused includes/imports slipped in (e.g. Fakes.hpp, simulation tests) | C++, Python | Needs a gate, not manual review |
| 13 | Per-file SPDX headers in hardware/simulation files | various | User decision: remove; folder-level licence mapping is enough |

## 2. Design principles for the refactor

- Keep the existing good parts: ports and adapters (Transport, LevelStore, BusPort), manual constructor injection with one composition root per build, value types for DALI addresses/levels, pure state machines with injected time, bounded queues between tasks.
- Commands stay plain data (they cross FreeRTOS queues), so the "strategy" is static: a `std::variant` per domain plus a handler class whose overloaded `operator()` / `handle()` methods form the strategies. `std::visit` then guarantees at compile time that every command has a handler. Virtual command objects are not an option because queue items must be trivially copyable.
- Small classes with one reason to change; target ≤ ~300 lines per production file.
- No speculative layers: each new abstraction removes duplication that exists today.

## 3. Target structure

```text
gateway/src/
  dali/                     protocol only (gateway::dali)
    types/                  ShortAddress, Group, Scene, Address, ArcLevel, Mirek, colour value types
    frame/                  Frame, gear/special/device command codes
    bus/                    Manchester, LinkLayer, Timing, BusPort
    gear/                   Transport, BusTransport, RetryingTransport, GearAddressing, Dt8 sequences
    devices/                Part 103: DeviceAddressing, DeviceInventory, EventDecoder
  lighting/                 domain (gateway::lighting)
    ChannelModel            states of 64 channels + 16 groups, Targets resolution (Address -> affected channels/groups)
    LevelController         DAPC, switch on/off, switch-on policies, last-active persistence
    ColourController        DT8 operations, colour state, capability checks
    SceneController         recall/store
    Dimming                 RelativeDimmer instances + step/finish logic
    Recovery                refresh, recover, startup restore
    GroupReconciler         ETS group membership -> gear
    GearTuning              (new) per-gear settings reconciliation, see section 5
  inputs/                   InputChannel, InputMapper
  commissioning/            Commissioning (scan/assign/replace/identify/faults)
  config/                   Configuration + parsing (grouped per entity)
  storage/                  LevelStore port
  app/                      application layer (gateway::app)
    commands/               LightingCommand, DimmingCommand, CommissioningCommand, InputCommand variants
    Dispatcher              std::visit over Command -> domain handlers
    Worker                  the worker loop, shared by firmware and lab (queues/clock/heartbeat injected)
    events/                 StatusEvent types
  knx/                      KNX side (gateway::knx)
    dpt/                    one codec per DPT family (1, 3, 5, 7.600, 9, 17, 18, 232.600, 242.600, 251.600)
    objects/                ObjectLayout (table generated from the knxprod header), ObjectRouter (function -> decoder table)
    commissioning/          function-property protocol
    stack/                  KnxApplication (thelsing glue)
    generated/
  ui/                       ProgButton and LED patterns
  platform/
    esp32/                  RmtDaliBus, NvsLevelStore, KnxDevice, UsbConsole, Board, Queue, main + composition root
    lab/                    SocketDaliTransport, FileLevelStore, StdQueue, main + composition root
gateway/tests/unit/<module>/  one PlatformIO suite per module (dali, lighting, knx, inputs, commissioning, app, ui)
```

Namespaces mirror the folders (`gateway::dali::bus`, `gateway::knx::dpt`, …).

## 4. Refactoring steps (each step keeps all tests and the lab green)

1. **Lint gates first:** clang-tidy (misc-include-cleaner plus a small bugprone/readability subset) on the compile databases and ruff for Python, both in Docker, wired into the check scripts; fix all findings. Remove per-file SPDX headers.
2. **Value types:** colour values, `Targets`, `ControlResult::ok()`; replace `void*` callbacks with small interfaces.
3. **Split LightingService** into the classes listed above, sharing `ChannelModel` and `Transport` through constructor injection. Behaviour unchanged; existing tests move with their unit.
4. **Commands and dispatch:** per-domain command variants, one handler per domain, `app::Dispatcher` via `std::visit`. Remove `address_of`; results carry the affected targets instead.
5. **Shared worker:** move the loop into `app::Worker`; firmware and lab only compose it.
6. **KNX table-driven layout and routing:** `ObjectLayout` built from the generated header once, `ObjectRouter` maps object functions to decoders/encoders; delete the switch walls.
7. **Console table:** command specs with typed argument parsers.
8. **Folder and namespace moves; split test suites** to mirror modules.

Steps 1–2 are mechanical; steps 3–6 are the real redesign; step 8 is a move with no logic change.

## 5. User-facing flexibility to add after the refactor

These need ETS parameters, firmware support and lab tests:

- **Operating mode per channel:** Auto (from detected capabilities), Brightness only, Colour temperature, RGB, RGBW, xy, or **Individual primaries**. Individual primaries expose RGBWAF primaries as separate dimmable channels through a pool of 32 "virtual channels". Each virtual channel is bound in ETS to (short address, primary) and has Switch/Brightness objects plus status. The pool keeps the object count bounded.
- **Gear tuning per channel (advanced page):** min/max level, power-on level, system-failure level, fade time, fade rate, extended fade time, dimming curve (logarithmic/linear for DALI-2 LED gear, Part 207), switch-on/off fade behaviour (instant or soft fade), colour temperature limits (warm/cool). Applied to the gear by `GearTuning` with query-then-write-differences, only after an ETS download or a replacement.
- **Capability overrides:** readback auto/yes/no (force fallback B), colour type override, exclude from fault monitoring. These cover gear that reports wrong or no information.
- **Gateway-wide tuning:** startup behaviour (readback recovery / restore saved / leave as is / fixed level), status sending (on change / cyclic / on request), fault-poll interval, retry count, status telegram rate limit.
- **ETS page structure:** General, Gateway tuning, Channels (basic + "advanced" toggle per channel), Groups, Virtual channels, Inputs, Commissioning.

## 6. Risks

- Object numbers change when virtual channels and tuning parameters are added. Nothing is deployed, so we renumber once and keep the generated-header static_asserts as the guard.
- The refactor touches most files. Doing it before the new features keeps both changes reviewable.
