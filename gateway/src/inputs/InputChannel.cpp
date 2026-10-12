#include "inputs/InputChannel.hpp"

#include <cstdint>
#include <optional>

namespace gateway::inputs {

InputChannel::InputChannel(std::uint8_t value) noexcept : value_(value) {}

std::optional<InputChannel> InputChannel::from(unsigned value) noexcept {
    if (value >= capacity) {
        return std::nullopt;
    }
    return InputChannel{static_cast<std::uint8_t>(value)};
}

std::uint8_t InputChannel::value() const noexcept {
    return value_;
}

}
