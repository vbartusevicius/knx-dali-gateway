#include "lab/SocketDaliTransport.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include "dali/Frame.hpp"
#include "dali/GearAddressing.hpp"
#include "dali/devices/DeviceAddressing.hpp"
#include "dali/bus/Timing.hpp"

namespace gateway::lab {
namespace {

// IEC 62386-102 opcodes, mirrored from the pinned espressif dali driver
// (dali_command.h); the driver itself is not part of the lab build.
constexpr std::uint8_t cmd_off = 0x00;
constexpr std::uint8_t cmd_up = 0x01;
constexpr std::uint8_t cmd_down = 0x02;
constexpr std::uint8_t cmd_recall_max_level = 0x05;
constexpr std::uint8_t cmd_on_and_step_up = 0x08;
constexpr std::uint8_t cmd_go_to_scene_0 = 0x10;
constexpr std::uint8_t cmd_add_to_group_0 = 0x60;
constexpr std::uint8_t cmd_remove_from_group_0 = 0x70;
constexpr std::uint8_t cmd_store_dtr_as_short_addr = 0x80;
constexpr std::uint8_t cmd_query_status = 0x90;
constexpr std::uint8_t cmd_query_control_gear = 0x91;
constexpr std::uint8_t cmd_query_actual_level = 0xA0;
constexpr std::uint8_t cmd_query_groups_0_7 = 0xC0;
constexpr std::uint8_t cmd_query_groups_8_15 = 0xC1;
// Part 102 special commands: first byte is the opcode, second the data.
constexpr std::uint8_t special_data_transfer_reg = 0xA3;
constexpr std::uint8_t special_enable_device_type = 0xC1;
constexpr std::uint8_t special_data_transfer_reg1 = 0xC3;
constexpr std::uint8_t special_data_transfer_reg2 = 0xC5;

constexpr std::uint32_t reply_timeout_s = 10;

// Minimal field extraction for the flat sim protocol; not a general parser.
// Whitespace after ':' is tolerated (json.dumps uses ", "/": " separators).
std::size_t field_value(const std::string& line, const char* key) {
    const auto pos = line.find(key);
    if (pos == std::string::npos) {
        return std::string::npos;
    }
    const auto colon = line.find(':', pos + std::strlen(key));
    if (colon == std::string::npos) {
        return std::string::npos;
    }
    return line.find_first_not_of(" \t", colon + 1);
}

long json_int(const std::string& line, const char* key, long fallback) {
    const auto value = field_value(line, key);
    if (value == std::string::npos) {
        return fallback;
    }
    return std::strtol(line.c_str() + value, nullptr, 10);
}

std::string json_string(const std::string& line, const char* key) {
    const auto value = field_value(line, key);
    if (value == std::string::npos || line[value] != '"') {
        return {};
    }
    const auto end = line.find('"', value + 1);
    return end == std::string::npos ? std::string{} : line.substr(value + 1, end - value - 1);
}

bool json_is_null(const std::string& line, const char* key) {
    const auto value = field_value(line, key);
    return value != std::string::npos && line.compare(value, 4, "null") == 0;
}

}  // namespace

SocketDaliTransport::SocketDaliTransport(const char* host, std::uint16_t port)
    : host_(host), port_(port) {
    try_connect();
}

bool SocketDaliTransport::try_connect() {
    if (fd_ >= 0) {
        return true;
    }
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char service[8] = {};
    std::snprintf(service, sizeof(service), "%u", port_);
    addrinfo* resolved = nullptr;
    if (getaddrinfo(host_.c_str(), service, &hints, &resolved) != 0) {
        return false;
    }
    fd_ = ::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (fd_ >= 0 && ::connect(fd_, resolved->ai_addr, resolved->ai_addrlen) != 0) {
        ::close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(resolved);
    if (fd_ >= 0) {
        const timeval timeout{reply_timeout_s, 0};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    return fd_ >= 0;
}

SocketDaliTransport::~SocketDaliTransport() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void SocketDaliTransport::acknowledge_event(std::uint32_t frame) {
    char line[64];
    const auto length = std::snprintf(line, sizeof(line),
                                      "{\"op\":\"event_ack\",\"frame\":%u}\n",
                                      static_cast<unsigned>(frame));
    ::send(fd_, line, static_cast<std::size_t>(length), MSG_NOSIGNAL);
}

std::string SocketDaliTransport::next_response(bool wait) {
    for (;;) {
        const auto newline = rx_buffer_.find('\n');
        if (newline != std::string::npos) {
            std::string line = rx_buffer_.substr(0, newline);
            rx_buffer_.erase(0, newline + 1);
            const auto event = field_value(line, "\"event\"");
            if (event != std::string::npos) {
                const auto frame = json_int(line, "\"event\"", -1);
                if (events_.size() >= max_pending_events) {
                    events_.pop_front();
                }
                events_.push_back(
                    dali::BusEvent{static_cast<std::uint32_t>(frame), 24});
                acknowledge_event(static_cast<std::uint32_t>(frame));
                continue;
            }
            return line;
        }
        char chunk[256];
        const auto received =
            ::recv(fd_, chunk, sizeof(chunk), wait ? 0 : MSG_DONTWAIT);
        if (received <= 0) {
            return {};
        }
        rx_buffer_.append(chunk, static_cast<std::size_t>(received));
    }
}

SocketDaliTransport::Response SocketDaliTransport::exchange(const std::string& line) {
    if (fd_ < 0) {
        return {dali::TransportStatus::Unconfigured};
    }
    std::string wire = line + '\n';
    if (::send(fd_, wire.data(), wire.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(wire.size())) {
        return {dali::TransportStatus::Failure};
    }
    const std::string reply = next_response(true);
    if (reply.empty()) {
        return {dali::TransportStatus::Timeout};
    }
    Response response{dali::TransportStatus::Failure};
    const auto status = json_string(reply, "\"status\"");
    if (status == "ok") {
        response.status = dali::TransportStatus::Ok;
    } else if (status == "timeout") {
        response.status = dali::TransportStatus::Timeout;
    } else if (status == "collision") {
        response.status = dali::TransportStatus::Collision;
        // "reply" means the forward frame landed but the backward answers
        // collided; anything else is a forward collision worth retrying.
        response.forward_collision = json_string(reply, "\"kind\"") != "reply";
    }
    response.reply = json_is_null(reply, "\"reply\"")
                         ? -1
                         : static_cast<int>(json_int(reply, "\"reply\"", -1));
    return response;
}

SocketDaliTransport::Response SocketDaliTransport::request(const std::string& line) {
    Response response = exchange(line);
    for (std::uint8_t attempt = 1;
         response.forward_collision && attempt < dali::bus::timing::collision_attempts_max;
         ++attempt) {
        response = exchange(line);
    }
    return response;
}

dali::TransportStatus SocketDaliTransport::send_frame(std::uint32_t frame, bool twice,
                                                    bool reply) {
    char line[96];
    std::snprintf(line, sizeof(line),
                  "{\"op\":\"frame\",\"frame\":%u,\"twice\":%s,\"reply\":%s}",
                  static_cast<unsigned>(frame), twice ? "true" : "false",
                  reply ? "true" : "false");
    return request(line).status;
}

SocketDaliTransport::Response SocketDaliTransport::send_command_frame(std::uint16_t frame) {
    char line[96];
    std::snprintf(line, sizeof(line),
                  "{\"op\":\"frame\",\"frame\":%u,\"twice\":false,\"reply\":true}",
                  static_cast<unsigned>(frame));
    return request(line);
}

dali::TransportStatus SocketDaliTransport::set_level(dali::Address address, dali::ArcLevel level) {
    return send_frame(dali::frame::dapc(address, level), false, false);
}

dali::TransportStatus SocketDaliTransport::send(dali::Address address, dali::GearCommand command) {
    std::uint8_t opcode = cmd_off;
    switch (command) {
    case dali::GearCommand::Off:
        opcode = cmd_off;
        break;
    case dali::GearCommand::Up:
        opcode = cmd_up;
        break;
    case dali::GearCommand::Down:
        opcode = cmd_down;
        break;
    case dali::GearCommand::OnAndStepUp:
        opcode = cmd_on_and_step_up;
        break;
    }
    return send_frame(dali::frame::command(address, opcode), false, false);
}

dali::TransportStatus SocketDaliTransport::send_command(dali::Address address, std::uint8_t opcode,
                                                        bool twice) {
    return send_frame(dali::frame::command(address, opcode), twice, false);
}

dali::TransportStatus SocketDaliTransport::set_dtr(dali::Dtr which, std::uint8_t value) {
    const auto opcode = static_cast<std::uint8_t>(
        which == dali::Dtr::Dtr1   ? special_data_transfer_reg1
        : which == dali::Dtr::Dtr2 ? special_data_transfer_reg2
                                   : special_data_transfer_reg);
    return send_frame(dali::frame::special(opcode, value), false, false);
}

dali::TransportStatus SocketDaliTransport::enable_device_type(std::uint8_t type) {
    return send_frame(dali::frame::special(special_enable_device_type, type), false, false);
}

dali::ByteRead SocketDaliTransport::query(dali::ShortAddress address, std::uint8_t opcode) {
    const auto response =
        send_command_frame(dali::frame::command(dali::Address::short_address(address), opcode));
    if (response.status != dali::TransportStatus::Ok || response.reply < 0 ||
        response.reply > 0xFF) {
        return {response.status, std::nullopt};
    }
    return {dali::TransportStatus::Ok, static_cast<std::uint8_t>(response.reply)};
}

dali::TransportStatus SocketDaliTransport::go_to_scene(dali::Address address, dali::Scene scene) {
    return send_frame(dali::frame::command(address, cmd_go_to_scene_0 + scene.value()), false,
                      false);
}

dali::LevelRead SocketDaliTransport::read_level(dali::ShortAddress address) {
    const auto response =
        send_command_frame(dali::frame::command(dali::Address::short_address(address),
                                                cmd_query_actual_level));
    if (response.status != dali::TransportStatus::Ok || response.reply < 0) {
        return {response.status, std::nullopt};
    }
    const auto level = dali::ArcLevel::from(static_cast<unsigned>(response.reply));
    if (!level) {
        return {dali::TransportStatus::InvalidResponse, std::nullopt};
    }
    return {dali::TransportStatus::Ok, *level};
}

dali::GroupsRead SocketDaliTransport::read_groups(dali::ShortAddress address) {
    const auto low = send_command_frame(
        dali::frame::command(dali::Address::short_address(address), cmd_query_groups_0_7));
    if (low.status != dali::TransportStatus::Ok || low.reply < 0) {
        return {low.status, std::nullopt};
    }
    const auto high = send_command_frame(
        dali::frame::command(dali::Address::short_address(address), cmd_query_groups_8_15));
    if (high.status != dali::TransportStatus::Ok || high.reply < 0) {
        return {high.status, std::nullopt};
    }
    return {dali::TransportStatus::Ok,
            static_cast<std::uint16_t>((high.reply << 8) | low.reply)};
}

dali::TransportStatus SocketDaliTransport::set_group_membership(dali::ShortAddress address,
                                                                dali::Group group, bool member) {
    const auto opcode = static_cast<std::uint8_t>(
        (member ? cmd_add_to_group_0 : cmd_remove_from_group_0) + group.value());
    return send_frame(dali::frame::command(dali::Address::short_address(address), opcode), true,
                      false);
}

dali::GearPresence SocketDaliTransport::query_present(dali::ShortAddress address) {
    const auto response = send_command_frame(
        dali::frame::command(dali::Address::short_address(address), cmd_query_control_gear));
    return {response.status, response.reply >= 0};
}

dali::GearStatusRead SocketDaliTransport::query_status(dali::ShortAddress address) {
    const auto response = send_command_frame(
        dali::frame::command(dali::Address::short_address(address), cmd_query_status));
    if (response.status != dali::TransportStatus::Ok || response.reply < 0) {
        return {response.status, std::nullopt};
    }
    return {dali::TransportStatus::Ok, static_cast<std::uint8_t>(response.reply)};
}

dali::CommissionResult SocketDaliTransport::commission(dali::CommissionMode mode,
                                                     dali::ShortAddress start,
                                                     unsigned max_devices) {
    dali::GearAddressing addressing{*this, [](std::uint32_t ms) {
                                        std::this_thread::sleep_for(
                                            std::chrono::milliseconds(ms));
                                    }};
    return addressing.commission(mode, start, max_devices);
}

dali::CommissionResult SocketDaliTransport::commission_devices(
    dali::CommissionMode mode, dali::ShortAddress start, unsigned max_devices) {
    dali::DeviceAddressing addressing{*this, [](std::uint32_t ms) {
                                          std::this_thread::sleep_for(
                                              std::chrono::milliseconds(ms));
                                      }};
    return addressing.commission(mode, start, max_devices);
}

dali::TransportStatus SocketDaliTransport::identify(dali::ShortAddress address) {
    // Like the firmware adapter: RECALL MAX LEVEL stands in for IDENTIFY.
    return send_frame(
        dali::frame::command(dali::Address::short_address(address), cmd_recall_max_level), false,
        false);
}

dali::TransportStatus SocketDaliTransport::clear_short_addresses() {
    const auto loaded =
        send_frame(dali::frame::special(special_data_transfer_reg, 0xFF), false, false);
    if (loaded != dali::TransportStatus::Ok) {
        return loaded;
    }
    return send_frame(dali::frame::command(dali::Address::broadcast(),
                                         cmd_store_dtr_as_short_addr),
                      true, false);
}

dali::TransportStatus SocketDaliTransport::send_special(std::uint8_t opcode, std::uint8_t data,
                                                      bool twice) {
    return send_frame(dali::frame::special(opcode, data), twice, false);
}

dali::ByteRead SocketDaliTransport::query_special(std::uint8_t opcode, std::uint8_t data) {
    const auto response = send_command_frame(dali::frame::special(opcode, data));
    if (response.status != dali::TransportStatus::Ok || response.reply < 0 ||
        response.reply > 0xFF) {
        return {response.status, std::nullopt};
    }
    return {dali::TransportStatus::Ok, static_cast<std::uint8_t>(response.reply)};
}

dali::TransportStatus SocketDaliTransport::send_device_frame(std::uint32_t frame,
                                                             bool twice) {
    return send_frame(frame, twice, false);
}

dali::ByteRead SocketDaliTransport::query_device_frame(std::uint32_t frame) {
    char line[96];
    std::snprintf(line, sizeof(line),
                  "{\"op\":\"frame\",\"frame\":%u,\"twice\":false,\"reply\":true}",
                  static_cast<unsigned>(frame));
    const auto response = request(line);
    if (response.status != dali::TransportStatus::Ok || response.reply < 0 ||
        response.reply > 0xFF) {
        return {response.status, std::nullopt};
    }
    return {dali::TransportStatus::Ok, static_cast<std::uint8_t>(response.reply)};
}

std::optional<dali::BusEvent> SocketDaliTransport::poll_event() {
    if (fd_ < 0) {
        return std::nullopt;
    }
    // Drain everything pending; response lines found here are stale and
    // dropped — requests are synchronous and always consumed by request().
    while (!next_response(false).empty()) {
    }
    if (events_.empty()) {
        return std::nullopt;
    }
    const auto event = events_.front();
    events_.pop_front();
    return event;
}

}  // namespace gateway::lab
