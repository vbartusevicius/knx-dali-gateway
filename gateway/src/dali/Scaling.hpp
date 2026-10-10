#pragma once

#include <cstdint>

#include "dali/ArcLevel.hpp"

namespace gateway::dali {

// DPT 5.001 scaling (0..255 percent) <-> DALI arc level (0..254).
dali::ArcLevel arc_from_scaling(std::uint8_t raw) noexcept;
std::uint8_t scaling_from_arc(dali::ArcLevel level) noexcept;

// KNX colour component (0..255) -> DALI channel dimlevel (0..254):
// round(v * 254 / 255); 0xFF on the bus stays a literal "no change" marker
// and is handled by the callers, not here.
std::uint8_t channel_from_scaling(std::uint8_t raw) noexcept;

// DALI channel dimlevel (0..254) -> KNX colour component (0..255).
std::uint8_t scaling_from_channel(std::uint8_t level) noexcept;

}
