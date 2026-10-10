#include "dali/ArcLevel.hpp"

namespace gateway::dali {

ArcLevel::ArcLevel(std::uint8_t value) noexcept : value_(value) {}

std::optional<ArcLevel> ArcLevel::from(unsigned value) noexcept {
    if (value > maximum) {
        return std::nullopt;
    }
    return ArcLevel{static_cast<std::uint8_t>(value)};
}

std::uint8_t ArcLevel::value() const noexcept {
    return value_;
}

}
