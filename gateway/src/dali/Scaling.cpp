#include "dali/Scaling.hpp"

namespace gateway::dali {

dali::ArcLevel arc_from_scaling(std::uint8_t raw) noexcept {
    if (raw == 0) {
        return *dali::ArcLevel::from(0);
    }
    const unsigned scaled = 1 + (static_cast<unsigned>(raw - 1) * 506 + 254) / 508;
    return *dali::ArcLevel::from(scaled);
}

std::uint8_t scaling_from_arc(dali::ArcLevel level) noexcept {
    if (level.value() == 0) {
        return 0;
    }
    return static_cast<std::uint8_t>(1 + (static_cast<unsigned>(level.value() - 1) * 508 + 253) / 506);
}

std::uint8_t channel_from_scaling(std::uint8_t raw) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(raw) * 254 + 127) / 255);
}

std::uint8_t scaling_from_channel(std::uint8_t level) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(level) * 255 + 127) / 254);
}

}
