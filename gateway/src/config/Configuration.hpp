#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "dali/ArcLevel.hpp"
#include "dali/Group.hpp"
#include "dali/ShortAddress.hpp"

namespace gateway::config {

// What switch-on recalls: the last non-zero level or a configured fixed level.
enum class SwitchOnPolicy : std::uint8_t {
    LastLevel,
    FixedLevel
};

struct SwitchOn {
    SwitchOnPolicy policy = SwitchOnPolicy::LastLevel;
    dali::ArcLevel fixed = *dali::ArcLevel::from(dali::ArcLevel::maximum);
};

// Per-channel DT6 ballast operating limits & fade curve (IEC 62386-102).
// 0xFF in fade_time / fade_rate means "keep existing ballast setting".
struct BallastParameters {
    std::uint8_t min_level = 1;               // DALI arc 1..254
    std::uint8_t max_level = 254;             // DALI arc 1..254
    std::uint8_t power_on_level = 254;        // DALI arc 0..254 or 255 (MASK / last level)
    std::uint8_t system_failure_level = 254;  // DALI arc 0..254 or 255 (MASK / no change)
    std::uint8_t fade_time = 0xFF;            // 0..15 or 0xFF (keep)
    std::uint8_t fade_rate = 0xFF;            // 1..15 or 0xFF (keep)
};

// DALI-2 input channel: the IEC 62386-103 control device short address and
// instance the channel binds to, the declared instance type, and the
// type-specific behaviour parameters.
constexpr std::size_t input_count = 32;

enum class InputType : std::uint8_t {
    Unused,
    PushButton,
    Occupancy,
    Light
};

enum class InputFunction : std::uint8_t {
    Toggle,
    On,
    Off,
    ToggleDim,
    Scene
};

struct InputChannel {
    std::uint8_t device = 0;    // control-device short address 0..63
    std::uint8_t instance = 0;  // instance number 0..31
    InputType type = InputType::Unused;
    InputFunction function = InputFunction::Toggle;
    std::uint8_t scene = 1;      // KNX scene number 1..64
    std::uint8_t threshold = 0;  // illuminance send threshold %; 0 = every event
};

// ETS parameter set: per-channel switch-on policy, DT6 ballast limits/fade,
// and DALI group membership (bit g = member of group g), plus group and
// broadcast switch-on policies and internal DALI PSU enable.
struct Configuration {
    bool internal_dali_psu_enabled = true;
    std::array<SwitchOn, dali::ShortAddress::capacity> channels{};
    std::array<BallastParameters, dali::ShortAddress::capacity> ballasts{};
    std::array<std::uint16_t, dali::ShortAddress::capacity> group_masks{};
    std::array<SwitchOn, dali::Group::capacity> groups{};
    SwitchOn broadcast{};
    std::array<InputChannel, input_count> inputs{};
};

static_assert(std::is_trivially_copyable_v<Configuration>, "Configuration travels through FreeRTOS queues");

Configuration default_configuration() noexcept;

// Decodes the ETS parameter memory (knx.paramData(0), MAIN_ParameterSize bytes).
// Out-of-range or invalid values fall back to that field's default.
Configuration parse_configuration(const std::uint8_t* parameters, std::size_t size) noexcept;

}  // namespace gateway::config
