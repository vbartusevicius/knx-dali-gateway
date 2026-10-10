#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gateway::dali {

// DALI scene number (0..15). Only constructible from a valid number, so code
// receiving a Scene never re-checks the range.
class Scene final {
public:
    static constexpr std::size_t capacity = 16;

    [[nodiscard]] static std::optional<Scene> from(unsigned value) noexcept;
    std::uint8_t value() const noexcept;

private:
    explicit Scene(std::uint8_t value) noexcept;
    std::uint8_t value_;
};

}
