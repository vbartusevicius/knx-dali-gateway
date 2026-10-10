#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

// Discovered IEC 62386-103 control-device inventory: per present device the
// instance count, per-instance type (Part 103 Table 4), and the resolution
// QUERY RESOLUTION reported for light instances (used to scale event
// values to lux). Also applies the instance configuration: device/instance
// event scheme, the per-type default event filter, and enable/disable
// according to which instances ETS parameters bind to input channels.
// Runs on the DALI worker; pure C++, hardware independent.
class DeviceInventory {
public:
    static constexpr std::uint8_t max_instances = 32;

    explicit DeviceInventory(Transport& bus) noexcept : bus_(bus) {}

    // QUERY NUMBER OF INSTANCES on all 64 device short addresses, then
    // QUERY INSTANCE TYPE (and, for light instances, QUERY RESOLUTION) per
    // instance. Timeout marks the device absent; the first other error is
    // returned while the rest of the scan continues best-effort.
    TransportStatus scan();

    // For every discovered instance bound() marks as bound to a channel:
    // DTR0 = event scheme device/instance + SET EVENT SCHEME, DTR0..2 =
    // the type's default filter + SET EVENT FILTER, ENABLE INSTANCE.
    // Unbound instances get DISABLE INSTANCE. Returns the first error.
    TransportStatus apply(bool (*bound)(std::uint8_t device, std::uint8_t instance,
                                        void* context),
                          void* context);

    const std::bitset<ShortAddress::capacity>& present() const noexcept { return present_; }
    std::optional<std::uint8_t> instance_count(std::uint8_t device) const noexcept;
    std::optional<std::uint8_t> instance_type(std::uint8_t device,
                                            std::uint8_t instance) const noexcept;
    std::optional<std::uint8_t> resolution(std::uint8_t device,
                                           std::uint8_t instance) const noexcept;
    // True when the device has at least one instance of the given type.
    bool has_type(std::uint8_t device, std::uint8_t type) const noexcept;

private:
    struct Device {
        std::array<std::uint8_t, max_instances> types{};
        std::array<std::uint8_t, max_instances> resolutions{};
        std::uint8_t count = 0;
    };

    TransportStatus configure_instance(std::uint8_t device, std::uint8_t instance,
                                       std::uint8_t type, bool bound);

    Transport& bus_;
    std::bitset<ShortAddress::capacity> present_{};
    std::array<Device, ShortAddress::capacity> devices_{};
};

// Part 301/303/304 default event filters (Table 3 of each part; filter
// bits 0..7 in DTR0, 8..15 in DTR1, 16..23 in DTR2).
// Push button: short press + long press start/repeat/stop + button
// stuck/free; pressed/released and double press stay off.
// Occupancy: occupied + vacant + movement + no movement; repeat off.
// Light: illuminance level.
constexpr std::uint32_t default_event_filter(std::uint8_t instance_type) noexcept {
    switch (instance_type) {
    case device::type::push_button:
        return 0xF4;
    case device::type::occupancy:
        return 0x1B;
    case device::type::light:
        return 0x01;
    default:
        return 0;
    }
}

}
