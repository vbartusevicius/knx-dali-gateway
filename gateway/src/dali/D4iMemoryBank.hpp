#pragma once

#include <cstdint>
#include <optional>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

struct D4iPowerReading {
    std::uint16_t active_power_deciwatts = 0;  // 0.1 W resolution
    std::uint32_t active_energy_wh = 0;        // 1 Wh resolution
};

// Probes and reads IEC 62386-252 (D4i Memory Bank 202 — Active Energy & Active Power).
// Returns std::nullopt if the control gear does not implement Memory Bank 202.
[[nodiscard]] std::optional<D4iPowerReading> read_d4i_power_energy(Transport& transport,
                                                                   ShortAddress address);

}  // namespace gateway::dali
