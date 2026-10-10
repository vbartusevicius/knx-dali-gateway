#pragma once

#include <cstdint>
#include <optional>

namespace gateway::dali {

// DALI brightness value (0 = off, 1..254 = dimming curve). 255 is DALI 'MASK'
// (no change) and is rejected, so it can never be sent as a target.
class ArcLevel final {
public:
    static constexpr unsigned maximum = 254;

    [[nodiscard]] static std::optional<ArcLevel> from(unsigned value) noexcept;
    std::uint8_t value() const noexcept;

private:
    explicit ArcLevel(std::uint8_t value) noexcept;
    std::uint8_t value_;
};

}
