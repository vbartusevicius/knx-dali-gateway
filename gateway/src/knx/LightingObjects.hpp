#pragma once

#include <array>
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
// DPT 7.600 (2 bytes Kelvin), 232.600 (3 bytes RGB), 251.600 (6 bytes RGBW),
// 242.600 (6 bytes xyY); layouts in ColourDatapoints.
std::optional<control::Command> command_for_colour_temperature(std::uint16_t object,
                                                             const std::uint8_t* data,
                                                             std::size_t length) noexcept;
// Same DPT 3.007 decode as brightness dimming, routed to the colour dimmer.
std::optional<control::Command> command_for_relative_colour_temperature(
    std::uint16_t object, std::uint8_t raw) noexcept;
std::optional<control::Command> command_for_rgb(std::uint16_t object, const std::uint8_t* data,
                                              std::size_t length) noexcept;
std::optional<control::Command> command_for_rgbw(std::uint16_t object, const std::uint8_t* data,
                                               std::size_t length) noexcept;
std::optional<control::Command> command_for_xyy(std::uint16_t object, const std::uint8_t* data,
                                              std::size_t length) noexcept;

struct StatusObjects {
    std::uint16_t switch_object;
    bool on;
    std::uint16_t brightness_object;
    std::uint8_t scaling;
};

std::optional<StatusObjects> status_objects(const control::StatusUpdate& update) noexcept;

// One status object write for a colour change: the object matching the
// report's colour kind, with the DPT-encoded payload.
struct ColourStatusObject {
    std::uint16_t object;
    std::array<std::uint8_t, 6> data;
    std::uint8_t length;
};

std::optional<ColourStatusObject> colour_status_object(const control::ColourUpdate& update) noexcept;

}
