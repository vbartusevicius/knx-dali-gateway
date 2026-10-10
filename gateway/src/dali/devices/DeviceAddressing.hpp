#pragma once

#include <cstdint>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

// IEC 62386-103 control-device addressing over the 24-bit Table 22 special
// commands: TERMINATE, INITIALISE, RANDOMISE, binary search with
// SEARCHADDRH/M/L + COMPARE, PROGRAM/VERIFY SHORT ADDRESS and WITHDRAW.
// Control devices share the bus with gear but live in a separate 0..63
// short-address space. Mirrors GearAddressing: the device's own event
// traffic is silenced with quiescent mode for the duration.
class DeviceAddressing {
public:
    // delay_ms yields the worker while devices generate their 24-bit random
    // addresses; a nullptr delay is a no-op (native tests drive no real
    // time).
    explicit DeviceAddressing(Transport& bus,
                              void (*delay_ms)(std::uint32_t) = nullptr)
        : bus_(bus), delay_ms_(delay_ms) {}

    CommissionResult commission(CommissionMode mode, ShortAddress start,
                                unsigned max_devices);

private:
    static constexpr std::uint32_t search_max = 0xFFFFFF;
    static constexpr std::uint32_t randomise_settle_ms = 100;
    static constexpr unsigned max_slots = 64;

    TransportStatus set_search_address(std::uint32_t search);
    // Any backward frame — clean or the collision of several units —
    // answers "yes" for COMPARE-style queries.
    TransportStatus compare_yes(bool& yes);

    Transport& bus_;
    void (*delay_ms_)(std::uint32_t);
};

}
