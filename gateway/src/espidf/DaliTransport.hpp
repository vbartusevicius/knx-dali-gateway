#pragma once

#include <dali.h>

#include "dali/Transport.hpp"

namespace gateway::espidf {

// dali::Transport implemented with Espressif's espressif/dali driver (RMT peripheral).
// Owns the driver handle and maps SDK errors to TransportStatus; a null handle means DALI is disabled.
class DaliTransport final : public dali::Transport {
public:
    explicit DaliTransport(dali_master_handle_t owned_handle) noexcept;
    ~DaliTransport() override;
    DaliTransport(const DaliTransport&) = delete;
    DaliTransport& operator=(const DaliTransport&) = delete;

    bool configured() const noexcept;
    dali::TransportStatus set_level(dali::Address address, dali::ArcLevel level) override;
    dali::TransportStatus send(dali::Address address, dali::GearCommand command) override;
    dali::TransportStatus go_to_scene(dali::Address address, dali::Scene scene) override;
    dali::TransportStatus store_scene(dali::Address address, dali::Scene scene) override;
    dali::LevelRead read_level(dali::ShortAddress address) override;
    dali::GroupsRead read_groups(dali::ShortAddress address) override;
    dali::TransportStatus set_group_membership(dali::ShortAddress address, dali::Group group, bool member) override;

private:
    dali::TransportStatus run(dali::Address address, std::uint8_t command, bool send_twice);
    dali::TransportStatus query(dali::ShortAddress address, std::uint8_t command, std::optional<std::uint8_t>& reply);

    dali_master_handle_t handle_;
};

}
