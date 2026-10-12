#include "knx/BrightnessScale.hpp"

#include <cstdint>

#include "dali/ArcLevel.hpp"
#include "dali/Scaling.hpp"

namespace gateway::knx {

dali::ArcLevel arc_from_scaling(std::uint8_t raw) noexcept {
    return dali::arc_from_scaling(raw);
}

std::uint8_t scaling_from_arc(dali::ArcLevel level) noexcept {
    return dali::scaling_from_arc(level);
}

}
