#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gateway::dali {

// Address of one DALI control gear (0..63). Only constructible from a valid
// number, so code receiving a ShortAddress never re-checks the range.
class ShortAddress final {
public:
    static constexpr std::size_t capacity = 64;

    [[nodiscard]] static std::optional<ShortAddress> from(unsigned value) noexcept;
    std::uint8_t value() const noexcept;

private:
    explicit ShortAddress(std::uint8_t value) noexcept;
    std::uint8_t value_;
};

}
