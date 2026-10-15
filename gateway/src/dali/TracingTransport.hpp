#pragma once

#include <cstdint>

#include "dali/Transport.hpp"
#include "diagnostics/DaliDebugTrace.hpp"
#include "diagnostics/DiagnosticLog.hpp"

namespace gateway::dali {

class TracingTransport final : public Transport {
public:
    using ClockMs = std::uint32_t (*)();

    TracingTransport(Transport& inner, diagnostics::DaliDebugTrace& trace, diagnostics::DiagnosticLog& log,
                     ClockMs clock_ms = nullptr) noexcept;

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
    std::uint32_t now_ms() const noexcept { return clock_ms_ ? clock_ms_() : 0u; }
    void record_tx(diagnostics::TraceKind kind, std::uint32_t frame, std::uint8_t bits, bool twice,
                   TransportStatus status, std::uint16_t addr) noexcept;
    void record_query(diagnostics::TraceKind kind, std::uint32_t frame, std::uint8_t bits,
                      const ByteRead& result, std::uint16_t addr) noexcept;

    Transport& inner_;
    diagnostics::DaliDebugTrace& trace_;
    diagnostics::DiagnosticLog& log_;
    ClockMs clock_ms_;
};

}
