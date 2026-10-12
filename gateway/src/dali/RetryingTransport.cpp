#include "dali/RetryingTransport.hpp"

#include <cstdint>
#include <optional>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Frame.hpp"
#include "dali/Group.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

bool RetryingTransport::retryable_read(TransportStatus status) noexcept {
    return status == TransportStatus::Timeout || status == TransportStatus::Collision ||
           status == TransportStatus::InvalidResponse;
}

template <typename Call>
auto RetryingTransport::read(Call call) -> decltype(call()) {
    auto result = call();
    for (unsigned attempt = 1; retryable_read(result.status) && attempt < max_attempts; ++attempt) {
        result = call();
    }
    return result;
}

TransportStatus RetryingTransport::set_level(Address address, ArcLevel level) {
    return inner_.set_level(address, level);
}

TransportStatus RetryingTransport::send(Address address, GearCommand command) {
    return inner_.send(address, command);
}

TransportStatus RetryingTransport::send_command(Address address, std::uint8_t opcode, bool twice) {
    return inner_.send_command(address, opcode, twice);
}

TransportStatus RetryingTransport::set_dtr(Dtr which, std::uint8_t value) {
    return inner_.set_dtr(which, value);
}

TransportStatus RetryingTransport::enable_device_type(std::uint8_t type) {
    return inner_.enable_device_type(type);
}

ByteRead RetryingTransport::query(ShortAddress address, std::uint8_t opcode) {
    return read([&] { return inner_.query(address, opcode); });
}

TransportStatus RetryingTransport::send_special(std::uint8_t opcode, std::uint8_t data, bool twice) {
    return inner_.send_special(opcode, data, twice);
}

ByteRead RetryingTransport::query_special(std::uint8_t opcode, std::uint8_t data) {
    return read([&] { return inner_.query_special(opcode, data); });
}

TransportStatus RetryingTransport::send_device_frame(std::uint32_t frame, bool twice) {
    return inner_.send_device_frame(frame, twice);
}

ByteRead RetryingTransport::query_device_frame(std::uint32_t frame) {
    return read([&] { return inner_.query_device_frame(frame); });
}

std::optional<BusEvent> RetryingTransport::poll_event() {
    return inner_.poll_event();
}

TransportStatus RetryingTransport::go_to_scene(Address address, Scene scene) {
    return inner_.go_to_scene(address, scene);
}

LevelRead RetryingTransport::read_level(ShortAddress address) {
    return read([&] { return inner_.read_level(address); });
}

GroupsRead RetryingTransport::read_groups(ShortAddress address) {
    return read([&] { return inner_.read_groups(address); });
}

TransportStatus RetryingTransport::set_group_membership(ShortAddress address, Group group, bool member) {
    return inner_.set_group_membership(address, group, member);
}

GearPresence RetryingTransport::query_present(ShortAddress address) {
    return read([&] { return inner_.query_present(address); });
}

GearStatusRead RetryingTransport::query_status(ShortAddress address) {
    return read([&] { return inner_.query_status(address); });
}

CommissionResult RetryingTransport::commission(CommissionMode mode, ShortAddress start,
                                               unsigned max_devices) {
    return inner_.commission(mode, start, max_devices);
}

CommissionResult RetryingTransport::commission_devices(CommissionMode mode, ShortAddress start,
                                                       unsigned max_devices) {
    return inner_.commission_devices(mode, start, max_devices);
}

TransportStatus RetryingTransport::identify(ShortAddress address) {
    return inner_.identify(address);
}

TransportStatus RetryingTransport::clear_short_addresses() {
    return inner_.clear_short_addresses();
}

}
