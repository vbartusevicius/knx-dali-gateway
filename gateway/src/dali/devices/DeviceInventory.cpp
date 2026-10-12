#include "dali/devices/DeviceInventory.hpp"

#include <cstdint>
#include <optional>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "dali/devices/DeviceFrames.hpp"

namespace gateway::dali {

namespace {

ByteRead device_query(Transport& bus, std::uint8_t device, std::uint8_t opcode) {
    return bus.query_device_frame(
        device::command(device::address_short(device), device::instance_device, opcode));
}

ByteRead instance_query(Transport& bus, std::uint8_t device, std::uint8_t instance, std::uint8_t opcode) {
    return bus.query_device_frame(
        device::command(device::address_short(device), device::instance_number(instance), opcode));
}

}  // namespace

std::optional<std::uint8_t> DeviceInventory::instance_count(std::uint8_t device) const noexcept {
    if (!present_.test(device)) {
        return std::nullopt;
    }
    return devices_[device].count;
}

std::optional<std::uint8_t> DeviceInventory::instance_type(std::uint8_t device,
                                                           std::uint8_t instance) const noexcept {
    if (!present_.test(device) || instance >= devices_[device].count) {
        return std::nullopt;
    }
    return devices_[device].types[instance];
}

std::optional<std::uint8_t> DeviceInventory::resolution(std::uint8_t device,
                                                        std::uint8_t instance) const noexcept {
    if (!present_.test(device) || instance >= devices_[device].count ||
        devices_[device].types[instance] != device::type::light) {
        return std::nullopt;
    }
    const auto value = devices_[device].resolutions[instance];
    return value == 0 ? std::nullopt : std::optional<std::uint8_t>{value};
}

bool DeviceInventory::has_type(std::uint8_t device, std::uint8_t type) const noexcept {
    if (!present_.test(device)) {
        return false;
    }
    const auto& entry = devices_[device];
    for (std::uint8_t instance = 0; instance < entry.count; ++instance) {
        if (entry.types[instance] == type) {
            return true;
        }
    }
    return false;
}

TransportStatus DeviceInventory::scan() {
    auto error = TransportStatus::Ok;
    for (unsigned index = 0; index < ShortAddress::capacity; ++index) {
        const auto device = static_cast<std::uint8_t>(index);
        const auto reply = device_query(bus_, device, device::cmd::query_number_of_instances);
        if (reply.status == TransportStatus::Timeout) {
            present_.set(index, false);
            continue;
        }
        if (reply.status != TransportStatus::Ok || !reply.value) {
            if (error == TransportStatus::Ok) {
                error = reply.status;
            }
            continue;
        }
        present_.set(index, true);
        auto& entry = devices_[index];
        entry = Device{};
        entry.count = *reply.value <= max_instances ? *reply.value : max_instances;
        for (std::uint8_t instance = 0; instance < entry.count; ++instance) {
            const auto type = instance_query(bus_, device, instance, device::inst::query_instance_type);
            if (type.status == TransportStatus::Ok && type.value) {
                entry.types[instance] = *type.value;
            } else if (error == TransportStatus::Ok && type.status != TransportStatus::Timeout) {
                error = type.status;
            }
            if (entry.types[instance] == device::type::light) {
                const auto resolution =
                    instance_query(bus_, device, instance, device::inst::query_resolution);
                if (resolution.status == TransportStatus::Ok && resolution.value) {
                    entry.resolutions[instance] = *resolution.value;
                }
            }
        }
    }
    return error;
}

TransportStatus DeviceInventory::configure_instance(std::uint8_t device_addr, std::uint8_t instance,
                                                    std::uint8_t type, bool bound) {
    const auto send = [&](std::uint32_t frame, bool twice) { return bus_.send_device_frame(frame, twice); };
    auto status = send(device::special::dtr0(device::event_scheme_device_instance), false);
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = send(device::command(device::address_short(device_addr), device::instance_number(instance),
                                  device::inst::set_event_scheme),
                  true);
    if (status != TransportStatus::Ok) {
        return status;
    }
    const std::uint32_t filter = default_event_filter(type);
    const std::uint32_t dtr_frames[3] = {
        device::special::dtr0(static_cast<std::uint8_t>(filter)),
        device::special::dtr1(static_cast<std::uint8_t>(filter >> 8)),
        device::special::dtr2(static_cast<std::uint8_t>(filter >> 16)),
    };
    for (const auto frame : dtr_frames) {
        status = send(frame, false);
        if (status != TransportStatus::Ok) {
            return status;
        }
    }
    status = send(device::command(device::address_short(device_addr), device::instance_number(instance),
                                  device::inst::set_event_filter),
                  true);
    if (status != TransportStatus::Ok) {
        return status;
    }
    return send(device::command(device::address_short(device_addr), device::instance_number(instance),
                                bound ? device::inst::enable_instance : device::inst::disable_instance),
                true);
}

TransportStatus DeviceInventory::apply(bool (*bound)(std::uint8_t, std::uint8_t, void*), void* context) {
    auto error = TransportStatus::Ok;
    for (std::uint8_t device = 0; device < ShortAddress::capacity; ++device) {
        if (!present_.test(device)) {
            continue;
        }
        const auto& entry = devices_[device];
        for (std::uint8_t instance = 0; instance < entry.count; ++instance) {
            const auto status = configure_instance(device, instance, entry.types[instance],
                                                   bound != nullptr && bound(device, instance, context));
            if (status != TransportStatus::Ok && error == TransportStatus::Ok) {
                error = status;
            }
        }
    }
    return error;
}

}
