#pragma once

#include <cstdint>

#include "dali/ArcLevel.hpp"

namespace gateway::knx {

// KNX DPT 5.001 scaling byte (0..255 = 0..100 %) mapped linearly onto DALI arc levels.
dali::ArcLevel arc_from_scaling(std::uint8_t raw) noexcept;
std::uint8_t scaling_from_arc(dali::ArcLevel level) noexcept;

}
