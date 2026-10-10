#pragma once

#include <cstdint>
#include <optional>

#include "control/Command.hpp"

namespace gateway::knx {

std::optional<control::Command> command_for_switch(std::uint16_t object, bool on) noexcept;
std::optional<control::Command> command_for_scaling(std::uint16_t object, std::uint8_t raw) noexcept;
// DPT 3.007: bit 3 = direction (1 = up), bits 0-2 = step code. Any non-zero
// step starts continuous dimming until a stop telegram — a deliberate
// simplification of the DPT step sizes.
std::optional<control::Command> command_for_relative_dimming(std::uint16_t object, std::uint8_t raw) noexcept;
// DPT 18.001: bit 7 = learn (store), bits 0-5 = scene number. Numbers 0..15
// map to DALI scenes 0..15; higher numbers have no DALI scene.
std::optional<control::Command> command_for_scene(std::uint16_t object, std::uint8_t raw) noexcept;

struct StatusObjects {
    std::uint16_t switch_object;
    bool on;
    std::uint16_t brightness_object;
    std::uint8_t scaling;
};

std::optional<StatusObjects> status_objects(const control::StatusUpdate& update) noexcept;

}
