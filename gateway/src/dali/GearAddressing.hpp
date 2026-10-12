#pragma once

#include <cstdint>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

// IEC 62386-102 §9.4 control-gear addressing implemented over plain
// Transport primitives (send_special/query_special): TERMINATE, INITIALISE,
// RANDOMISE, binary search with SEARCHADDRH/M/L + COMPARE, PROGRAM/VERIFY
// SHORT ADDRESS and WITHDRAW. Part 103 input devices are silenced with
// quiescent mode for the duration so their event frames cannot corrupt the
// COMPARE backward windows.
class GearAddressing {
public:
    // delay_ms yields the worker while gear generate their 24-bit random
    // addresses (>= 100 ms after RANDOMISE); a nullptr delay is a no-op
    // (native tests drive no real time).
    explicit GearAddressing(Transport& bus, void (*delay_ms)(std::uint32_t) = nullptr)
        : bus_(bus), delay_ms_(delay_ms) {}

    CommissionResult commission(CommissionMode mode, ShortAddress start, unsigned max_devices);

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
