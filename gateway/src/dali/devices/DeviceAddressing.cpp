#include "dali/devices/DeviceAddressing.hpp"

#include <cstdint>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "dali/devices/DeviceFrames.hpp"

namespace gateway::dali {

TransportStatus DeviceAddressing::set_search_address(std::uint32_t search) {
    const std::uint32_t frames[3] = {
        device::special::searchaddr_h(static_cast<std::uint8_t>(search >> 16)),
        device::special::searchaddr_m(static_cast<std::uint8_t>(search >> 8)),
        device::special::searchaddr_l(static_cast<std::uint8_t>(search)),
    };
    for (const auto frame : frames) {
        const auto status = bus_.send_device_frame(frame, false);
        if (status != TransportStatus::Ok) {
            return status;
        }
    }
    return TransportStatus::Ok;
}

TransportStatus DeviceAddressing::compare_yes(bool& yes) {
    const auto reply = bus_.query_device_frame(device::special::compare());
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

CommissionResult DeviceAddressing::commission(CommissionMode mode, ShortAddress start, unsigned max_devices) {
    unsigned addressed = 0;
    const auto finish = [&](TransportStatus status) {
        bus_.send_device_frame(device::special::terminate(), false);
        bus_.send_device_command(device::address_broadcast, device::cmd::stop_quiescent_mode, true);
        return CommissionResult{status, addressed};
    };

    // Silence device event traffic; failure only means there are none.
    bus_.send_device_command(device::address_broadcast, device::cmd::start_quiescent_mode, true);

    auto status = bus_.send_device_frame(device::special::terminate(), false);
    if (status != TransportStatus::Ok) {
        return finish(status);
    }
    // INITIALISE operand: 0x00 all devices, 0xFF devices without a short
    // address, 0AAAAAA1 one device.
    status =
        bus_.send_device_frame(device::special::initialise(mode == CommissionMode::All ? 0x00 : 0xFF), true);
    if (status != TransportStatus::Ok) {
        return finish(status);
    }
    status = bus_.send_device_frame(device::special::randomise(), true);
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

        const std::uint8_t encoded = device::special::encode_short_address(next);
        status = bus_.send_device_frame(device::special::program_short_address(encoded), true);
        if (status != TransportStatus::Ok) {
            return finish(status);
        }
        const auto verified = bus_.query_device_frame(device::special::verify_short_address(encoded));
        const bool verified_yes =
            verified.status == TransportStatus::Ok || verified.status == TransportStatus::Collision;
        status = bus_.send_device_frame(device::special::withdraw(), true);
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
