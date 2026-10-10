#pragma once

#include <cstdint>

// IEC 62386-102 Table 16 special-command opcodes (byte 0 of the frame;
// byte 1 carries the operand) and the IEC 62386-103 device-level broadcast
// command bytes used for quiescent mode.
namespace gateway::dali::special {

inline constexpr std::uint8_t terminate = 0xA1;
inline constexpr std::uint8_t dtr0 = 0xA3;
inline constexpr std::uint8_t initialise = 0xA5;
inline constexpr std::uint8_t randomise = 0xA7;
inline constexpr std::uint8_t compare = 0xA9;
inline constexpr std::uint8_t withdraw = 0xAB;
inline constexpr std::uint8_t ping = 0xAD;
inline constexpr std::uint8_t searchaddr_h = 0xB1;
inline constexpr std::uint8_t searchaddr_m = 0xB3;
inline constexpr std::uint8_t searchaddr_l = 0xB5;
inline constexpr std::uint8_t program_short_address = 0xB7;
inline constexpr std::uint8_t verify_short_address = 0xB9;
inline constexpr std::uint8_t query_short_address = 0xBB;
inline constexpr std::uint8_t enable_device_type = 0xC1;
inline constexpr std::uint8_t dtr1 = 0xC3;
inline constexpr std::uint8_t dtr2 = 0xC5;

// Encodes the operand byte of PROGRAM/VERIFY/QUERY SHORT ADDRESS.
inline constexpr std::uint8_t encode_short_address(unsigned address) {
    return static_cast<std::uint8_t>((address << 1) | 0x01);
}

}
