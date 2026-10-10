#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "dali/Transport.hpp"

namespace gateway::lab {

// dali::Transport over TCP to the DALI bus simulator. Newline-delimited JSON:
//   -> {"op":"frame","frame":<uint16>,"twice":<bool>,"reply":<bool>}
//   <- {"status":"ok"|"timeout"|"collision"|"error","reply":<int|null>,
//       "kind":"forward"|"reply"}
// The simulator may also push unsolicited 24-bit device frames on the same
// connection:
//   <- {"event":<uint24>}
// which the transport acknowledges (-> {"op":"event_ack","frame":<n>}) so the
// simulator can tell the gateway actually consumed it, and queues as
// BusEvents for poll_event(). Commissioning runs the portable
// dali::GearAddressing sequence over these frames — there is no dedicated
// simulator operation for it.
// All bus work is serialized by the single caller (the worker thread); the
// socket is blocking with a reply timeout.
class SocketDaliTransport final : public dali::Transport {
public:
    // Stores host/port and tries to connect; try_connect() retries later —
    // the simulator may start after the gateway. Requests report Unconfigured
    // while disconnected.
    SocketDaliTransport(const char* host, std::uint16_t port);
    ~SocketDaliTransport() override;

    SocketDaliTransport(const SocketDaliTransport&) = delete;
    SocketDaliTransport& operator=(const SocketDaliTransport&) = delete;

    bool try_connect();
    bool connected() const noexcept { return fd_ >= 0; }

    dali::TransportStatus set_level(dali::Address address, dali::ArcLevel level) override;
    dali::TransportStatus send(dali::Address address, dali::GearCommand command) override;
    dali::TransportStatus send_command(dali::Address address, std::uint8_t opcode, bool twice) override;
    dali::TransportStatus set_dtr(dali::Dtr which, std::uint8_t value) override;
    dali::TransportStatus enable_device_type(std::uint8_t type) override;
    dali::ByteRead query(dali::ShortAddress address, std::uint8_t opcode) override;
    dali::TransportStatus go_to_scene(dali::Address address, dali::Scene scene) override;
    dali::LevelRead read_level(dali::ShortAddress address) override;
    dali::GroupsRead read_groups(dali::ShortAddress address) override;
    dali::TransportStatus set_group_membership(dali::ShortAddress address, dali::Group group,
                                               bool member) override;
    dali::GearPresence query_present(dali::ShortAddress address) override;
    dali::GearStatusRead query_status(dali::ShortAddress address) override;
    dali::CommissionResult commission(dali::CommissionMode mode, dali::ShortAddress start,
                                      unsigned max_devices) override;
    dali::CommissionResult commission_devices(dali::CommissionMode mode,
                                              dali::ShortAddress start,
                                              unsigned max_devices) override;
    dali::TransportStatus identify(dali::ShortAddress address) override;
    dali::TransportStatus clear_short_addresses() override;
    dali::TransportStatus send_special(std::uint8_t opcode, std::uint8_t data,
                                       bool twice) override;
    dali::ByteRead query_special(std::uint8_t opcode, std::uint8_t data) override;
    dali::TransportStatus send_device_frame(std::uint32_t frame, bool twice) override;
    dali::ByteRead query_device_frame(std::uint32_t frame) override;
    std::optional<dali::BusEvent> poll_event() override;

private:
    struct Response {
        dali::TransportStatus status;
        int reply = -1;              // -1: no backward frame
        bool forward_collision = false;
    };

    static constexpr std::size_t max_pending_events = 16;

    dali::TransportStatus send_frame(std::uint32_t frame, bool twice, bool reply);
    Response send_command_frame(std::uint16_t frame);
    // One request/response round on the socket.
    Response exchange(const std::string& line);
    // exchange() plus the bounded collision retry this transport owes in
    // place of the link layer it replaces in the lab: a forward-frame
    // collision is retransmitted, a backward-reply collision is not.
    Response request(const std::string& line);
    // Moves complete lines out of the socket; pushed events are queued and
    // acknowledged, the first response line is returned. Blocks only when
    // wait is true.
    std::string next_response(bool wait);
    void acknowledge_event(std::uint32_t frame);

    std::string host_;
    std::uint16_t port_;
    int fd_ = -1;
    std::string rx_buffer_;
    std::deque<dali::BusEvent> events_;
};

}
