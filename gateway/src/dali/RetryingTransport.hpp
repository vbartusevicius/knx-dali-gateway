#pragma once

#include "dali/Transport.hpp"

namespace gateway::dali {

// Decorator that retries only reads, on the transient statuses Timeout,
// Collision and InvalidResponse — reads are idempotent, so one more
// bounded pass is safe when the link layer's own collision recovery has
// run out. Writes pass every status through unmodified: their collision
// recovery (settle, break, re-arbitration) is owned by the bus link layer
// below, and retrying here would multiply its attempts. At most
// max_attempts transactions per call; commission() and identify() are
// never retried.
class RetryingTransport final : public Transport {
public:
    static constexpr unsigned max_attempts = 3;

    explicit RetryingTransport(Transport& inner) noexcept : inner_(inner) {}

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
    static bool retryable_read(TransportStatus status) noexcept;
    template <typename Call>
    auto read(Call call) -> decltype(call());

    Transport& inner_;
};

}
