#include "dali/Group.hpp"

namespace gateway::dali {

Group::Group(std::uint8_t value) noexcept : value_(value) {}

std::optional<Group> Group::from(unsigned value) noexcept {
    if (value >= capacity) {
        return std::nullopt;
    }
    return Group{static_cast<std::uint8_t>(value)};
}

std::uint8_t Group::value() const noexcept {
    return value_;
}

}
