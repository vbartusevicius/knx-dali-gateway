#pragma once

#include <cstdint>

// IEC 62386-102 standard command opcodes (second byte of an addressed
// frame) used by the Transport adapters.
namespace gateway::dali::cmd {

inline constexpr std::uint8_t off = 0x00;
inline constexpr std::uint8_t up = 0x01;
inline constexpr std::uint8_t down = 0x02;
inline constexpr std::uint8_t step_up = 0x03;
inline constexpr std::uint8_t step_down = 0x04;
inline constexpr std::uint8_t recall_max_level = 0x05;
inline constexpr std::uint8_t recall_min_level = 0x06;
inline constexpr std::uint8_t step_down_and_off = 0x07;
inline constexpr std::uint8_t on_and_step_up = 0x08;
inline constexpr std::uint8_t enable_dapc_sequence = 0x09;
inline constexpr std::uint8_t go_to_last_active_level = 0x0A;
inline constexpr std::uint8_t go_to_scene_0 = 0x10;
inline constexpr std::uint8_t reset = 0x20;
inline constexpr std::uint8_t store_actual_level_in_dtr0 = 0x21;
inline constexpr std::uint8_t save_persistent_variables = 0x22;
inline constexpr std::uint8_t set_operating_mode = 0x23;
inline constexpr std::uint8_t reset_memory_bank = 0x24;
inline constexpr std::uint8_t identify_device = 0x25;
inline constexpr std::uint8_t set_max_level = 0x2A;
inline constexpr std::uint8_t set_min_level = 0x2B;
inline constexpr std::uint8_t set_system_failure_level = 0x2C;
inline constexpr std::uint8_t set_power_on_level = 0x2D;
inline constexpr std::uint8_t set_fade_time = 0x2E;
inline constexpr std::uint8_t set_fade_rate = 0x2F;
inline constexpr std::uint8_t set_extended_fade_time = 0x30;
inline constexpr std::uint8_t store_dtr_as_scene_0 = 0x40;
inline constexpr std::uint8_t remove_from_scene_0 = 0x50;
inline constexpr std::uint8_t add_to_group_0 = 0x60;
inline constexpr std::uint8_t remove_from_group_0 = 0x70;
inline constexpr std::uint8_t store_dtr_as_short_address = 0x80;
inline constexpr std::uint8_t enable_write_memory = 0x81;
inline constexpr std::uint8_t query_status = 0x90;
inline constexpr std::uint8_t query_control_gear = 0x91;
inline constexpr std::uint8_t query_lamp_failure = 0x92;
inline constexpr std::uint8_t query_lamp_power_on = 0x93;
inline constexpr std::uint8_t query_limit_error = 0x94;
inline constexpr std::uint8_t query_reset_state = 0x95;
inline constexpr std::uint8_t query_missing_short_address = 0x96;
inline constexpr std::uint8_t query_version_number = 0x97;
inline constexpr std::uint8_t query_content_dtr0 = 0x98;
inline constexpr std::uint8_t query_device_type = 0x99;
inline constexpr std::uint8_t query_physical_minimum = 0x9A;
inline constexpr std::uint8_t query_power_failure = 0x9B;
inline constexpr std::uint8_t query_content_dtr1 = 0x9C;
inline constexpr std::uint8_t query_content_dtr2 = 0x9D;
inline constexpr std::uint8_t query_operating_mode = 0x9E;
inline constexpr std::uint8_t query_light_source_type = 0x9F;
inline constexpr std::uint8_t query_actual_level = 0xA0;
inline constexpr std::uint8_t query_max_level = 0xA1;
inline constexpr std::uint8_t query_min_level = 0xA2;
inline constexpr std::uint8_t query_power_on_level = 0xA3;
inline constexpr std::uint8_t query_system_failure_level = 0xA4;
inline constexpr std::uint8_t query_fade_time_rate = 0xA5;
inline constexpr std::uint8_t query_manufacturer_specific_mode = 0xA6;
inline constexpr std::uint8_t query_next_device_type = 0xA7;
inline constexpr std::uint8_t query_extended_fade_time = 0xA8;
inline constexpr std::uint8_t query_control_gear_failure = 0xAA;
inline constexpr std::uint8_t query_scene_level_0 = 0xB0;
inline constexpr std::uint8_t query_groups_0_7 = 0xC0;
inline constexpr std::uint8_t query_groups_8_15 = 0xC1;
inline constexpr std::uint8_t query_random_address_h = 0xC2;
inline constexpr std::uint8_t query_random_address_m = 0xC3;
inline constexpr std::uint8_t query_random_address_l = 0xC4;
inline constexpr std::uint8_t read_memory_location = 0xC5;

}
