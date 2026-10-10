#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gateway::dali {

// DALI group number (0..15). Only constructible from a valid number, so code
// receiving a Group never re-checks the range.
class Group final {
public:
    static constexpr std::size_t capacity = 16;

    [[nodiscard]] static std::optional<Group> from(unsigned value) noexcept;
    std::uint8_t value() const noexcept;

private:
    explicit Group(std::uint8_t value) noexcept;
    std::uint8_t value_;
};

}
