#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gateway::inputs {

// Index of one configured input channel (0..31, a row of the ETS input
// table). Only constructible from a valid number, so code receiving an
// InputChannel never re-checks the range.
class InputChannel final {
public:
    static constexpr std::size_t capacity = 32;

    [[nodiscard]] static std::optional<InputChannel> from(unsigned value) noexcept;
    std::uint8_t value() const noexcept;

private:
    explicit InputChannel(std::uint8_t value) noexcept;
    std::uint8_t value_;
};

}
