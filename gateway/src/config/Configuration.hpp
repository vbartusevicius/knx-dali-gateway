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

// ETS parameter set: per-channel switch-on policy and DALI group membership
// (bit g = member of group g), plus group and broadcast switch-on policies.
struct Configuration {
    std::array<SwitchOn, dali::ShortAddress::capacity> channels{};
    std::array<std::uint16_t, dali::ShortAddress::capacity> group_masks{};
    std::array<SwitchOn, dali::Group::capacity> groups{};
    SwitchOn broadcast{};
};

static_assert(std::is_trivially_copyable_v<Configuration>, "Configuration travels through FreeRTOS queues");

Configuration default_configuration() noexcept;

// Decodes the ETS parameter memory (knx.paramData(0), MAIN_ParameterSize bytes).
// Out-of-range or invalid values fall back to that field's default.
Configuration parse_configuration(const std::uint8_t* parameters, std::size_t size) noexcept;

}
