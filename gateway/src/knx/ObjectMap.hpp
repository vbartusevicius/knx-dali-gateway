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
    Fault,
    Scene,
    ColourTemperature,
    ColourTemperatureStatus,
    RelativeColourTemperature,
    Rgb,
    RgbStatus,
    Rgbw,
    RgbwStatus,
    XyY,
    XyYStatus
};

// KNX group objects: 15 per channel at 1+15n+k, 14 per group at 961+14g+k,
// broadcast objects 1185..1193, 4 per input channel at 1194+4i+k.
// All offsets derive from the generated header.
constexpr std::uint16_t object_count = MAIN_MaxKoNumber;

std::uint16_t object_number(dali::Address address, ObjectFunction function) noexcept;

struct ObjectTarget {
    dali::Address address;
    ObjectFunction function;
};

std::optional<ObjectTarget> object_target(std::uint16_t number) noexcept;

// Input-channel objects; inputs bind a control-device instance, not a gear
// address, so they get their own functions.
enum class InputObjectFunction {
    Switch,
    Dimming,
    Illuminance,
    Scene
};

std::uint16_t input_object_number(std::uint8_t channel, InputObjectFunction function) noexcept;

struct InputObjectTarget {
    std::uint8_t channel;
    InputObjectFunction function;
};

std::optional<InputObjectTarget> input_object_target(std::uint16_t number) noexcept;

}
