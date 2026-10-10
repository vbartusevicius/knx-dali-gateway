#include "dali/BusTransport.hpp"

#include "dali/Commands.hpp"
#include "dali/Frame.hpp"
#include "dali/GearAddressing.hpp"
#include "dali/SpecialCommands.hpp"
#include "dali/bus/Timing.hpp"
#include "dali/devices/DeviceAddressing.hpp"

namespace gateway::dali {

namespace {

constexpr bus::Priority frame_priority = bus::Priority::P2;

TransportStatus outcome_status(bus::TxOutcome outcome) {
    switch (outcome) {
    case bus::TxOutcome::Sent:
    case bus::TxOutcome::Reply:
        return TransportStatus::Ok;
    case bus::TxOutcome::NoReply:
        return TransportStatus::Timeout;
    case bus::TxOutcome::ReplyCollision:
    case bus::TxOutcome::Collision:
        return TransportStatus::Collision;
    }
    return TransportStatus::Failure;
}

std::uint8_t gear_opcode(GearCommand command) {
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

BusTransport::BusTransport(BusPort& port, void (*delay_ms)(std::uint32_t))
    : port_(port), delay_ms_(delay_ms) {}

TransportStatus BusTransport::send16(std::uint16_t frame, bool twice) {
    const auto result = port_.transact(BusRequest{
        .value = frame,
        .bits = 16,
        .expect_reply = false,
        .priority = frame_priority,
        .repeat_gap_us = twice ? bus::timing::send_twice_gap_us : 0u,
    });
    return outcome_status(result.outcome);
}

ByteRead BusTransport::query16(std::uint16_t frame) {
    const auto result = port_.transact(BusRequest{
        .value = frame,
        .bits = 16,
        .expect_reply = true,
        .priority = frame_priority,
        .repeat_gap_us = 0,
    });
    switch (result.outcome) {
    case bus::TxOutcome::Reply:
        return {TransportStatus::Ok, result.reply};
    case bus::TxOutcome::NoReply:
        return {TransportStatus::Timeout, std::nullopt};
    case bus::TxOutcome::ReplyCollision:
    case bus::TxOutcome::Collision:
        return {TransportStatus::Collision, std::nullopt};
    case bus::TxOutcome::Sent:
        break;
    }
    return {TransportStatus::Failure, std::nullopt};
}

TransportStatus BusTransport::set_level(Address address, ArcLevel level) {
    return send16(frame::dapc(address, level), false);
}

TransportStatus BusTransport::send(Address address, GearCommand command) {
    return send16(frame::command(address, gear_opcode(command)), false);
}

TransportStatus BusTransport::send_command(Address address, std::uint8_t opcode,
                                           bool twice) {
    return send16(frame::command(address, opcode), twice);
}

TransportStatus BusTransport::set_dtr(Dtr which, std::uint8_t value) {
    const auto opcode = which == Dtr::Dtr1   ? special::dtr1
                        : which == Dtr::Dtr2 ? special::dtr2
                                           : special::dtr0;
    return send16(frame::special(opcode, value), false);
}

TransportStatus BusTransport::enable_device_type(std::uint8_t type) {
    return send16(frame::special(special::enable_device_type, type), false);
}

ByteRead BusTransport::query(ShortAddress address, std::uint8_t opcode) {
    return query16(
        frame::command(Address::short_address(address), opcode));
}

TransportStatus BusTransport::send_special(std::uint8_t opcode, std::uint8_t data,
                                           bool twice) {
    return send16(frame::special(opcode, data), twice);
}

ByteRead BusTransport::query_special(std::uint8_t opcode, std::uint8_t data) {
    return query16(frame::special(opcode, data));
}

TransportStatus BusTransport::send_device_frame(std::uint32_t frame, bool twice) {
    const auto result = port_.transact(BusRequest{
        .value = frame,
        .bits = 24,
        .expect_reply = false,
        .priority = frame_priority,
        .repeat_gap_us = twice ? bus::timing::send_twice_gap_us : 0u,
    });
    return outcome_status(result.outcome);
}

ByteRead BusTransport::query_device_frame(std::uint32_t frame) {
    const auto result = port_.transact(BusRequest{
        .value = frame,
        .bits = 24,
        .expect_reply = true,
        .priority = frame_priority,
        .repeat_gap_us = 0,
    });
    switch (result.outcome) {
    case bus::TxOutcome::Reply:
        return {TransportStatus::Ok, result.reply};
    case bus::TxOutcome::NoReply:
        return {TransportStatus::Timeout, std::nullopt};
    case bus::TxOutcome::ReplyCollision:
    case bus::TxOutcome::Collision:
        return {TransportStatus::Collision, std::nullopt};
    case bus::TxOutcome::Sent:
        break;
    }
    return {TransportStatus::Failure, std::nullopt};
}

std::optional<BusEvent> BusTransport::poll_event() {
    return port_.poll_event();
}

TransportStatus BusTransport::go_to_scene(Address address, Scene scene) {
    return send16(frame::command(address, cmd::go_to_scene_0 + scene.value()),
                  false);
}

LevelRead BusTransport::read_level(ShortAddress address) {
    const auto read = query(address, cmd::query_actual_level);
    if (read.status != TransportStatus::Ok || !read.value) {
        return {read.status, std::nullopt};
    }
    const auto level = ArcLevel::from(*read.value);
    if (!level) {
        return {TransportStatus::InvalidResponse, std::nullopt};
    }
    return {TransportStatus::Ok, *level};
}

GroupsRead BusTransport::read_groups(ShortAddress address) {
    const auto low = query(address, cmd::query_groups_0_7);
    if (low.status != TransportStatus::Ok || !low.value) {
        return {low.status, std::nullopt};
    }
    const auto high = query(address, cmd::query_groups_8_15);
    if (high.status != TransportStatus::Ok || !high.value) {
        return {high.status, std::nullopt};
    }
    return {TransportStatus::Ok,
            static_cast<std::uint16_t>((*high.value << 8) | *low.value)};
}

TransportStatus BusTransport::set_group_membership(ShortAddress address,
                                                   Group group, bool member) {
    const auto opcode = static_cast<std::uint8_t>(
        (member ? cmd::add_to_group_0 : cmd::remove_from_group_0) +
        group.value());
    return send16(
        frame::command(Address::short_address(address), opcode), true);
}

GearPresence BusTransport::query_present(ShortAddress address) {
    const auto read = query(address, cmd::query_control_gear);
    return {read.status, read.status == TransportStatus::Ok && read.value.has_value()};
}

GearStatusRead BusTransport::query_status(ShortAddress address) {
    const auto read = query(address, cmd::query_status);
    if (read.status != TransportStatus::Ok || !read.value) {
        return {read.status, std::nullopt};
    }
    return {TransportStatus::Ok, *read.value};
}

CommissionResult BusTransport::commission(CommissionMode mode, ShortAddress start,
                                          unsigned max_devices) {
    GearAddressing addressing{*this, delay_ms_};
    return addressing.commission(mode, start, max_devices);
}

CommissionResult BusTransport::commission_devices(CommissionMode mode,
                                                  ShortAddress start,
                                                  unsigned max_devices) {
    DeviceAddressing addressing{*this, delay_ms_};
    return addressing.commission(mode, start, max_devices);
}

TransportStatus BusTransport::identify(ShortAddress address) {
    return send16(frame::command(Address::short_address(address),
                                 cmd::recall_max_level),
                  false);
}

TransportStatus BusTransport::clear_short_addresses() {
    const auto loaded = send16(frame::special(special::dtr0, 0xFF), false);
    if (loaded != TransportStatus::Ok) {
        return loaded;
    }
    return send16(frame::command(Address::broadcast(),
                                 cmd::store_dtr_as_short_address),
                  true);
}

}
