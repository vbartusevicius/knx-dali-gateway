#pragma once

#include <cstdint>
#include <optional>

#include "dali/Address.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace gateway::knx {

enum class ObjectFunction {
    Switch,
    SwitchStatus,
    Brightness,
    BrightnessStatus,
    RelativeDimming,
    Scene
};

// KNX group objects: 5 per channel at 1+5n+k, 5 per group at 321+5g+k,
// broadcast objects 401..404. All offsets derive from the generated header.
constexpr std::uint16_t object_count = MAIN_MaxKoNumber;

std::uint16_t object_number(dali::Address address, ObjectFunction function) noexcept;

struct ObjectTarget {
    dali::Address address;
    ObjectFunction function;
};

std::optional<ObjectTarget> object_target(std::uint16_t number) noexcept;

}
