#include "dali/ShortAddress.hpp"

namespace gateway::dali {

ShortAddress::ShortAddress(std::uint8_t value) noexcept : value_(value) {}

std::optional<ShortAddress> ShortAddress::from(unsigned value) noexcept {
    if (value >= capacity) {
        return std::nullopt;
    }
    return ShortAddress{static_cast<std::uint8_t>(value)};
}

std::uint8_t ShortAddress::value() const noexcept {
    return value_;
}

}
