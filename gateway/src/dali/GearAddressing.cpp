#include "dali/GearAddressing.hpp"

#include <cstdint>

#include "dali/ShortAddress.hpp"
#include "dali/SpecialCommands.hpp"
#include "dali/Transport.hpp"
#include "dali/devices/DeviceFrames.hpp"

namespace gateway::dali {

TransportStatus GearAddressing::set_search_address(std::uint32_t search) {
    const std::uint8_t opcodes[3] = {special::searchaddr_h, special::searchaddr_m, special::searchaddr_l};
    const std::uint8_t bytes[3] = {static_cast<std::uint8_t>(search >> 16),
                                   static_cast<std::uint8_t>(search >> 8), static_cast<std::uint8_t>(search)};
    for (int i = 0; i < 3; ++i) {
        const auto status = bus_.send_special(opcodes[i], bytes[i], false);
        if (status != TransportStatus::Ok) {
            return status;
        }
    }
    return TransportStatus::Ok;
}

TransportStatus GearAddressing::compare_yes(bool& yes) {
    const auto reply = bus_.query_special(special::compare, 0x00);
    switch (reply.status) {
    case TransportStatus::Ok:
    case TransportStatus::Collision:
        yes = true;
        return TransportStatus::Ok;
    case TransportStatus::Timeout:
        yes = false;
        return TransportStatus::Ok;
    default:
        return reply.status;
    }
}

CommissionResult GearAddressing::commission(CommissionMode mode, ShortAddress start, unsigned max_devices) {
    unsigned addressed = 0;
    const auto finish = [&](TransportStatus status) {
        bus_.send_special(special::terminate, 0x00, false);
        bus_.send_device_command(device::address_broadcast, device::cmd::stop_quiescent_mode, true);
        return CommissionResult{status, addressed};
    };

    // Quiesce Part 103 input devices; failure only means there are none.
    bus_.send_device_command(device::address_broadcast, device::cmd::start_quiescent_mode, true);

    auto status = bus_.send_special(special::terminate, 0x00, false);
    if (status != TransportStatus::Ok) {
        return finish(status);
    }
    // INITIALISE operand: 0x00 all gear, 0xFF gear without a short address.
    status = bus_.send_special(special::initialise, mode == CommissionMode::All ? 0x00 : 0xFF, true);
    if (status != TransportStatus::Ok) {
        return finish(status);
    }
    status = bus_.send_special(special::randomise, 0x00, true);
    if (status != TransportStatus::Ok) {
        return finish(status);
    }
    if (delay_ms_ != nullptr) {
        delay_ms_(randomise_settle_ms);
    }

    std::uint8_t next = start.value();
    for (unsigned slot = 0; slot < max_slots && addressed < max_devices && next <= 63; ++slot) {
        status = set_search_address(search_max);
        if (status != TransportStatus::Ok) {
            return finish(status);
        }
        bool yes = false;
        status = compare_yes(yes);
        if (status != TransportStatus::Ok) {
            return finish(status);
        }
        if (!yes) {
            break;
        }

        // Bisect down to the lowest random address still answering.
        std::uint32_t low = 0;
        std::uint32_t high = search_max;
        while (low < high) {
            const std::uint32_t mid = low + (high - low) / 2;
            status = set_search_address(mid);
            if (status != TransportStatus::Ok) {
                return finish(status);
            }
            status = compare_yes(yes);
            if (status != TransportStatus::Ok) {
                return finish(status);
            }
            if (yes) {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        status = set_search_address(low);
        if (status != TransportStatus::Ok) {
            return finish(status);
        }

        const std::uint8_t encoded = special::encode_short_address(next);
        status = bus_.send_special(special::program_short_address, encoded, true);
        if (status != TransportStatus::Ok) {
            return finish(status);
        }
        const auto verified = bus_.query_special(special::verify_short_address, encoded);
        const bool verified_yes =
            verified.status == TransportStatus::Ok || verified.status == TransportStatus::Collision;
        status = bus_.send_special(special::withdraw, 0x00, true);
        if (status != TransportStatus::Ok) {
            return finish(status);
        }
        // A unit that did not verify is withdrawn anyway and the slot is
        // retried for the next lowest random address.
        if (verified_yes) {
            ++addressed;
            ++next;
        }
    }
    return finish(TransportStatus::Ok);
}

}
