#pragma once

#include <array>
#include <type_traits>
#include <variant>

#include "commissioning/Commissioning.hpp"
#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "inputs/InputChannel.hpp"
#include "inputs/InputMapper.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace gateway::control {

// Requests that cross task boundaries (console, KNX) into the lighting service.
struct SetLevel {
    dali::Address address;
    dali::ArcLevel level;
};

struct SwitchOn {
    dali::Address address;
};

struct SwitchOff {
    dali::Address address;
};

struct Refresh {
    dali::ShortAddress address;
};

struct Recover {
    dali::ShortAddress address;
};

struct ReconcileGroups {
    dali::ShortAddress address;
};

struct DimStart {
    dali::Address address;
    lighting::DimDirection direction;
};

struct DimStop {
    dali::Address address;
};

struct RecallScene {
    dali::Scene scene;
};

struct StoreScene {
    dali::Scene scene;
};

struct ScanBus {};

struct AssignAddresses {
    dali::CommissionMode mode;
};

struct ReplaceDevice {
    dali::ShortAddress address;
};

struct IdentifyDevice {
    dali::ShortAddress address;
};

struct ResetAddresses {};

// Part 103 control-device inventory + instance configuration (the worker's
// InputMapper supplies which instances to enable).
struct ScanInputDevices {};

// Part 103 address assignment; mode chooses new (unaddressed) or all.
struct AssignInputDevices {
    dali::CommissionMode mode;
};

// A KNX write on an input channel's switch object: updates the channel's
// remembered toggle state.
struct InputFeedback {
    inputs::InputChannel channel;
    bool on;
};

// Colour commands decoded from KNX objects (DPT 7.600 / 232.600 / 251.600 /
// 242.600 writes).
struct SetColourTemperature {
    dali::Address address;
    std::uint16_t kelvin;
};

struct SetRgb {
    dali::Address address;
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

// validity: bit 3 = r, bit 2 = g, bit 1 = b, bit 0 = w (DPT 251.600 mask).
struct SetRgbw {
    dali::Address address;
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t white;
    std::uint8_t validity;
};

struct SetXy {
    dali::Address address;
    std::uint16_t x;
    std::uint16_t y;
    bool xy_valid;
    std::uint8_t brightness;
    bool brightness_valid;
};

// Relative colour temperature (DPT 3.007 on the relative-Tc objects); the
// worker's second RelativeDimmer drives the steps.
struct ColourDimStart {
    dali::Address address;
    lighting::DimDirection direction;
};

struct ColourDimStop {
    dali::Address address;
};

using Command =
    std::variant<SetLevel, SwitchOn, SwitchOff, Refresh, Recover, ReconcileGroups, DimStart, DimStop,
                 RecallScene, StoreScene, ScanBus, AssignAddresses, ReplaceDevice, IdentifyDevice,
                 ResetAddresses, ScanInputDevices, AssignInputDevices, InputFeedback, SetColourTemperature,
                 SetRgb, SetRgbw, SetXy, ColourDimStart, ColourDimStop>;

static_assert(std::is_trivially_copyable_v<Command>, "Command travels through FreeRTOS queues");

struct StatusUpdate {
    dali::Address address;
    lighting::State state;
};

struct FaultUpdate {
    dali::ShortAddress address;
    bool faulty;
};

// A channel/group colour report changed (or its brightness half did).
struct ColourUpdate {
    dali::Address address;
    lighting::ColourReport report;
};

// One input-channel action the KNX task writes into the named group
// object: raw value bytes plus length (1-bit/4-bit/1-byte in data[0], the
// DPT 9 illuminance as a 2-byte float).
struct InputAction {
    std::uint16_t object;
    std::array<std::uint8_t, 2> data;
    std::uint8_t length;
};

// Items on the worker-to-KNX status queue.
using StatusEvent = std::variant<StatusUpdate, FaultUpdate, ColourUpdate, InputAction>;

static_assert(std::is_trivially_copyable_v<StatusEvent>, "StatusEvent travels through FreeRTOS queues");

[[nodiscard]] dali::Address address_of(const Command& command) noexcept;

// Dispatches one command: dim start/stop go to the worker's brightness
// dimmer, colour-dim start/stop to `colour_dimmer` (a stop finishes the dim
// in the service with a readback), commissioning commands to the
// commissioning service (input jobs bind instances through `inputs`),
// InputFeedback updates the mapper's toggle state, everything else to the
// lighting service.
[[nodiscard]] commissioning::JobResult execute(lighting::LightingService& service,
                                               lighting::RelativeDimmer& dimmer,
                                               lighting::RelativeDimmer& colour_dimmer,
                                               commissioning::Commissioning& commissioning,
                                               inputs::InputMapper& inputs, std::uint32_t now_ms,
                                               const Command& command);

}
