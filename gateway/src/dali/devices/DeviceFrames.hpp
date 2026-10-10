#pragma once

#include <cstdint>

// IEC 62386-103 24-bit forward frame encoding for control devices
// (DALI-2 input devices). A command frame is {device address, instance,
// opcode}; an event frame has bit 16 clear instead of set.
// Device address byte (bits 23:16): bit 16 = 1 for commands, the address in
// bits 23:17 — 0AAAAAAA short, 10GGGGG device group, 0x7F broadcast,
// 0x7E broadcast unaddressed.
// Instance byte (bits 15:8): 000 nnnnn instance number, 100 ggggg instance
// group, 110 ttttt instance type, plus the broadcast selectors 0xFE
// ("device", device-level commands of Table 21) and 0xFF (all instances).
// Opcodes are IEC 62386-103 Table 21 (device- and instance-level commands)
// and Table 22 (special commands, first byte 0xC1/0xC5/0xC7/0xC9, second
// byte the operation); cross-checked against python-dali's
// dali/device/general.py.
namespace gateway::dali::device {

constexpr std::uint8_t address_short(std::uint8_t short_address) noexcept {
    return static_cast<std::uint8_t>((short_address << 1) | 0x01);
}

constexpr std::uint8_t address_group(std::uint8_t group) noexcept {
    return static_cast<std::uint8_t>(0x80 | (group << 1) | 0x01);
}

inline constexpr std::uint8_t address_broadcast = 0xFF;
inline constexpr std::uint8_t address_unaddressed = 0xFD;

constexpr std::uint8_t instance_number(std::uint8_t number) noexcept {
    return number;
}

constexpr std::uint8_t instance_group(std::uint8_t group) noexcept {
    return static_cast<std::uint8_t>(0x80 | group);
}

constexpr std::uint8_t instance_type(std::uint8_t type) noexcept {
    return static_cast<std::uint8_t>(0xC0 | type);
}

inline constexpr std::uint8_t instance_device = 0xFE;
inline constexpr std::uint8_t instance_broadcast = 0xFF;

// Part 103 Table 4 instance types implemented by this gateway.
namespace type {
inline constexpr std::uint8_t push_button = 1;
inline constexpr std::uint8_t occupancy = 3;
inline constexpr std::uint8_t light = 4;
}  // namespace type

constexpr std::uint32_t command(std::uint8_t address, std::uint8_t instance,
                                std::uint8_t opcode) noexcept {
    return (static_cast<std::uint32_t>(address) << 16) |
           (static_cast<std::uint32_t>(instance) << 8) | opcode;
}

// Table 21 device-level opcodes (instance byte 0xFE "device").
namespace cmd {
inline constexpr std::uint8_t set_short_address = 0x14;
inline constexpr std::uint8_t start_quiescent_mode = 0x1D;
inline constexpr std::uint8_t stop_quiescent_mode = 0x1E;
inline constexpr std::uint8_t query_device_status = 0x30;
inline constexpr std::uint8_t query_missing_short_address = 0x33;
inline constexpr std::uint8_t query_number_of_instances = 0x35;
inline constexpr std::uint8_t query_random_address_h = 0x39;
inline constexpr std::uint8_t query_random_address_m = 0x3A;
inline constexpr std::uint8_t query_random_address_l = 0x3B;
inline constexpr std::uint8_t query_quiescent_mode = 0x40;
}  // namespace cmd

// Table 21 instance-level opcodes (instance byte selects the instance).
namespace inst {
inline constexpr std::uint8_t set_event_priority = 0x61;
inline constexpr std::uint8_t enable_instance = 0x62;
inline constexpr std::uint8_t disable_instance = 0x63;
inline constexpr std::uint8_t set_primary_instance_group = 0x64;
inline constexpr std::uint8_t set_instance_group1 = 0x65;
inline constexpr std::uint8_t set_instance_group2 = 0x66;
inline constexpr std::uint8_t set_event_scheme = 0x67;
inline constexpr std::uint8_t set_event_filter = 0x68;
inline constexpr std::uint8_t query_instance_type = 0x80;
inline constexpr std::uint8_t query_resolution = 0x81;
inline constexpr std::uint8_t query_instance_error = 0x82;
inline constexpr std::uint8_t query_instance_status = 0x83;
inline constexpr std::uint8_t query_event_priority = 0x84;
inline constexpr std::uint8_t query_instance_enabled = 0x86;
inline constexpr std::uint8_t query_primary_instance_group = 0x88;
inline constexpr std::uint8_t query_instance_group1 = 0x89;
inline constexpr std::uint8_t query_instance_group2 = 0x8A;
inline constexpr std::uint8_t query_event_scheme = 0x8B;
inline constexpr std::uint8_t query_input_value = 0x8C;
inline constexpr std::uint8_t query_input_value_latch = 0x8D;
inline constexpr std::uint8_t query_feature_type = 0x8E;
inline constexpr std::uint8_t query_next_feature_type = 0x8F;
inline constexpr std::uint8_t query_event_filter_0_7 = 0x90;
inline constexpr std::uint8_t query_event_filter_8_15 = 0x91;
inline constexpr std::uint8_t query_event_filter_16_23 = 0x92;
}  // namespace inst

// Event schemes (Part 103 Table 8); events are addressed device/instance.
inline constexpr std::uint8_t event_scheme_device_instance = 2;

// Table 22 special commands: first byte 0xC1, second byte the operation;
// the operand (where any) occupies the third byte.
namespace special {
constexpr std::uint32_t terminate() noexcept {
    return command(0xC1, 0x00, 0x00);
}
// Operand: 0x00 = all devices, 0xFF = devices without a short address,
// 0AAAAAA1 = the addressed device only.
constexpr std::uint32_t initialise(std::uint8_t operand) noexcept {
    return command(0xC1, 0x01, operand);
}
constexpr std::uint32_t randomise() noexcept {
    return command(0xC1, 0x02, 0x00);
}
constexpr std::uint32_t compare() noexcept {
    return command(0xC1, 0x03, 0x00);
}
constexpr std::uint32_t withdraw() noexcept {
    return command(0xC1, 0x04, 0x00);
}
constexpr std::uint32_t searchaddr_h(std::uint8_t value) noexcept {
    return command(0xC1, 0x05, value);
}
constexpr std::uint32_t searchaddr_m(std::uint8_t value) noexcept {
    return command(0xC1, 0x06, value);
}
constexpr std::uint32_t searchaddr_l(std::uint8_t value) noexcept {
    return command(0xC1, 0x07, value);
}
constexpr std::uint32_t program_short_address(std::uint8_t encoded) noexcept {
    return command(0xC1, 0x08, encoded);
}
constexpr std::uint32_t verify_short_address(std::uint8_t encoded) noexcept {
    return command(0xC1, 0x09, encoded);
}
constexpr std::uint32_t query_short_address() noexcept {
    return command(0xC1, 0x0A, 0x00);
}
constexpr std::uint32_t write_memory_location(std::uint8_t value) noexcept {
    return command(0xC1, 0x20, value);
}
constexpr std::uint32_t write_memory_location_no_reply(std::uint8_t value) noexcept {
    return command(0xC1, 0x21, value);
}
constexpr std::uint32_t dtr0(std::uint8_t value) noexcept {
    return command(0xC1, 0x30, value);
}
constexpr std::uint32_t dtr1(std::uint8_t value) noexcept {
    return command(0xC1, 0x31, value);
}
constexpr std::uint32_t dtr2(std::uint8_t value) noexcept {
    return command(0xC1, 0x32, value);
}
constexpr std::uint32_t send_testframe(std::uint8_t value) noexcept {
    return command(0xC1, 0x33, value);
}
constexpr std::uint32_t direct_write_memory(std::uint8_t param1,
                                            std::uint8_t param2) noexcept {
    return command(0xC5, param1, param2);
}
constexpr std::uint32_t dtr1_dtr0(std::uint8_t value1, std::uint8_t value0) noexcept {
    return command(0xC7, value1, value0);
}
constexpr std::uint32_t dtr2_dtr1(std::uint8_t value2, std::uint8_t value1) noexcept {
    return command(0xC9, value2, value1);
}

// Operand encoding shared with Part 102: 0AAAAAA1.
constexpr std::uint8_t encode_short_address(unsigned address) noexcept {
    return static_cast<std::uint8_t>((address << 1) | 0x01);
}
}  // namespace special

}  // namespace gateway::dali::device
