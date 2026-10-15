#include "dali/TracingTransport.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Commands.hpp"
#include "dali/Frame.hpp"
#include "dali/Scene.hpp"
#include "dali/SpecialCommands.hpp"
#include "dali/Transport.hpp"
#include "diagnostics/DaliDebugTrace.hpp"
#include "diagnostics/DiagnosticLog.hpp"
#include "diagnostics/ErrorCode.hpp"

namespace gateway::dali {
namespace {

std::uint16_t encode_addr_tag(Address address) noexcept {
    if (const auto s = address.as_short()) {
        return s->value();
    }
    if (const auto g = address.as_group()) {
        return static_cast<std::uint16_t>(0x80u | g->value());
    }
    return 0xFFu;
}

std::uint8_t gear_cmd_opcode(GearCommand command) noexcept {
    switch (command) {
    case GearCommand::Off:
        return cmd::off;
    case GearCommand::Up:
        return cmd::up;
    case GearCommand::Down:
        return cmd::down;
    case GearCommand::OnAndStepUp:
        return cmd::on_and_step_up;
    }
    return cmd::off;
}

}  // namespace

TracingTransport::TracingTransport(Transport& inner, diagnostics::DaliDebugTrace& trace,
                                   diagnostics::DiagnosticLog& log, ClockMs clock_ms) noexcept
    : inner_(inner), trace_(trace), log_(log), clock_ms_(clock_ms) {}

void TracingTransport::record_tx(diagnostics::TraceKind kind, std::uint32_t frame, std::uint8_t bits,
                                 bool twice, TransportStatus status, std::uint16_t addr) noexcept {
    const auto uptime = now_ms();
    const auto epoch = log_.current_epoch_ms(uptime);
    trace_.record(kind, frame, bits, twice, status, false, 0, epoch, uptime);
    if (status == TransportStatus::Collision) {
        char detail[48]{};
        std::snprintf(detail, sizeof(detail), "tx frame=0x%lX collision", static_cast<unsigned long>(frame));
        log_.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Warning,
                    diagnostics::ErrorCode::DaliFrameCollision, addr, detail, uptime);
    } else if (status == TransportStatus::Failure) {
        char detail[48]{};
        std::snprintf(detail, sizeof(detail), "tx frame=0x%lX bus failure",
                      static_cast<unsigned long>(frame));
        log_.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Error,
                    diagnostics::ErrorCode::DaliNoPower, addr, detail, uptime);
    }
}

void TracingTransport::record_query(diagnostics::TraceKind kind, std::uint32_t frame, std::uint8_t bits,
                                    const ByteRead& result, std::uint16_t addr) noexcept {
    const auto uptime = now_ms();
    const auto epoch = log_.current_epoch_ms(uptime);
    trace_.record(kind, frame, bits, false, result.status, result.value.has_value(), result.value.value_or(0),
                  epoch, uptime);
    if (result.status == TransportStatus::Collision) {
        char detail[48]{};
        std::snprintf(detail, sizeof(detail), "query frame=0x%lX collision",
                      static_cast<unsigned long>(frame));
        log_.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Warning,
                    diagnostics::ErrorCode::DaliFrameCollision, addr, detail, uptime);
    } else if (result.status == TransportStatus::InvalidResponse) {
        char detail[48]{};
        std::snprintf(detail, sizeof(detail), "query frame=0x%lX invalid reply",
                      static_cast<unsigned long>(frame));
        log_.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Warning,
                    diagnostics::ErrorCode::DaliInvalidReply, addr, detail, uptime);
    }
}

TransportStatus TracingTransport::set_level(Address address, ArcLevel level) {
    const auto status = inner_.set_level(address, level);
    record_tx(diagnostics::TraceKind::Command16, frame::dapc(address, level), 16, false, status,
              encode_addr_tag(address));
    return status;
}

TransportStatus TracingTransport::send(Address address, GearCommand command) {
    const auto status = inner_.send(address, command);
    record_tx(diagnostics::TraceKind::Command16, frame::command(address, gear_cmd_opcode(command)), 16, false,
              status, encode_addr_tag(address));
    return status;
}

TransportStatus TracingTransport::send_command(Address address, std::uint8_t opcode, bool twice) {
    const auto status = inner_.send_command(address, opcode, twice);
    record_tx(diagnostics::TraceKind::Command16, frame::command(address, opcode), 16, twice, status,
              encode_addr_tag(address));
    return status;
}

TransportStatus TracingTransport::set_dtr(Dtr which, std::uint8_t value) {
    const auto status = inner_.set_dtr(which, value);
    const auto opcode = which == Dtr::Dtr1   ? special::dtr1
                        : which == Dtr::Dtr2 ? special::dtr2
                                             : special::dtr0;
    record_tx(diagnostics::TraceKind::Special16, frame::special(opcode, value), 16, false, status,
              diagnostics::no_address);
    return status;
}

TransportStatus TracingTransport::enable_device_type(std::uint8_t type) {
    const auto status = inner_.enable_device_type(type);
    record_tx(diagnostics::TraceKind::Special16, frame::special(special::enable_device_type, type), 16, false,
              status, diagnostics::no_address);
    return status;
}

ByteRead TracingTransport::query(ShortAddress address, std::uint8_t opcode) {
    const auto res = inner_.query(address, opcode);
    record_query(diagnostics::TraceKind::Query16, frame::command(Address::short_address(address), opcode), 16,
                 res, address.value());
    return res;
}

TransportStatus TracingTransport::send_special(std::uint8_t opcode, std::uint8_t data, bool twice) {
    const auto status = inner_.send_special(opcode, data, twice);
    record_tx(diagnostics::TraceKind::Special16, frame::special(opcode, data), 16, twice, status,
              diagnostics::no_address);
    return status;
}

