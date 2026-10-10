#include "espidf/DaliTransport.hpp"

namespace gateway::espidf {
namespace {

dali::TransportStatus transport_status(esp_err_t error) {
    if (error == ESP_OK) {
        return dali::TransportStatus::Ok;
    }
    return error == ESP_ERR_TIMEOUT ? dali::TransportStatus::Timeout : dali::TransportStatus::Failure;
}

void map_address(dali::Address address, dali_master_transaction_config_t& config) {
    switch (address.kind()) {
        case dali::AddressKind::Short:
            config.addr_type = DALI_ADDR_SHORT;
            config.addr = address.as_short()->value();
            break;
        case dali::AddressKind::Group:
            config.addr_type = DALI_ADDR_GROUP;
            config.addr = address.as_group()->value();
            break;
        case dali::AddressKind::Broadcast:
            config.addr_type = DALI_ADDR_BROADCAST;
            config.addr = 0;
            break;
    }
}

dali_master_transaction_config_t transaction(dali::Address address, bool command, std::uint8_t value,
                                             bool send_twice) {
    dali_master_transaction_config_t config{};
    map_address(address, config);
    config.is_cmd = command;
    config.command = value;
    config.send_twice = send_twice;
    config.tx_timeout_ms = DALI_TX_TIMEOUT_MS;
    return config;
}

std::uint8_t gear_command_byte(dali::GearCommand command) {
    switch (command) {
        case dali::GearCommand::Off:
            return DALI_CMD_OFF;
        case dali::GearCommand::Up:
            return DALI_CMD_UP;
        case dali::GearCommand::Down:
            return DALI_CMD_DOWN;
        case dali::GearCommand::OnAndStepUp:
            return DALI_CMD_ON_AND_STEP_UP;
    }
    return DALI_CMD_OFF;
}

}

DaliTransport::DaliTransport(dali_master_handle_t owned_handle) noexcept : handle_(owned_handle) {}

DaliTransport::~DaliTransport() {
    if (handle_) {
        dali_del_master(handle_);
    }
}

bool DaliTransport::configured() const noexcept {
    return handle_ != nullptr;
}

dali::TransportStatus DaliTransport::run(dali::Address address, std::uint8_t command, bool send_twice) {
    const auto config = transaction(address, true, command, send_twice);
    return transport_status(dali_master_do_transaction(handle_, &config, nullptr));
}

dali::TransportStatus DaliTransport::query(dali::ShortAddress address, std::uint8_t command,
                                           std::optional<std::uint8_t>& reply) {
    const auto config = transaction(dali::Address::short_address(address), true, command, false);
    int response = DALI_RESULT_NO_REPLY;
    const auto status = transport_status(dali_master_do_transaction(handle_, &config, &response));
    if (status != dali::TransportStatus::Ok) {
        return status;
    }
    if (response == DALI_RESULT_NO_REPLY) {
        return dali::TransportStatus::Timeout;
    }
    if (response < 0 || response > 0xFF) {
        return dali::TransportStatus::InvalidResponse;
    }
    reply = static_cast<std::uint8_t>(response);
    return dali::TransportStatus::Ok;
}

dali::TransportStatus DaliTransport::set_level(dali::Address address, dali::ArcLevel level) {
    if (!configured()) {
        return dali::TransportStatus::Unconfigured;
    }
    const auto config = transaction(address, false, level.value(), false);
    return transport_status(dali_master_do_transaction(handle_, &config, nullptr));
}

dali::TransportStatus DaliTransport::send(dali::Address address, dali::GearCommand command) {
    if (!configured()) {
        return dali::TransportStatus::Unconfigured;
    }
    return run(address, gear_command_byte(command), false);
}

dali::TransportStatus DaliTransport::go_to_scene(dali::Address address, dali::Scene scene) {
    if (!configured()) {
        return dali::TransportStatus::Unconfigured;
    }
    return run(address, static_cast<std::uint8_t>(DALI_CMD_GO_TO_SCENE_0 + scene.value()), false);
}

dali::TransportStatus DaliTransport::store_scene(dali::Address address, dali::Scene scene) {
    if (!configured()) {
        return dali::TransportStatus::Unconfigured;
    }
    const auto stored = run(address, DALI_CMD_STORE_ACTUAL_LEVEL, true);
    if (stored != dali::TransportStatus::Ok) {
        return stored;
    }
    return run(address, static_cast<std::uint8_t>(DALI_CMD_STORE_DTR_AS_SCENE_0 + scene.value()), true);
}

dali::LevelRead DaliTransport::read_level(dali::ShortAddress address) {
    if (!configured()) {
        return {dali::TransportStatus::Unconfigured, std::nullopt};
    }
    std::optional<std::uint8_t> response;
    const auto status = query(address, DALI_CMD_QUERY_ACTUAL_LEVEL, response);
    if (status != dali::TransportStatus::Ok) {
        return {status, std::nullopt};
    }
    const auto level = dali::ArcLevel::from(*response);
    return {level ? dali::TransportStatus::Ok : dali::TransportStatus::InvalidResponse, level};
}

dali::GroupsRead DaliTransport::read_groups(dali::ShortAddress address) {
    if (!configured()) {
        return {dali::TransportStatus::Unconfigured, std::nullopt};
    }
    std::optional<std::uint8_t> low;
    auto status = query(address, DALI_CMD_QUERY_GROUPS_0_7, low);
    if (status != dali::TransportStatus::Ok) {
        return {status, std::nullopt};
    }
    std::optional<std::uint8_t> high;
    status = query(address, DALI_CMD_QUERY_GROUPS_8_15, high);
    if (status != dali::TransportStatus::Ok) {
        return {status, std::nullopt};
    }
    return {dali::TransportStatus::Ok,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(*low | (*high << 8))}};
}

dali::TransportStatus DaliTransport::set_group_membership(dali::ShortAddress address, dali::Group group,
                                                        bool member) {
    if (!configured()) {
        return dali::TransportStatus::Unconfigured;
    }
    const auto command = static_cast<std::uint8_t>(
        (member ? DALI_CMD_ADD_TO_GROUP_0 : DALI_CMD_REMOVE_FROM_GROUP_0) + group.value());
    return run(dali::Address::short_address(address), command, true);
}

}
