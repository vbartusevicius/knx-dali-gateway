#pragma once

#include <cstdint>

// IEC 62386-102 standard command opcodes (second byte of an addressed
// frame) used by the Transport adapters.
namespace gateway::dali::cmd {

inline constexpr std::uint8_t off = 0x00;
inline constexpr std::uint8_t up = 0x01;
inline constexpr std::uint8_t down = 0x02;
inline constexpr std::uint8_t recall_max_level = 0x05;
inline constexpr std::uint8_t on_and_step_up = 0x08;
inline constexpr std::uint8_t go_to_scene_0 = 0x10;
inline constexpr std::uint8_t store_actual_level_in_dtr0 = 0x21;
inline constexpr std::uint8_t store_dtr_as_scene_0 = 0x40;
inline constexpr std::uint8_t add_to_group_0 = 0x60;
inline constexpr std::uint8_t remove_from_group_0 = 0x70;
inline constexpr std::uint8_t store_dtr_as_short_address = 0x80;
inline constexpr std::uint8_t query_status = 0x90;
inline constexpr std::uint8_t query_control_gear = 0x91;
inline constexpr std::uint8_t query_actual_level = 0xA0;
inline constexpr std::uint8_t query_groups_0_7 = 0xC0;
inline constexpr std::uint8_t query_groups_8_15 = 0xC1;

}