ByteRead TracingTransport::query_special(std::uint8_t opcode, std::uint8_t data) {
    const auto res = inner_.query_special(opcode, data);
    record_query(diagnostics::TraceKind::Special16, frame::special(opcode, data), 16, res,
                 diagnostics::no_address);
    return res;
}

TransportStatus TracingTransport::send_device_frame(std::uint32_t frame, bool twice) {
    const auto status = inner_.send_device_frame(frame, twice);
    record_tx(diagnostics::TraceKind::DeviceCommand24, frame, 24, twice, status, diagnostics::no_address);
    return status;
}

ByteRead TracingTransport::query_device_frame(std::uint32_t frame) {
    const auto res = inner_.query_device_frame(frame);
    record_query(diagnostics::TraceKind::DeviceQuery24, frame, 24, res, diagnostics::no_address);
    return res;
}

std::optional<BusEvent> TracingTransport::poll_event() {
    const auto ev = inner_.poll_event();
    if (ev && ev->bits == 24) {
        const auto uptime = now_ms();
        const auto epoch = log_.current_epoch_ms(uptime);
        trace_.record(diagnostics::TraceKind::UnsolicitedEvent24, ev->value, 24, false, TransportStatus::Ok,
                      false, 0, epoch, uptime);
    }
    return ev;
}

TransportStatus TracingTransport::go_to_scene(Address address, Scene scene) {
    const auto status = inner_.go_to_scene(address, scene);
    record_tx(diagnostics::TraceKind::Command16,
              frame::command(address, static_cast<std::uint8_t>(cmd::go_to_scene_0 + scene.value())), 16,
              false, status, encode_addr_tag(address));
    return status;
}

LevelRead TracingTransport::read_level(ShortAddress address) {
    const auto res = inner_.read_level(address);
    ByteRead raw{res.status, res.level ? std::optional<std::uint8_t>(res.level->value()) : std::nullopt};
    record_query(diagnostics::TraceKind::Query16,
                 frame::command(Address::short_address(address), cmd::query_actual_level), 16, raw,
                 address.value());
    return res;
}

GroupsRead TracingTransport::read_groups(ShortAddress address) {
    const auto res = inner_.read_groups(address);
    ByteRead raw{res.status, res.groups
                                 ? std::optional<std::uint8_t>(static_cast<std::uint8_t>(*res.groups & 0xFFu))
                                 : std::nullopt};
    record_query(diagnostics::TraceKind::Query16,
                 frame::command(Address::short_address(address), cmd::query_groups_0_7), 16, raw,
                 address.value());
    return res;
}

TransportStatus TracingTransport::set_group_membership(ShortAddress address, Group group, bool member) {
    const auto status = inner_.set_group_membership(address, group, member);
    const auto opcode =
        static_cast<std::uint8_t>((member ? cmd::add_to_group_0 : cmd::remove_from_group_0) + group.value());
    record_tx(diagnostics::TraceKind::Command16, frame::command(Address::short_address(address), opcode), 16,
              true, status, address.value());
    return status;
}

GearPresence TracingTransport::query_present(ShortAddress address) {
    const auto res = inner_.query_present(address);
    ByteRead raw{res.status, res.present ? std::optional<std::uint8_t>(0xFFu) : std::nullopt};
    record_query(diagnostics::TraceKind::Query16,
                 frame::command(Address::short_address(address), cmd::query_control_gear), 16, raw,
                 address.value());
    return res;
}

GearStatusRead TracingTransport::query_status(ShortAddress address) {
    const auto res = inner_.query_status(address);
    ByteRead raw{res.status, res.gear_status};
    record_query(diagnostics::TraceKind::Query16,
                 frame::command(Address::short_address(address), cmd::query_status), 16, raw,
                 address.value());
    if (res.status == TransportStatus::Ok && res.gear_status) {
        const auto st = *res.gear_status;
        const auto uptime = now_ms();
        if ((st & 0x02u) != 0) {
            char detail[48]{};
            std::snprintf(detail, sizeof(detail), "A%02u status=0x%02X lamp failure",
                          static_cast<unsigned>(address.value()), st);
            log_.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Error,
                        diagnostics::ErrorCode::DaliLampFailure, address.value(), detail, uptime);
        }
        if ((st & 0x01u) != 0) {
            char detail[48]{};
            std::snprintf(detail, sizeof(detail), "A%02u status=0x%02X ballast fault",
                          static_cast<unsigned>(address.value()), st);
            log_.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Error,
                        diagnostics::ErrorCode::DaliGearFailure, address.value(), detail, uptime);
        }
    }
    return res;
}

CommissionResult TracingTransport::commission(CommissionMode mode, ShortAddress start, unsigned max_devices) {
    return inner_.commission(mode, start, max_devices);
}

CommissionResult TracingTransport::commission_devices(CommissionMode mode, ShortAddress start,
                                                      unsigned max_devices) {
    return inner_.commission_devices(mode, start, max_devices);
}

TransportStatus TracingTransport::identify(ShortAddress address) {
    const auto status = inner_.identify(address);
    record_tx(diagnostics::TraceKind::Command16,
              frame::command(Address::short_address(address), cmd::recall_max_level), 16, false, status,
              address.value());
    return status;
}

TransportStatus TracingTransport::clear_short_addresses() {
    const auto status = inner_.clear_short_addresses();
    record_tx(diagnostics::TraceKind::Command16,
              frame::command(Address::broadcast(), cmd::store_dtr_as_short_address), 16, true, status, 0xFFu);
    return status;
}

}
