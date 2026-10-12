#pragma once

#include <cstdint>
#include <optional>

#include "dali/BusPort.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

// dali::Transport on top of a BusPort (RmtDaliBus on ESP-IDF,
// SocketBusPort in the lab): encodes frames through dali::frame and maps
// link-layer outcomes to TransportStatus. Collision recovery happens in
// the link layer; RetryingTransport therefore does not re-retry collisions
// at this level.
class BusTransport final : public Transport {
public:
    // delay_ms yields the worker during RANDOMISE settling inside
    // commission(); nullptr is a no-op (tests).
    explicit BusTransport(BusPort& port, void (*delay_ms)(std::uint32_t) = nullptr);

    TransportStatus set_level(Address address, ArcLevel level) override;
    TransportStatus send(Address address, GearCommand command) override;
    TransportStatus send_command(Address address, std::uint8_t opcode, bool twice) override;
    TransportStatus set_dtr(Dtr which, std::uint8_t value) override;
    TransportStatus enable_device_type(std::uint8_t type) override;
    ByteRead query(ShortAddress address, std::uint8_t opcode) override;
    TransportStatus send_special(std::uint8_t opcode, std::uint8_t data, bool twice) override;
    ByteRead query_special(std::uint8_t opcode, std::uint8_t data) override;
    TransportStatus send_device_frame(std::uint32_t frame, bool twice) override;
    ByteRead query_device_frame(std::uint32_t frame) override;
    std::optional<BusEvent> poll_event() override;
    TransportStatus go_to_scene(Address address, Scene scene) override;
    LevelRead read_level(ShortAddress address) override;
    GroupsRead read_groups(ShortAddress address) override;
    TransportStatus set_group_membership(ShortAddress address, Group group, bool member) override;
    GearPresence query_present(ShortAddress address) override;
    GearStatusRead query_status(ShortAddress address) override;
    CommissionResult commission(CommissionMode mode, ShortAddress start, unsigned max_devices) override;
    CommissionResult commission_devices(CommissionMode mode, ShortAddress start,
                                        unsigned max_devices) override;
    TransportStatus identify(ShortAddress address) override;
    TransportStatus clear_short_addresses() override;

private:
    TransportStatus send16(std::uint16_t frame, bool twice);
    ByteRead query16(std::uint16_t frame);

    BusPort& port_;
    void (*delay_ms_)(std::uint32_t);
};

}
