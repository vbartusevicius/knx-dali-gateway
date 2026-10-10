#pragma once

#include <cstdint>
#include <optional>

#include "control/Command.hpp"

namespace gateway::knx {

std::optional<control::Command> command_for_switch(std::uint16_t object, bool on) noexcept;
std::optional<control::Command> command_for_scaling(std::uint16_t object, std::uint8_t raw) noexcept;

struct StatusObjects {
    std::uint16_t switch_object;
    bool on;
    std::uint16_t brightness_object;
    std::uint8_t scaling;
};

std::optional<StatusObjects> status_objects(const control::StatusUpdate& update) noexcept;

}
